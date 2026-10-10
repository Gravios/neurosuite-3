/***************************************************************************
                          tracewidget.cpp  -  description
                             -------------------
    begin                : Wed Mar 17 2004
    copyright            : (C) 2004 by Lynn Hazan
    email                : lynn.hazan.myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
//include files for the application
#include "tracewidget.h"
#include "spectralview.h"
#include "freqbandslider.h"

// Shared input registry (neuroscope input overhaul S3 — claude/neuroscope-input-plan.md).
#include "input/bindingregistry.h"
#include "input/inputdispatcher.h"   // input::registry()
#include "input/chord.h"

#include <QShortcut>
#include <QAction>
// Qt6 PMF connect requires complete type for ItemColors* in eventsAvailable signal signature
#include "itemcolors.h"
#include <QScrollBar>
#include <QSpinBox>

// include files for QT
#include <QString>

#include <QFrame>
#include <QList>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDebug>
#include <QVBoxLayout>
#include <QHBoxLayout>


/// Added by M.Zugaro to enable automatic forward paging
#include <QTimer>

namespace {
// Register the trace view's resolver-dispatched key commands once (neuroscope input
// overhaul S3).  A "view.trace" ViewType scope — active whenever the event's view is a
// TraceWidget — carries the +/- time-window keys.  Each command acts on ctx.view (the
// TraceWidget that received the key), so one registration serves every display.  The
// chords are AtLeast+NoModifier on Key_Plus/Key_Minus, matching the former switch, which
// fired on those keys regardless of modifiers (numpad "+" or Shift-based "+").
void registerTraceInputOnce()
{
    static bool done = false;
    if(done) return;
    done = true;

    input::BindingRegistry& reg = input::registry();

    input::InputScope s;
    s.id     = QStringLiteral("view.trace");
    s.layer  = input::Layer::ViewType;
    s.active = [](const input::Ctx& c){ return qobject_cast<TraceWidget*>(c.view) != nullptr; };
    reg.addScope(s);

    auto anyMod = [](int key){
        return input::Chord{ input::Device::Key, key, Qt::NoModifier,
                             input::Phase::Press, input::ModMatch::AtLeast };
    };
    auto add = [&](const QString& id, const QString& label, int key, void (TraceWidget::*fn)()){
        input::Command c;
        c.id       = id;
        c.scopeId  = QStringLiteral("view.trace");
        c.label    = label;
        c.category = TraceWidget::tr("Trace view");
        c.kind     = input::Kind::Action;
        c.defaultChord = anyMod(key);
        c.invoke   = [fn](const input::Ctx& ctx){
            if(auto* tw = qobject_cast<TraceWidget*>(ctx.view)) (tw->*fn)();
        };
        reg.addCommand(c);
    };
    add(QStringLiteral("trace.durationDouble"), TraceWidget::tr("Double the time window"), Qt::Key_Plus,  &TraceWidget::doubleTimeWindow);
    add(QStringLiteral("trace.durationHalve"),  TraceWidget::tr("Halve the time window"),  Qt::Key_Minus, &TraceWidget::halveTimeWindow);

    // External mirrors of the Left/Right quarter-window scroll QActions (S3b).  Qt dispatches
    // those via their WidgetWithChildrenShortcut; registering them here makes them discoverable
    // + conflict-checked.  external => resolve() skips them, so TraceWidget::keyPressEvent never
    // double-scrolls.
    auto addExternal = [&](const QString& id, const QString& label, int key){
        input::Command c;
        c.id       = id;
        c.scopeId  = QStringLiteral("view.trace");
        c.label    = label;
        c.category = TraceWidget::tr("Trace view");
        c.kind     = input::Kind::Action;
        c.external = true;
        c.defaultChord = input::Chord::key(key);
        reg.addCommand(c);
    };
    addExternal(QStringLiteral("trace.scrollLeft"),  TraceWidget::tr("Scroll left (quarter window)"),  Qt::Key_Left);
    addExternal(QStringLiteral("trace.scrollRight"), TraceWidget::tr("Scroll right (quarter window)"), Qt::Key_Right);

    // Tool-mode scopes (neuroscope input overhaul S4): one per TraceView interaction mode,
    // active when the *pressed* trace view is in that mode (read via BaseFrame::currentMode()).
    // Inert for now — no commands — so the S2 seam still falls through to TraceView's
    // mode-switched mouse handler.  S5 hangs each mode's press gesture off its scope, retiring
    // the monolith one mode at a time.  ToolMode layer, Passive (a mode binding nothing falls
    // through).
    auto modeScope = [&](const QString& id, int modeValue){
        input::InputScope sc;
        sc.id     = id;
        sc.layer  = input::Layer::ToolMode;
        sc.active = [modeValue](const input::Ctx& c){
            auto* v = qobject_cast<TraceView*>(c.view);
            return v && v->currentMode() == modeValue;
        };
        reg.addScope(sc);
    };
    modeScope(QStringLiteral("mode.zoom"),           BaseFrame::ZOOM);
    modeScope(QStringLiteral("mode.selectChannels"), TraceView::SELECT);
    modeScope(QStringLiteral("mode.measure"),        TraceView::MEASURE);
    modeScope(QStringLiteral("mode.selectTime"),     TraceView::SELECT_TIME);
    modeScope(QStringLiteral("mode.selectEvent"),    TraceView::SELECT_EVENT);
    modeScope(QStringLiteral("mode.addEvent"),       TraceView::ADD_EVENT);
    modeScope(QStringLiteral("mode.drawLine"),       TraceView::DRAW_LINE);

    // S5 — first ToolMode gesture: the rubber-band ZOOM press.  A Left press (any modifiers)
    // in ZOOM mode begins the band.  Gesture kind — invoke() only BEGINS it; the drag preview
    // (BaseFrame::mouseMoveEvent) and the zoom commit (BaseFrame::mouseReleaseEvent) stay in
    // the base handlers, reached because dispatch() maps only the press (release/move fall
    // through).  AtLeast+NoModifier on LeftButton reproduces the former inline gate exactly:
    // TraceView::mousePressEvent delegated mode==ZOOM to BaseFrame::mousePressEvent on any Left
    // press, ignoring modifiers at press (Shift only means "shrink" at the release commit).
    // Resolves only in ZOOM via the mode.zoom scope, so the other modes still fall through.
    {
        input::Command z;
        z.id       = QStringLiteral("trace.zoomRubberBand");
        z.scopeId  = QStringLiteral("mode.zoom");
        z.label    = TraceView::tr("Rubber-band zoom");
        z.category = TraceView::tr("Zoom");
        z.kind     = input::Kind::Gesture;
        z.defaultChord = input::Chord::button(Qt::LeftButton, Qt::NoModifier,
                                              input::Phase::Press, input::ModMatch::AtLeast);
        z.invoke   = [](const input::Ctx& c){
            auto* v = qobject_cast<TraceView*>(c.view);
            if(v && c.event)
                v->beginBaseZoom(static_cast<QMouseEvent*>(c.event)->position().toPoint());
        };
        reg.addCommand(z);
    }

    // S5 mode 2/7 — the SELECT_TIME press.  A Left press (any modifiers) in SELECT_TIME mode
    // begins a full-height selection band and records the drag's starting abscissa.  Gesture
    // kind — invoke() only begins it; the drag preview (BaseFrame::mouseMoveEvent) and the
    // time-range commit (TraceView::mouseReleaseEvent) stay inline (dispatch maps only the
    // press).  Same AtLeast+NoModifier-on-Left gate the old inline branch used; resolves only
    // in SELECT_TIME via the mode.selectTime scope.
    {
        input::Command st;
        st.id       = QStringLiteral("trace.selectTimePress");
        st.scopeId  = QStringLiteral("mode.selectTime");
        st.label    = TraceView::tr("Begin time-range selection");
        st.category = TraceView::tr("Select time");
        st.kind     = input::Kind::Gesture;
        st.defaultChord = input::Chord::button(Qt::LeftButton, Qt::NoModifier,
                                              input::Phase::Press, input::ModMatch::AtLeast);
        st.invoke   = [](const input::Ctx& c){
            auto* v = qobject_cast<TraceView*>(c.view);
            if(v && c.event)
                v->beginSelectTimePress(static_cast<QMouseEvent*>(c.event)->position().toPoint());
        };
        reg.addCommand(st);
    }

    // S5 mode 3/7 — the ADD_EVENT press.  A Left press (any modifiers) in ADD_EVENT mode records
    // the clicked sample position for a new event; the release creates it.  No rubber band, no
    // drag — invoke() does the whole press.  Same AtLeast+NoModifier-on-Left gate as the old
    // inline branch; resolves only in ADD_EVENT via the mode.addEvent scope.
    {
        input::Command ae;
        ae.id       = QStringLiteral("trace.addEventPress");
        ae.scopeId  = QStringLiteral("mode.addEvent");
        ae.label    = TraceView::tr("Place a new event");
        ae.category = TraceView::tr("Add event");
        ae.kind     = input::Kind::Gesture;
        ae.defaultChord = input::Chord::button(Qt::LeftButton, Qt::NoModifier,
                                              input::Phase::Press, input::ModMatch::AtLeast);
        ae.invoke   = [](const input::Ctx& c){
            auto* v = qobject_cast<TraceView*>(c.view);
            if(v && c.event)
                v->beginAddEventPress(static_cast<QMouseEvent*>(c.event)->position().toPoint());
        };
        reg.addCommand(ae);
    }

    // S5 mode 4/7 — the DRAW_LINE press.  A Left press (any modifiers) in DRAW_LINE mode seeds the
    // line positions and arms the drag; invoke() does the press, the drag/commit stay inline.  No
    // rubber band.  Same AtLeast+NoModifier-on-Left gate; resolves only in DRAW_LINE via the
    // mode.drawLine scope.
    {
        input::Command dl;
        dl.id       = QStringLiteral("trace.drawLinePress");
        dl.scopeId  = QStringLiteral("mode.drawLine");
        dl.label    = TraceView::tr("Begin drawing a line");
        dl.category = TraceView::tr("Draw line");
        dl.kind     = input::Kind::Gesture;
        dl.defaultChord = input::Chord::button(Qt::LeftButton, Qt::NoModifier,
                                              input::Phase::Press, input::ModMatch::AtLeast);
        dl.invoke   = [](const input::Ctx& c){
            auto* v = qobject_cast<TraceView*>(c.view);
            if(v && c.event)
                v->beginDrawLinePress(static_cast<QMouseEvent*>(c.event)->position().toPoint());
        };
        reg.addCommand(dl);
    }

    // S5 mode 5/7 — the SELECT_EVENT press.  A Left press (any modifiers) in SELECT_EVENT mode
    // picks the nearest selected event under the click so a drag can move it; invoke() does the
    // press, the drag/commit stay inline.  No rubber band.  Same AtLeast+NoModifier-on-Left gate;
    // resolves only in SELECT_EVENT via the mode.selectEvent scope.
    {
        input::Command se;
        se.id       = QStringLiteral("trace.selectEventPress");
        se.scopeId  = QStringLiteral("mode.selectEvent");
        se.label    = TraceView::tr("Pick an event");
        se.category = TraceView::tr("Select event");
        se.kind     = input::Kind::Gesture;
        se.defaultChord = input::Chord::button(Qt::LeftButton, Qt::NoModifier,
                                              input::Phase::Press, input::ModMatch::AtLeast);
        se.invoke   = [](const input::Ctx& c){
            auto* v = qobject_cast<TraceView*>(c.view);
            if(v && c.event)
                v->beginSelectEventPress(static_cast<QMouseEvent*>(c.event)->position().toPoint());
        };
        reg.addCommand(se);
    }
}
}  // namespace

TraceWidget::TraceWidget(long startTime,long duration,bool greyScale,TracesProvider& tracesProvider,bool multiColumns,bool verticalLines,
                         bool raster,bool waveforms,bool labelsDisplay,QList<int>& channelsToDisplay,int gain,int acquisitionGain,
                         ChannelColors* channelColors,QMap<int, QList<int> >* groupsChannels,
                         QMap<int,int>* channelsGroups,bool autocenterChannels,QList<int>& channelOffsets,QList<int>& gains,const QList<int>& skippedChannels,
                         int rasterHeight,const QImage& backgroundImage,QWidget* parent,
                         const char* name,const QColor& backgroundColor,QStatusBar* statusBar,
                         int minSize,int maxSize,int windowTopLeft,int windowBottomRight,int border):
   QWidget(parent),timeWindow(duration),
    view(tracesProvider,greyScale,multiColumns,verticalLines,raster,waveforms,labelsDisplay,channelsToDisplay,gain,acquisitionGain,
         startTime,timeWindow,channelColors,groupsChannels,channelsGroups,autocenterChannels,channelOffsets,gains,skippedChannels,rasterHeight,backgroundImage,this,name,
         backgroundColor,statusBar,minSize,maxSize,windowTopLeft,windowBottomRight,border),
    mTracesProvider(tracesProvider),
    startTime(startTime),
    validator(this),
    isInit(true),
    updateView(true),
    statusBar(statusBar)
{
    registerTraceInputOnce();   // input overhaul S3: the +/- duration commands (once, process-wide)

    QVBoxLayout *lay = new QVBoxLayout;
    setLayout(lay);
    mainLayout = lay;
    currentChannels = channelsToDisplay;

    // "u" applies any pending spectral parameter change (manual update mode).
    // Left / Right arrows scroll the time window by a quarter of its width.
    // Scoped to this widget and its children; text fields keep the keys for
    // cursor movement (they accept the shortcut override), so only the data
    // views and scroll bar are affected.
    // Input overhaul S3b: Left/Right were standalone QShortcuts; they are now QActions so
    // they are registry-visible + rebindable (mirrored as external trace.scrollLeft /
    // trace.scrollRight in registerTraceInputOnce()).  WidgetWithChildrenShortcut + addAction
    // reproduce the former QShortcuts' reach exactly (fire while this widget or a child has
    // focus), and a QAction shortcut honours the same override, so text fields keep the arrows.
    QAction* scrollLeft  = new QAction(tr("Scroll left"),  this);
    QAction* scrollRight = new QAction(tr("Scroll right"), this);
    scrollLeft->setShortcut(QKeySequence(Qt::Key_Left));
    scrollRight->setShortcut(QKeySequence(Qt::Key_Right));
    scrollLeft->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    scrollRight->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(scrollLeft);
    addAction(scrollRight);
    connect(scrollLeft,  &QAction::triggered, this, [this]() { scrollByQuarterWindow(-1); });
    connect(scrollRight, &QAction::triggered, this, [this]() { scrollByQuarterWindow(+1); });
    recordingLength = tracesProvider.recordingLength();

    selectionWidgets = new QWidget(this);
    lay->addWidget(&view);
    lay->addWidget(selectionWidgets);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->setStretchFactor(selectionWidgets,0);
    lay->setStretchFactor(&view,200);

    setFocusPolicy(Qt::NoFocus);

    initSelectionWidgets();
    adjustSize();

    connect(&view,&TraceView::channelsSelected,this, &TraceWidget::slotChannelsSelected);
    connect(&view,&TraceView::setStartAndDuration,this, &TraceWidget::slotSetStartAndDuration);
    connect(&view,&TraceView::eventModified,this, &TraceWidget::slotEventModified);
    connect(&view,&TraceView::eventRemoved,this, &TraceWidget::slotEventRemoved);
    connect(&view,&TraceView::eventAdded,this, &TraceWidget::slotEventAdded);
    connect(&view,&TraceView::eventsAvailable,this, &TraceWidget::slotEventsAvailable);

    isInit = false;
    /// Added by M.Zugaro to enable automatic forward paging
    timer = new QTimer(this);
    connect(timer,&QTimer::timeout, this, &TraceWidget::advance);
    pageTime = 500;
}

TraceWidget::~TraceWidget(){
}

/// Added by M.Zugaro to enable automatic forward paging
void TraceWidget::page()
{
    if ( timer->isActive() )
        timer->stop();
    else
	 {
        timer->start(pageTime);
		  statusBar->showMessage(tr("Auto-advance every %1 ms").arg(pageTime));
	 }
}

bool TraceWidget::isStill()
{
	return ! ( timer != nullptr && timer->isActive() );
}

void TraceWidget::stop()
{
	if ( timer->isActive() )
	{
			timer->stop();
			emit stopped();
	}
}

void TraceWidget::accelerate()
{
    if ( !timer->isActive() )
        return;
    pageTime -= 125;
    if ( pageTime < 0 ) pageTime = 0;
    statusBar->showMessage(tr("Auto-advance every %1 ms").arg(pageTime));
    timer->start(pageTime);
}

void TraceWidget::decelerate()
{
    if ( !timer->isActive() ) return;
    pageTime += 125;
    if ( pageTime > 1000 ) pageTime = 1000;
    statusBar->showMessage(tr("Auto-advance every %1 ms").arg(pageTime));
    timer->start(pageTime);
}

/// Added by M.Zugaro to enable automatic forward paging
void TraceWidget::advance()
{
	 // Temporarily disconnect so that changes to scrollbar and time boxes do not automatically stop paging!
	 disconnect(startMinute, &QSpinBox::valueChanged, this, &TraceWidget::stop);
    disconnect(startSecond, &QSpinBox::valueChanged, this, &TraceWidget::stop);
    disconnect(startMilisecond, &QSpinBox::valueChanged, this, &TraceWidget::stop);
    disconnect(scrollBar, &QScrollBar::valueChanged, this, &TraceWidget::stop);
	 
    // Because data files are expected to have grown, update recording length,
    // as well as spin box and scroll bar in the view
    view.updateRecordingLength();
    recordingLength = view.recordingLength();
    minutePart = recordingLength / 60000;
    int remainingSeconds = static_cast<int>(fmod(static_cast<double>(recordingLength),60000));
    secondPart = remainingSeconds / 1000;
    milisecondPart = static_cast<int>(fmod(static_cast<double>(remainingSeconds),1000));
    startMinute->setMaximum(minutePart);
    scrollBar->setMaximum(recordingLength - timeWindow);

    // Move one page
    /*	startTime += timeWindow;
    if ( startTime + timeWindow > recordingLength ) correctStartTime();*/
    updateView = false; // do not redraw yet
    correctStartTime();
    updateView = true;
    //Inform the traceView
    informViewsTimeFrame();
	 
    //Inform listener of the modification
    emit updateStartAndDuration(startTime,timeWindow);
	 
	 // Reconnect
	 connect(startMinute,&QSpinBox::valueChanged,this, &TraceWidget::stop);
    connect(startSecond,&QSpinBox::valueChanged,this, &TraceWidget::stop);
    connect(startMilisecond,&QSpinBox::valueChanged,this, &TraceWidget::stop);
    connect(scrollBar, &QScrollBar::valueChanged, this, &TraceWidget::stop);
	 
    timer->start(pageTime); // restart timer
}

void TraceWidget::changeBackgroundColor(const QColor &color)
{
    view.changeBackgroundColor(color);
    update();
}

void TraceWidget::setGreyScale(bool grey)
{
    view.setGreyScale(grey);
}

void TraceWidget::initSelectionWidgets()
{
    QHBoxLayout *lay = new QHBoxLayout;
    selectionWidgets->setLayout(lay);
    QFont font("Helvetica",9);

    //Create and initialize the spin boxe and lineEdit.
    startLabel = new QLabel("Start time",selectionWidgets);
    startLabel->setFrameStyle(QFrame::StyledPanel|QFrame::Plain);
    startLabel->setFont(font);
    lay->addWidget(startLabel);

    minutePart = recordingLength / 60000;
    int remainingSeconds = static_cast<int>(fmod(static_cast<double>(recordingLength),60000));
    secondPart = remainingSeconds / 1000;
    milisecondPart = static_cast<int>(fmod(static_cast<double>(remainingSeconds),1000));

    int nbMinutes = startTime / 60000;
    remainingSeconds = static_cast<int>(fmod(static_cast<double>(startTime),60000));
    int nbSeconds = remainingSeconds / 1000;
    int remainingMiliseconds = static_cast<int>(fmod(static_cast<double>(remainingSeconds),1000));

    startMinute = new QSpinBox(selectionWidgets);
    startMinute->setMinimum(0);
    startMinute->setMaximum(minutePart);
    startMinute->setSingleStep(1);
    lay->addWidget(startMinute);
    startMinute->setSuffix( tr(" min") );
    startMinute->setWrapping(true);
    startMinute->setValue(nbMinutes);
    startSecond = new QSpinBox(selectionWidgets);
    startSecond->setMinimum(0);
    startSecond->setMaximum(recordingLength/1000);
    startSecond->setSingleStep(1);
    lay->addWidget(startSecond);
    startSecond->setSuffix( tr(" s") );
    startSecond->setValue(nbSeconds);
    startMilisecond = new QSpinBox(selectionWidgets);

    startMilisecond->setMinimum(0);
    startMilisecond->setMaximum(recordingLength);
    startMilisecond->setSingleStep(1);

    lay->addWidget(startMilisecond);
    startMilisecond->setSuffix( tr(" ms") );
    startMilisecond->setValue(remainingMiliseconds);


    durationLabel = new QLabel(tr("  Duration (ms)"),selectionWidgets);
    lay->addWidget(durationLabel);
    durationLabel->setFrameStyle(QFrame::StyledPanel|QFrame::Plain);
    durationLabel->setFont(font);
    duration = new QLineEdit(QString::number(timeWindow),selectionWidgets);
    lay->addWidget(duration);
    duration->setMinimumSize(50,duration->minimumHeight());
    duration->setMaximumSize(50,duration->maximumHeight());
    duration->setMaxLength(5);
    //duration will only accept integers between 0 and a max equal
    //to maximum of time for the current document (set when the document will be opened)
    duration->setValidator(&validator);

    connect(startMinute, &QAbstractSpinBox::editingFinished, this, &TraceWidget::slotStartMinuteTimeUpdated);
    connect(startSecond, &QAbstractSpinBox::editingFinished, this, &TraceWidget::slotStartSecondTimeUpdated);
    connect(startMilisecond, &QAbstractSpinBox::editingFinished, this, &TraceWidget::slotStartMilisecondTimeUpdated);

	 /// Added by M.Zugaro to enable automatic forward paging
	 connect(startMinute,&QSpinBox::valueChanged,this, &TraceWidget::stop);
    connect(startSecond,&QSpinBox::valueChanged,this, &TraceWidget::stop);
    connect(startMilisecond,&QSpinBox::valueChanged,this, &TraceWidget::stop);
    connect(duration,&QLineEdit::returnPressed,this, &TraceWidget::slotDurationUpdated);

    //Create and initialize the scrollbar. The line step is a 20iest of the page step
    pageStep = timeWindow;
    lineStep = static_cast<long>(floor(0.5 + static_cast<float>(static_cast<float>(timeWindow) / static_cast<float>(20))));

    scrollBar = new QScrollBar(selectionWidgets);
    scrollBar->setOrientation(Qt::Horizontal);
    scrollBar->setMinimum(0);
    scrollBar->setMaximum(recordingLength - timeWindow);
    scrollBar->setSingleStep(lineStep);
    scrollBar->setPageStep(pageStep);


    lay->addWidget(scrollBar);
    scrollBar->setValue(startTime);
    connect(scrollBar, &QAbstractSlider::sliderReleased, this, &TraceWidget::slotScrollBarUpdated);
    connect(scrollBar, &QScrollBar::valueChanged, this, &TraceWidget::slotScrollBarUpdated);
    connect(scrollBar, &QScrollBar::valueChanged, this, &TraceWidget::stop);

    //enable the user to use the keyboard to interact with the scrollbar.
    scrollBar->setMouseTracking(false);
    scrollBar->setFocusPolicy(Qt::StrongFocus);

    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->setStretchFactor(startLabel,0);
    lay->setStretchFactor(startMinute,0);
    lay->setStretchFactor(startSecond,0);
    lay->setStretchFactor(startMilisecond,0);
    lay->setStretchFactor(durationLabel,0);
    lay->setStretchFactor(duration,0);
    lay->setStretchFactor(scrollBar,200);
}

void TraceWidget::samplingRateModified(qlonglong length)
{
    recordingLength = length;

    view.samplingRateModified(length);

    //Reset the position
    slotSetStartAndDuration(0,50);
}

void TraceWidget::doubleTimeWindow()
{
    timeWindow = timeWindow * 2;
    duration->setText(QString::number(timeWindow));
    slotDurationUpdated();
}

void TraceWidget::halveTimeWindow()
{
    timeWindow = timeWindow / 2;
    duration->setText(QString::number(timeWindow));
    slotDurationUpdated();
}

void TraceWidget::keyPressEvent(QKeyEvent* event)
{
    // Dispatch through the shared input registry (neuroscope input overhaul S3): the +/-
    // duration keys are now the view.trace commands trace.durationDouble / trace.durationHalve
    // (rebindable + discoverable).  allowAutoRepeat so a held key keeps stepping, as the
    // original switch did.  Keys bound to nothing are left alone — exactly as the former
    // switch, which had no default and did not chain to the base handler.
    input::Ctx ctx;
    ctx.view  = this;
    ctx.event = event;
    if(const input::Command* cmd =
           input::registry().resolve(input::chordFromEvent(event, /*allowAutoRepeat=*/true), ctx))
        if(cmd->invoke) cmd->invoke(ctx);
}

void TraceWidget::slotDurationUpdated()
{
    if(!isInit && updateView){
        //Modify updateView to prevent the scrollBar to trigger a changeEvent while been updated.
        updateView = false;

        timeWindow = (duration->displayText()).toLong();

        //Test if the time window is bigger than the time of the recording, if so fix it to the time of the recording
        if(timeWindow > recordingLength){
            timeWindow = recordingLength;
            duration->setText(QString::number(timeWindow));
        }
        //Test if the time window is inferior to 1 ms, if so fix set it to the minimum 1.
        if(timeWindow < 1){
            timeWindow = 1;
            duration->setText("1");
        }


        //Test if we go over the time of the recording if so keep the time window and move back in time
        if((startTime + timeWindow) > recordingLength)
            correctStartTime();
        else{
            startMinute->setMaximum(minutePart);
            startSecond->setMaximum(recordingLength/1000);
            startMilisecond->setMaximum(recordingLength);
        }

        //beyond 10 ms the lineStep is fixe at 1 ms
        if(timeWindow < 10)
            lineStep = 1;
        else
            lineStep =  static_cast<long>(floor(0.5 + static_cast<float>(static_cast<float>(timeWindow) / static_cast<float>(20))));
        pageStep = timeWindow;

        scrollBar->setMaximum(recordingLength - timeWindow);
        scrollBar->setSingleStep(lineStep);
        scrollBar->setPageStep(pageStep);

        updateView = true;

        //Inform the traceView
        informViewsTimeFrame();
        //Inform the listeners of the modification
        emit updateStartAndDuration(startTime,timeWindow);
    }
}

void TraceWidget::correctStartTime()
{
    //update the selection widgets
    int extraMinutes = timeWindow / 60000;
    int remainingSeconds = static_cast<int>(fmod(static_cast<double>(timeWindow),60000));
    int extraSeconds = remainingSeconds / 1000;
    int extraMiliseconds = static_cast<int>(fmod(static_cast<double>(remainingSeconds),1000));

    int nbMinutes = minutePart - extraMinutes;
    int nbSeconds = secondPart - extraSeconds;
    int nbMiliseconds = milisecondPart - extraMiliseconds;

    if(nbMiliseconds < 0){
        int additionalSeconds = static_cast<int>(abs(nbMiliseconds) / 1000);
        startMilisecond->setMaximum(recordingLength);
        startMilisecond->setValue(1000 - extraMiliseconds + milisecondPart);
        if(additionalSeconds == 0) additionalSeconds = 1;
        nbSeconds -= additionalSeconds;
    }
    else{
        startMilisecond->setMaximum(recordingLength);
        startMilisecond->setValue(nbMiliseconds);
    }

    if(nbSeconds < 0){
        int additionalMinutes = static_cast<int>(abs(nbSeconds) / 60);
        if(additionalMinutes == 0) additionalMinutes = 1;
        nbMinutes -= additionalMinutes;
        if(nbMinutes <= 0){
            startSecond->setMaximum(0);
            startSecond->setValue(0);
        }
        else{
            startSecond->setMaximum(recordingLength/1000);
            startSecond->setValue(59 + nbSeconds + 1);
        }
    }
    else{
        startSecond->setMaximum(recordingLength/1000);
        startSecond->setValue(nbSeconds);
    }

    if (nbMinutes < 0) {
        startMinute->setMaximum(0);
        startMinute->setValue(0);
        startSecond->setMaximum(0);
        startSecond->setValue(0);
        startMilisecond->setMaximum(0);
        startMilisecond->setValue(0);
    } else {
        startMinute->setMaximum(nbMinutes);
        startMinute->setValue(nbMinutes);
    }

    startTime = startMinute->value()* 60000 + startSecond->value() * 1000 + startMilisecond->value();
    scrollBar->setMaximum(recordingLength - timeWindow);
    scrollBar->setValue(startTime);
}

void TraceWidget::slotStartMinuteTimeUpdated(/*int start*/){
    if(!isInit && updateView){
        int start = startMinute->value();
        //Modify updateView to prevent the scrollBar and other spinboxes to trigger a changeEvent while been updated.
        updateView = false;

        long modifiedStartTime = start * 60000 + startSecond->value() * 1000 + startMilisecond->value();

        //Test if we go over the time of the recording if so keep the time window and move back in time
        if((modifiedStartTime + timeWindow) > recordingLength)  {
           correctStartTime();
        }else{
            startTime = modifiedStartTime;
            scrollBar->blockSignals(true);
            scrollBar->setValue(startTime);
            scrollBar->blockSignals(false);

        }

        updateView = true;

        //Inform the traceView
        informViewsTimeFrame();
        //Inform listern of the modification
        emit updateStartAndDuration(startTime,timeWindow);
    }
}

void TraceWidget::slotStartSecondTimeUpdated(){
    if(!isInit && updateView){
        int start = startSecond->value();
        //Modify updateView to prevent the scrollBar and other spinboxes to trigger a changeEvent while been updated.
        updateView = false;

        long modifiedStartTime = startMinute->value() * 60000 + start * 1000 + startMilisecond->value();

        //Test if we go over the time of the recording if so keep the time window and move back in time
        if((modifiedStartTime + timeWindow) > recordingLength) correctStartTime();
        else if(start > 59){
            int remainingSeconds = static_cast<int>(fmod(static_cast<double>(start),60));
            startSecond->setValue(remainingSeconds);
            int additionalMinutes = static_cast<int>(abs(start) / 60);
            if(additionalMinutes == 0) additionalMinutes = 1;
            int nbMinutes = startMinute->value() + additionalMinutes;

            if(nbMinutes > minutePart) correctStartTime();
            else{
                startMinute->setValue(nbMinutes);
                startTime = startMinute->value()* 60000 + startSecond->value() * 1000 + startMilisecond->value();

                scrollBar->blockSignals(true);
                scrollBar->setMaximum(recordingLength - timeWindow);
                scrollBar->setValue(startTime);
                scrollBar->blockSignals(false);
            }
        }
        else{
            startTime = modifiedStartTime;
            //startMinute->setMaximum(minutePart);
            //startSecond->setMaximum(recordingLength/1000);
            //startMilisecond->setMaximum(recordingLength);
            scrollBar->blockSignals(true);
            scrollBar->setValue(startTime);
            scrollBar->blockSignals(false);
        }

        updateView = true;

        //Inform the traceView
        informViewsTimeFrame();
        //Inform listern of the modification
        emit updateStartAndDuration(startTime,timeWindow);
    }
}

void TraceWidget::slotStartMilisecondTimeUpdated(){
    if(!isInit && updateView){
        int start = startMilisecond->value();
        //Modify updateView to prevent the scrollBar to trigger a changeEvent while been updated.
        updateView = false;

        long modifiedStartTime = startMinute->value() * 60000 + startSecond->value() * 1000 + start;
        //Test if we go over the time of the recording if so keep the time window and move back in time
        if((modifiedStartTime + timeWindow) > recordingLength) correctStartTime();
        else if(start > 999){
            int remainingMiliseconds = static_cast<int>(fmod(static_cast<double>(start),1000));
            startMilisecond->setValue(remainingMiliseconds);
            int additionalSeconds = static_cast<int>(abs(start) / 1000);
            if(additionalSeconds == 0) additionalSeconds = 1;
            int nbSeconds = startSecond->value() + additionalSeconds;

            if(nbSeconds > 59){
                int remainingSeconds = static_cast<int>(fmod(static_cast<double>(nbSeconds),60));
                startSecond->setValue(remainingSeconds);
                int additionalMinutes = static_cast<int>(abs(nbSeconds) / 60);
                if(additionalMinutes == 0) additionalMinutes = 1;
                int nbMinutes = startMinute->value() + additionalMinutes;

                if(nbMinutes > minutePart) correctStartTime();
                else{
                    startMinute->setValue(nbMinutes);
                    startTime = startMinute->value()* 60000 + startSecond->value() * 1000 + startMilisecond->value();
                    scrollBar->blockSignals(true);
                    scrollBar->setMaximum(recordingLength - timeWindow);
                    scrollBar->setValue(startTime);
                    scrollBar->blockSignals(false);
                }
            }
            else{
                startSecond->setValue(nbSeconds);
                startTime = startMinute->value()* 60000 + startSecond->value() * 1000 + startMilisecond->value();
                scrollBar->blockSignals(true);
                scrollBar->setMaximum(recordingLength - timeWindow);
                scrollBar->setValue(startTime);
                scrollBar->blockSignals(false);
            }
        }
        else{
            startTime = modifiedStartTime;
            //startMinute->setMaximum(minutePart);
            //startSecond->setMaximum(recordingLength/1000);
            //startMilisecond->setMaximum(recordingLength);
            scrollBar->blockSignals(true);
            scrollBar->setValue(startTime);
            scrollBar->blockSignals(false);
        }
        updateView = true;

        //Inform the traceView
        informViewsTimeFrame();
        //Inform listener of the modification
        emit updateStartAndDuration(startTime,timeWindow);
    }
}

void TraceWidget::slotScrollBarUpdated(){
    if(!isInit && updateView){
        //Modify updateView to prevent the spinboxes to trigger a changeEvent while been updated.
        updateView = false;

        long modifiedStartTime = scrollBar->value();//in miliseconds

        if(modifiedStartTime == startTime){
            updateView = true;
            return;
        }

        //Test if we go over the time of the recording if so keep the time window and move back in time
        if((modifiedStartTime + timeWindow) > recordingLength) correctStartTime();
        else{
            startTime = modifiedStartTime;
            int nbMinutes = startTime / 60000;
            int remainingSeconds = static_cast<int>(fmod(static_cast<double>(startTime),60000));
            int nbSeconds = remainingSeconds / 1000;
            int remainingMiliseconds = static_cast<int>(fmod(static_cast<double>(remainingSeconds),1000));


            startMinute->blockSignals(true);
            startSecond->blockSignals(true);
            startMilisecond->blockSignals(true);
            startMinute->setValue(nbMinutes);
            startSecond->setValue(nbSeconds);
            startMilisecond->setValue(remainingMiliseconds);

            startMinute->blockSignals(false);
            startSecond->blockSignals(false);
            startMilisecond->blockSignals(false);


            //startMinute->setMaximum(minutePart);
            //startSecond->setMaximum(recordingLength/1000);
            //startMilisecond->setMaximum(recordingLength);

        }

        updateView = true;

        //Inform the traceView
        informViewsTimeFrame();
        //Inform listener of the modification
        emit updateStartAndDuration(startTime,timeWindow);
    }
}

void TraceWidget::scrollByQuarterWindow(int direction)
{
    if (timeWindow <= 0)
        return;
    const long step = (timeWindow / 4 > 0) ? timeWindow / 4 : 1;
    long newStart = startTime + static_cast<long>(direction) * step;
    long maxStart = recordingLength - timeWindow;
    if (maxStart < 0) maxStart = 0;
    if (newStart < 0) newStart = 0;
    if (newStart > maxStart) newStart = maxStart;
    if (newStart != startTime)
        moveToTime(newStart);
}

void TraceWidget::moveToTime(long time){
    if(!isInit && updateView){
        //Test if we go over the time of the recording
        if(time > recordingLength)
            return;

        //Modify updateView to prevent the spinboxes to trigger a changeEvent while been updated.
        updateView = false;

        //Test if we go over the time of the recording if so keep the time window and move back in time
        if((time + timeWindow) > recordingLength) correctStartTime();
        else{
            scrollBar->setValue(time);
            startTime = time;
            int nbMinutes = startTime / 60000;
            int remainingSeconds = static_cast<int>(fmod(static_cast<double>(startTime),60000));
            int nbSeconds = remainingSeconds / 1000;
            int remainingMiliseconds = static_cast<int>(fmod(static_cast<double>(remainingSeconds),1000));

            startMinute->blockSignals(true);
            startSecond->blockSignals(true);
            startMilisecond->blockSignals(true);

            startMinute->setValue(nbMinutes);
            startSecond->setValue(nbSeconds);
            startMilisecond->setValue(remainingMiliseconds);
            startMinute->blockSignals(false);
            startSecond->blockSignals(false);
            startMilisecond->blockSignals(false);
        }
        updateView = true;

        //Inform the traceView
        informViewsTimeFrame();
        //Inform listern of the modification
        emit updateStartAndDuration(startTime,timeWindow);
    }
}

void TraceWidget::slotSetStartAndDuration(long time,long duration){
    if(!isInit && updateView){
        //Test if we go over the time of the recoTraceWidget::rding
        if(time > recordingLength) return;

        //Modify updateView to prevent the spinboxes to trigger a changeEvent while been updated.
        updateView = false;

        //First set the duration then the start time

        //Duration
        //Test if the time window is inferior to 1 ms, if so fix set it to the minimum 1.
        if(duration < 1) duration = 1;
        this->duration->setText(QString::number(duration));
        timeWindow = duration;

        //Test if we go over the time of the recording if so keep the time window and move back in time
        if((startTime + timeWindow) > recordingLength) correctStartTime();
        else{
            startMinute->setMaximum(minutePart);
            startSecond->setMaximum(recordingLength/1000);
            startMilisecond->setMaximum(recordingLength);
        }

        //beyond 10 ms the lineStep is fixe at 1 ms
        if(timeWindow < 10)
            lineStep = 1;
        else
            lineStep =  static_cast<long>(floor(0.5 + static_cast<float>(static_cast<float>(timeWindow) / static_cast<float>(20))));
        pageStep = timeWindow;

        scrollBar->setMaximum(recordingLength - timeWindow);
        scrollBar->setSingleStep(lineStep);
        scrollBar->setPageStep(pageStep);

        //Start time
        //Test if we go over the time of the recording if so keep the time window and move back in time
        if((time + timeWindow) > recordingLength)
            correctStartTime();
        else{
            scrollBar->setValue(time);
            startTime = time;
            int nbMinutes = startTime / 60000;
            int remainingSeconds = static_cast<int>(fmod(static_cast<double>(startTime),60000));
            int nbSeconds = remainingSeconds / 1000;
            int remainingMiliseconds = static_cast<int>(fmod(static_cast<double>(remainingSeconds),1000));

            startMinute->setValue(nbMinutes);
            startSecond->setValue(nbSeconds);
            startMilisecond->setValue(remainingMiliseconds);
        }
        updateView = true;

        //Inform the traceView
        informViewsTimeFrame();
        //Inform listern of the modification
        emit updateStartAndDuration(startTime,timeWindow);
    }
}

void TraceWidget::selectChannels(const QList<int>& selectedIds)
{
    view.selectChannels(selectedIds);
}

void TraceWidget::setMode(BaseFrame::Mode selectedMode,bool active)
{
    view.setMode(selectedMode,active);
}

void TraceWidget::setAutocenterChannels(bool status)
{
    view.setAutocenterChannels(status);
}

void TraceWidget::showLabels(bool show)
{
    view.showHideLabels(show);
}

void TraceWidget::slotChannelsSelected(const QList<int>& selectedIds)
{
    emit channelsSelected(selectedIds);
}

void TraceWidget::slotEventAdded(const QString &providerName,const QString& addedEventDescription,double time){
    emit eventAdded(providerName,addedEventDescription,time);
}

void TraceWidget::updateEvents(const QString& providerName,QList<int>& eventsToShow,bool active){
    view.updateEvents(providerName,eventsToShow,active);
}

void TraceWidget::updateEvents(bool active,const QString& providerName,double time){
    long eventTime = static_cast<long>(floor(0.5 + time));
    if((eventTime >= startTime  && eventTime <= (startTime + timeWindow)))
        view.updateEvents(providerName,active);
}

// =============================================================================
//  Spectral view toggle
// =============================================================================

void TraceWidget::informViewsTimeFrame()
{
    // Forward the current window to whichever view is active, so the hidden
    // one does not issue redundant data requests.
    if (spectralMode && spectralView)
        spectralView->displayTimeFrame(startTime, timeWindow);
    else
        view.displayTimeFrame(startTime, timeWindow);
}

void TraceWidget::updateSpectralChannels()
{
    if (spectralView)
        spectralView->setChannels(currentChannels);
}

void TraceWidget::setSpectralMode(bool on)
{
    if (on == spectralMode)
        return;

    if (on) {
        if (!spectralView) {
            spectralView = new SpectralView(mTracesProvider, currentChannels,
                                            startTime, timeWindow, this);

            // Thin frequency-band slider to the left of the spectral view. Its
            // extent is the displayed frequency range; its selection drives the
            // mode-B per-channel integration band (re-integrated, not recomputed).
            freqBandSlider = new FreqBandSlider(this);
            const neuroscope::spectral::SpectralParams& sp = spectralView->spectralParams();
            const double fLo = sp.freqLow;
            const double fHi = (sp.freqHigh > 0.0) ? sp.freqHigh
                                                   : (sp.samplingRate > 1.0 ? sp.samplingRate / 2.0 : 300.0);
            freqBandSlider->setRange(fLo, fHi);
            freqBandSlider->setBand(fLo, fHi);

            spectralRow = new QWidget(this);
            QHBoxLayout* rowLay = new QHBoxLayout(spectralRow);
            rowLay->setContentsMargins(0, 0, 0, 0);
            rowLay->setSpacing(0);
            rowLay->addWidget(freqBandSlider);
            rowLay->addWidget(spectralView, 1);

            // Same layout slot as the traces, above the time-selection controls.
            mainLayout->insertWidget(0, spectralRow);
            mainLayout->setStretchFactor(spectralRow, 200);

            connect(freqBandSlider, &FreqBandSlider::bandChanged,
                    spectralView, &SpectralView::setBand);
            connect(spectralView, &SpectralView::frequencyRangeChanged,
                    freqBandSlider, &FreqBandSlider::setRange);
            connect(spectralView, &SpectralView::spectralModeChanged, this,
                    [this](neuroscope::spectral::SpectralMode m) {
                        if (freqBandSlider)
                            freqBandSlider->setVisible(
                                m == neuroscope::spectral::SpectralMode::FrequencyAcrossChannels);
                    });
            // Only meaningful in channel mode; hidden until then.
            freqBandSlider->setVisible(
                spectralView->spectralParams().mode
                == neuroscope::spectral::SpectralMode::FrequencyAcrossChannels);
        }
        view.hide();
        spectralRow->show();
        spectralMode = true;
        spectralView->setChannels(currentChannels);
        spectralView->displayTimeFrame(startTime, timeWindow);
    } else {
        if (spectralRow)
            spectralRow->hide();
        view.show();
        spectralMode = false;
        view.displayTimeFrame(startTime, timeWindow);
    }
}
