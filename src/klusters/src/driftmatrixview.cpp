/***************************************************************************
 * driftmatrixview.cpp — see driftmatrixview.h.
 *
 * Structure mirrors ResidualMatrixView (read-only matrix, double-buffered
 * paint, pan/zoom, stale marker); the drift slider and the geometry lookup are
 * what is new.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "driftmatrixview.h"
#include <QDebug>
#include <QStringList>
#include "driftshiftthread.h"
#include "configuration.h"

#include <QShowEvent>
#include "matrixgrid.h"
#include "matrixbadge.h"
#include "driftmatrixthread.h"
#include "driftmatrixkernel.h"
#include "matrixtemplatecols.h"  // drawMatrixTemplateStrip — the shared edge-strip renderer
#include "driftgeometry.h"
#include "klustersdoc.h"
#include "klustersview.h"

#include <QApplication>
#include <QThread>        // msleep for the synchronous job quiesce
#include <QMutexLocker>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QImage>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSlider>
#include <QSpinBox>
#include <cmath>
#include <algorithm>

static constexpr int CELL_WIDTH   = 50;
static constexpr int LABEL_MARGIN = 16;
static constexpr int CTRL_H       = 30;   // drift slider bar (top)
static constexpr int INFO_H       = 24;   // hover readout (bottom)
static constexpr int DEFAULT_MAX_UM = 100;

// ── ctor / dtor ──────────────────────────────────────────────────────────────

DriftMatrixView::DriftMatrixView(KlustersDoc& doc_, KlustersView& view_,
                                 const QColor& backgroundColor,
                                 QStatusBar* statusBar_,
                                 QWidget* parent)
    : QWidget(parent),
      doc(doc_), view(view_), statusBar(statusBar_),
      scores(nullptr),
      dataReady(false), goingToDie(false), isStale(false),
      shiftToken(std::make_shared<KlustersJobToken>()),
      computeToken(std::make_shared<KlustersJobToken>()),
      cellWidth(CELL_WIDTH), widthBorder(0), heightBorder(0)
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, backgroundColor);
    setPalette(pal);
    setAutoFillBackground(true);

    const double lum = 0.299 * backgroundColor.redF()
                     + 0.587 * backgroundColor.greenF()
                     + 0.114 * backgroundColor.blueF();
    textColor = (lum > 0.5) ? QColor(Qt::black) : QColor(Qt::white);

    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->setSpacing(0);

    // ── control bar: drift slider + readout + editable range ─────────────
    QWidget*     bar    = new QWidget(this);
    QHBoxLayout* barLay = new QHBoxLayout(bar);
    bar->setFixedHeight(CTRL_H);
    barLay->setContentsMargins(8,0,8,0);
    barLay->setSpacing(6);

    driftLabel = new QLabel(bar);
    driftLabel->setMinimumWidth(110);
    driftLabel->setText(tr("Drift: ±0 µm"));

    driftSlider = new QSlider(Qt::Horizontal, bar);
    driftSlider->setRange(0, DEFAULT_MAX_UM);
    driftSlider->setValue(0);
    driftSlider->setTickPosition(QSlider::TicksBelow);
    driftSlider->setTickInterval(10);
    driftSlider->setToolTip(tr("Shift the row unit's mean waveform along the probe "
                               "depth axis: +Δ in the upper triangle, −Δ in the lower."));

    maxUmSpin = new QSpinBox(bar);
    maxUmSpin->setRange(10, 1000);
    maxUmSpin->setValue(DEFAULT_MAX_UM);
    maxUmSpin->setSuffix(tr(" µm max"));
    maxUmSpin->setToolTip(tr("Upper end of the drift slider range."));

    barLay->addWidget(driftLabel, 0);
    barLay->addWidget(driftSlider, 1);
    barLay->addWidget(maxUmSpin, 0);
    mainLayout->addWidget(bar, 0);

    mainLayout->addStretch(1);

    infoLabel = new QLabel(this);
    infoLabel->setFixedHeight(INFO_H);
    infoLabel->setContentsMargins(8,0,8,0);
    // Ignored horizontally: a plain QLabel reports its entire (non-wrapping)
    // text width as its minimum, which would floor the whole matrix dock frame
    // at ~1000 px and stop the user narrowing the pane.
    infoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setInfoText(tr("Drift matrix — cell(row A, col B) = xcorr of A's mean shifted "
                          "along depth (+Δ above the diagonal, −Δ below) against B's mean; "
                          "red ≈ 1 (same shape at that drift, merge candidate), blue ≈ 0."));
    mainLayout->addWidget(infoLabel, 0);

    connect(driftSlider, &QSlider::valueChanged,
            this, &DriftMatrixView::driftSliderChanged);
    connect(maxUmSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &DriftMatrixView::driftRangeChanged);

    initializeColorMap();
    updateMatrixContents();
}

DriftMatrixView::~DriftMatrixView()
{
    willBeKilled();
    //Fence the completion posts on both request streams: once viewDead is set
    //under the locks, no job will post to this view again.  The jobs
    //themselves are not waited for: they were superseded by willBeKilled(),
    //the pool owns and deletes them, and their results travel inside the
    //events, which delete what nobody takes.
    {
        QMutexLocker lock(&computeToken->postMutex);
        computeToken->viewDead = true;
    }
    {
        QMutexLocker lock(&shiftToken->postMutex);
        shiftToken->viewDead = true;
    }
    delete cacheAll.scores;
    delete cacheSel.scores;   // `scores` is non-owning: it aliases one of these
    QApplication::removePostedEvents(this);
}

void DriftMatrixView::willBeKilled()
{
    if (!goingToDie) {
        goingToDie = true;
        //Supersede both request streams: each in-flight job stops at its next
        //cancellation check, and its completion event fails the generation
        //guard in customEvent().
        computeToken->generation.fetch_add(1, std::memory_order_acq_rel);
        shiftToken->generation.fetch_add(1, std::memory_order_acq_rel);
    }
}

bool DriftMatrixView::isThreadsRunning() const
{
    return computeToken->active.load(std::memory_order_acquire) > 0;
}

void DriftMatrixView::stopRunningThreadsSync()
{
    // Supersede both request streams, then wait for every job to retire:
    // callers are about to rewrite .spk.pending, so no full-recompute job may
    // still be inside a file read once this returns (the shift jobs only read
    // their own copied means, but they are quiesced too, as before).
    stopShiftThreads();
    computeToken->generation.fetch_add(1, std::memory_order_acq_rel);
    while (computeToken->active.load(std::memory_order_acquire) > 0)
        QThread::msleep(1);
    // Drop completion events the superseded jobs posted before retiring;
    // they would fail the generation guard anyway.
    QApplication::removePostedEvents(this, QEvent::User + 604);
    QApplication::removePostedEvents(this, QEvent::User + 606);
}

void DriftMatrixView::supersedeRunningThreads()
{
    //Non-blocking twin of stopRunningThreadsSync() (epoch-snapshot
    //step 6b) — see TemplateMatrixView::supersedeRunningThreads().
    shiftToken->generation.fetch_add(1, std::memory_order_acq_rel);
    computeToken->generation.fetch_add(1, std::memory_order_acq_rel);
    QApplication::removePostedEvents(this, QEvent::User + 604);
    QApplication::removePostedEvents(this, QEvent::User + 606);
}

// ── compute ──────────────────────────────────────────────────────────────────

void DriftMatrixView::launchComputeThread()
{
    // Resolve per-channel probe depths from the session YAML `probes:` section
    // + the referenced .probe geometry.  An empty result disables the slider;
    // the worker then falls back to a plain unshifted mean xcorr.
    //
    // NB: this needs parameterFileUrl(), the session .yaml — NOT url(), which is
    // the .clu the document was opened from.  Feeding a .clu to the YAML parser
    // yields a scalar, no probes section, and a silently dead drift slider.
    QString err;
    const QString yamlPath = doc.parameterFileUrl();
    if (yamlPath.isEmpty())
        err = tr("session has no YAML parameter file");

    std::vector<float> chanDepths;
    if (err.isEmpty())
        chanDepths = loadGroupChannelDepths(yamlPath, doc.data().getCurrentChannels(), &err);

    geometryError = chanDepths.empty() ? err : QString();

    if (chanDepths.empty() && !err.isEmpty() && statusBar)
        statusBar->showMessage(tr("Drift matrix: no probe geometry (%1) — "
                                  "showing unshifted correlations.").arg(err), 5000);

    // Scoped matrices: restrict to one parent's children when the child palette is
    // driving and the parent has enough of them to be worth comparing.  Empty
    // otherwise, which is the unrestricted behaviour.
    if (qEnvironmentVariableIsSet("NS3_VERBOSE")) {
        const QList<dataType> ids = doc.matrixData().clusterIds();
        const QList<int> scope = doc.matrixScopeClusters();
        QStringList head; for (int i = 0; i < ids.size() && i < 6; ++i) head << QString::number(ids[i]);
        QStringList sh;   for (int i = 0; i < scope.size() && i < 6; ++i) sh << QString::number(scope[i]);
        int overlap = 0; for (int s : scope) if (ids.contains(static_cast<dataType>(s))) ++overlap;
        qDebug().noquote()
            << "[matrixscope] DriftMatrixView"
            << " childScopeActive=" << doc.isChildClusteringActive()
            << " scopeActive="      << doc.matrixScopeActive()
            << " scopeParent="      << doc.curatedParent()
            << " | data() clusters=" << ids.size() << "first=[" << head.join(',') << "]"
            << " | scope n=" << scope.size() << "first=[" << sh.join(',') << "]"
            << " | OVERLAP=" << overlap
            << (scope.isEmpty() ? "" : (overlap == 0 ? "   <-- DISJOINT: wrong id space"
                                                     : (overlap < scope.size() ? "   <-- PARTIAL" : "")));
    }
    //Creating the job launches it; the pool owns and deletes it.
    new DriftMatrixThread(*this, doc.matrixData(), std::move(chanDepths),
                          static_cast<float>(currentDriftUm), computeToken,
                          doc.selectedChannels(),
                          doc.matrixScopeClusters());
}

void DriftMatrixView::launchCompute()
{
    if (goingToDie) return;
    //Supersede the in-flight computes (their results are stale by definition
    //now); the replacement job enqueued below captures the new generation.
    computeToken->generation.fetch_add(1, std::memory_order_acq_rel);
    computing = true;      // paint a badge over the old matrix, do not blank

    setCursor(Qt::WaitCursor);
    launchComputeThread();

    isStale = false;
    update();
}

void DriftMatrixView::updateMatrixContents()
{
    // Swap zoom/pan when the scope changes, before anything is drawn: the parent
    // matrix and the child-scoped one are different matrices and a position in
    // one does not mean anything in the other.
    swapViewStateForScope(doc.matrixScopeActive());

    if (goingToDie) return;

    // The spikes themselves changed, so BOTH cached matrices are stale — drop
    // them rather than let a later selection toggle swap in an outdated one.
    // Only this path invalidates: a mere selection change must keep the other
    // slot, or swapping back would cost a recompute.
    invalidateCaches();

    // Do not compute a matrix nobody is looking at.  KlustersApp emits
    // computeDriftMatrix() on EVERY edit, and the matrix docks are tabified, so
    // a drift matrix sitting behind the error matrix tab -- or on a display the
    // user has switched away from -- was doing a full O(clusters^2) pass per
    // edit for a widget with no pixels on screen.  isVisible() is false for a
    // dock that is closed, minimised, on a hidden display, or tabbed behind
    // another; showEvent() below picks the work up again the moment it is
    // actually shown, so nothing is silently left out of date.
    if (!isVisible()) {
        isStale = true;
        return;
    }

    launchCompute();
}

void DriftMatrixView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Revealed (tab selected, dock reopened, display switched back).  Recompute
    // only if an edit happened while it was hidden -- otherwise selecting the
    // tab would relaunch a matrix that is already current.
    if (!goingToDie && isStale)
        launchCompute();
}

void DriftMatrixView::customEvent(QEvent* event)
{
    // ── slider recompute (User+606) ─────────────────────────────────────────
    if (event->type() == QEvent::Type(QEvent::User + 606)) {
        auto* sev = static_cast<DriftShiftThread::DriftShiftEvent*>(event);

        //The job retired itself and the result travels in the event.
        const bool genMatch = (sev->generation()
                               == shiftToken->generation.load(std::memory_order_acquire));
        Array<double>* fresh = sev->takeScores();
        // Only the newest drag position may paint; every earlier one was
        // superseded by it and yields a null matrix anyway.
        const bool ok = (fresh != nullptr && genMatch);
        if (genMatch) computing = false;

        if (ok) {
            // Install into the cache slot this run was computed for, so the
            // matrix being painted and the cached one never diverge -- the old
            // in-place write kept them identical by construction and the swap
            // has to preserve that.
            Cache& slot = sev->wasForSelection() ? cacheSel : cacheAll;
            delete slot.scores;
            slot.scores = fresh;
            slot.valid  = true;
            scores      = fresh;
        } else {
            delete fresh;
        }

        if (!goingToDie) {
            if (ok) { recomputeTemplateStripCells();   // same drift the body just moved to
                      updateWindow(); setCursor(Qt::ArrowCursor); }
            update();
        }
        return;
    }

    if (event->type() != QEvent::Type(QEvent::User + 604)) return;

    auto* ev = static_cast<DriftMatrixThread::DriftMatrixEvent*>(event);

    //The job retired itself and the results travel in the event, so there is
    //no thread to wait for or delete anymore.
    const bool genMatch = (ev->generation()
                           == computeToken->generation.load(std::memory_order_acquire));
    Array<double>* newScores = ev->takeScores();
    const bool accepted = (newScores != nullptr && genMatch);
    // Only the generation we are waiting on ends the "computing" state: a
    // superseded result arriving must not cancel the badge for the newer
    // compute that replaced it.
    if (genMatch) computing = false;

    if (accepted) {
        // File the result under the selection it was computed for, together with
        // the means it came from, so the other slot stays usable for an instant
        // swap later.
        const QList<int> ranFor = ev->getSelection();
        Cache& slot = ranFor.isEmpty() ? cacheAll : cacheSel;
        delete slot.scores;
        slot.scores     = newScores;
        slot.meanWav    = std::move(ev->getMeanWav());
        slot.depths     = std::move(ev->getDepths());
        slot.nChan      = ev->getNbChannels();
        slot.geometryOk = ev->geometryOk();
        slot.valid      = true;
        if (!ranFor.isEmpty()) cachedSelection = ranFor;

        clusterList    = ev->getClusterList();
        nSampCached    = ev->getNbSamples();
        maxShiftCached = ev->getMaxShift();
        activateCache(slot);
    } else {
        delete newScores;
    }

    if (!goingToDie) {
        if (accepted) {
            // Without geometry the shift is meaningless: keep the (unshifted)
            // matrix visible but say why the slider is dead rather than leaving
            // the user to guess.
            refreshSliderEnabled();
            if (!geometryOk) {
                driftLabel->setText(tr("Drift: n/a"));
                const QString why = geometryError.isEmpty()
                                        ? tr("no probe geometry for this group")
                                        : geometryError;
                driftSlider->setToolTip(tr("Drift shifting needs probe geometry: %1.").arg(why));
                setInfoText(tr("No probe geometry (%1) — showing unshifted "
                               "correlations; the drift slider is disabled.").arg(why));
            }

            dataReady = true;
            recomputeTemplateStripCells();   // fresh means/clusters: refresh the strip
            updateWindow();
            setCursor(Qt::ArrowCursor);
        } else if (computeToken->active.load(std::memory_order_acquire) == 0) {
            setCursor(Qt::ArrowCursor);
        }
        update();
        if (accepted)
            emit matrixUpdated();
    }
}

void DriftMatrixView::recomputeAtCurrentDrift()
{
    if (goingToDie) return;
    if (!dataReady || !geometryOk || scores == nullptr) return;
    if (meanWav.empty() || static_cast<int>(meanWav.size()) != clusterList.size())
        return;
    // Belt and braces: the slider is disabled above the cap, but activateCache()
    // and the range spin box can also reach here.
    if (static_cast<int>(meanWav.size()) > driftSliderClusterCap()) return;

    // Off-thread, into a NEW matrix.  This used to run inline and write straight
    // into *scores -- safe only because it held the GUI thread throughout.  A
    // drag issues one of these per valueChanged, so each new position
    // supersedes the one before it and only the last to survive is painted.
    shiftToken->generation.fetch_add(1, std::memory_order_acq_rel);

    computing = true;                       // badge over the current matrix
    new DriftShiftThread(
        *this, shiftToken, meanWav, depths,
        nChanCached, nSampCached, maxShiftCached, currentDriftUm,
        !cachedSelection.isEmpty());
    update();
}

void DriftMatrixView::stopShiftThreads()
{
    // Supersede every in-flight slider job and wait for them to retire.
    shiftToken->generation.fetch_add(1, std::memory_order_acq_rel);
    while (shiftToken->active.load(std::memory_order_acquire) > 0)
        QThread::msleep(1);
}

int DriftMatrixView::driftSliderClusterCap() const
{
    return configuration().getDriftSliderMaxClusters();
}

void DriftMatrixView::refreshSliderEnabled()
{
    const int nCl = static_cast<int>(meanWav.size());
    const int cap = driftSliderClusterCap();
    sliderCappedByClusterCount = (nCl > cap);
    const bool on = geometryOk && !sliderCappedByClusterCount;
    driftSlider->setEnabled(on);
    maxUmSpin->setEnabled(on);

    if (sliderCappedByClusterCount) {
        driftLabel->setText(tr("Drift: n/a"));
        driftSlider->setToolTip(
            tr("The drift slider is disabled above %1 clusters (this group has %2).\n"
               "Each step recomputes every cluster pair, and drift linking is only\n"
               "meaningful once the sort has been consolidated. Merge down, or raise\n"
               "the limit in Preferences > Display > Drift Matrix, and it re-enables\n"
               "itself.")
                .arg(cap).arg(nCl));
    }
}

void DriftMatrixView::activateCache(const Cache& c)
{
    // Point at a cached result and restore the state the drift slider rebuilds
    // from, so a swap needs no .spk read and the slider keeps working on the
    // means this matrix was actually computed from.
    scores      = c.scores;
    meanWav     = c.meanWav;
    depths      = c.depths;
    nChanCached = c.nChan;
    geometryOk  = c.geometryOk;
    refreshSliderEnabled();
    dataReady = true;
    updateWindow();
    setCursor(Qt::ArrowCursor);
    update();
}

void DriftMatrixView::invalidateCaches()
{
    // Mark both slots out of date without freeing them, and leave `scores` and
    // dataReady alone: the matrix already on screen keeps being painted while
    // the replacement computes, and its memory is released when the new result
    // is filed into its slot.  Only `valid` gates the instant-swap path, so a
    // stale matrix is displayed but never swapped in as current.
    cacheAll.valid = false;
    cacheSel.valid = false;
    cachedSelection.clear();
}

void DriftMatrixView::selectedChannelsChanged(const QList<int>& channels)
{
    if (goingToDie) return;

    // Swap in a cached result when we already have one for this selection; only
    // a selection we have not computed costs a recompute.  That is the whole
    // point of keeping two: flicking the selection on and off is free.
    if (channels.isEmpty()) {
        if (cacheAll.valid) { activateCache(cacheAll); return; }
    } else if (cacheSel.valid && channels == cachedSelection) {
        activateCache(cacheSel);
        return;
    }
    // Not cached: compute it, but keep the other slot intact.
    launchCompute();
}

void DriftMatrixView::driftSliderChanged(int um)
{
    currentDriftUm = um;
    driftLabel->setText(tr("Drift: ±%1 µm").arg(um));
    recomputeAtCurrentDrift();
    emit viewInteracted();
}

void DriftMatrixView::driftRangeChanged(int maxUm)
{
    driftSlider->setRange(0, maxUm);
    driftSlider->setTickInterval(std::max(1, maxUm / 10));
}

// ── stale markers ────────────────────────────────────────────────────────────

void DriftMatrixView::clustersGrouped(QList<int>&, int)                      { isStale=true; update(); }
void DriftMatrixView::clustersDeleted(QList<int>&, int)                      { isStale=true; update(); }
void DriftMatrixView::removeSpikesFromClusters(QList<int>&,int,QList<int>&)  { isStale=true; update(); }
void DriftMatrixView::newClusterAdded(QList<int>&,int,QList<int>&)           { isStale=true; update(); }
void DriftMatrixView::newClustersAdded(QMap<int,int>&,QList<int>&)           { isStale=true; update(); }
void DriftMatrixView::newClustersAdded(QList<int>&)                          { isStale=true; update(); }
void DriftMatrixView::renumber(QMap<int,int>&)                               { isStale=true; update(); }
void DriftMatrixView::markStale()                                            { isStale=true; update(); }

// ── colour / layout ──────────────────────────────────────────────────────────

void DriftMatrixView::initializeColorMap()
{
    // Same ramp as the residual matrix: index 0 = blue, NB_COLORS-1 = red.
    for (int i = 0; i < NB_COLORS; ++i) {
        const int hue = static_cast<int>(240.0 * (1.0 - static_cast<double>(i)
                                                        / (NB_COLORS - 1)));
        QColor c;
        c.setHsv(hue, 220, 255);
        colorMap.insert(i, c);
    }
}

void DriftMatrixView::updateWindow()
{
    const int n = clusterList.size();
    if (n <= 0) return;
    // Reserve room for the marked-node template strip (gap + M cells) so it fits
    // on screen instead of running off the right/bottom edge (as the others do).
    const int nStrip = tplCols_.empty() ? 0 : (kTemplateStripGapCells + static_cast<int>(tplCols_.size()));
    const int nTot   = n + nStrip;
    const int matH   = std::max(height() - CTRL_H - INFO_H, 1);
    const int availW = width() - LABEL_MARGIN - 10;
    const int availH = matH - 14 - 10;
    const int fitW   = (availW > 0) ? availW / nTot : CELL_WIDTH;
    const int fitH   = (availH > 0) ? availH / nTot : CELL_WIDTH;
    cellWidth        = std::max(4, std::min({fitW, fitH, CELL_WIDTH}));
    widthBorder      = cellWidth / 3 + 5;
    heightBorder     = cellWidth / 3 + 14;
}

QPoint DriftMatrixView::matrixTopLeft() const
{
    return QPoint(LABEL_MARGIN + widthBorder, CTRL_H + heightBorder);
}

QSize DriftMatrixView::sizeHint() const
{
    return QSize(400, 400);
}

void DriftMatrixView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    updateInfoElide();
    if (dataReady) updateWindow();
    update();
}

// ── painting ─────────────────────────────────────────────────────────────────

void DriftMatrixView::paintEvent(QPaintEvent*)
{
    if (doublebuffer.size() != size())
        doublebuffer = QPixmap(size());
    doublebuffer.fill(palette().color(QPalette::Window));

    QPainter buf(&doublebuffer);
    if (dataReady) {
        drawMatrix(buf);
        drawClusterIds(buf);
    }
    // A recompute no longer wipes the frame: whatever was there stays up and a
    // small badge says fresh numbers are coming.  With nothing to show yet (the
    // first compute) the badge centres itself instead.  Confined to the matrix
    // area, between the control bar and the info line.
    if (computing)
        mbDrawComputingBadge(buf, QRect(0, CTRL_H, width(),
                                        std::max(height() - CTRL_H - INFO_H, 1)),
                             tr("Computing drift matrix\u2026"), dataReady);
    buf.end();

    QPainter p(this);
    p.drawPixmap(0, 0, doublebuffer);
}

void DriftMatrixView::drawMatrix(QPainter& p)
{
    const int     n    = clusterList.size();
    const QPointF oriF = effMatrixTopLeft();
    const double  eff  = effCellSize();

    if (isStale) {
        QPen pen(Qt::red); pen.setWidth(2);
        p.setPen(pen);
        p.drawRect(QRectF(oriF.x()-1, oriF.y()-1, n * eff + 2, n * eff + 2));
        p.setPen(Qt::NoPen);
    }

    if (n > 0 && scores) {
        QImage img(n, n, QImage::Format_Indexed8);
        QList<QRgb> table;
        table.reserve(NB_COLORS + 1);
        for (int i = 0; i < NB_COLORS; ++i) table.append(colorMap[i].rgb());
        const int blackIdx = NB_COLORS;                 // diagonal
        table.append(qRgb(0, 0, 0));
        img.setColorTable(table);

        for (int row = 0; row < n; ++row) {
            uchar* line = img.scanLine(row);
            for (int col = 0; col < n; ++col) {
                if (row == col) { line[col] = static_cast<uchar>(blackIdx); continue; }
                // xcorr is already bounded [0,1]; high (red) = similar.
                double r = (*scores)(row+1, col+1);
                r = std::max(0.0, std::min(1.0, r));
                int idx = static_cast<int>(r * (NB_COLORS-1) + 0.5);
                idx = std::max(0, std::min(NB_COLORS-1, idx));
                line[col] = static_cast<uchar>(idx);
            }
        }
        p.drawImage(QRectF(oriF.x(), oriF.y(), n * eff, n * eff), img,
                    QRectF(0, 0, n, n));
        // One-pixel dashed grid so adjacent cells read as separate elements.
        // Full-span lines only; boxing each cell would draw every interior edge
        // twice for the same picture.
        drawMatrixGrid(p, oriF, eff, n);

        // Parent identity bands under a JOINT scope; see templatematrixview.cpp.
        const QList<int> scopeParents = doc.matrixScopeParents();
        if (scopeParents.size() >= 2) {
            ItemColors& parentColours = doc.parentClusterColors();
            QList<int> parentPerCell;      parentPerCell.reserve(n);
            QList<QColor> colourPerCell;   colourPerCell.reserve(n);
            for (int d = 0; d < n; ++d) {
                const int par = doc.parentOfChild(clusterList[d]);
                parentPerCell.append(par);
                colourPerCell.append(par >= 0 ? parentColours.color(par) : QColor());
            }
            drawMatrixParentBands(p, oriF, eff, parentPerCell, colourPerCell);
        }
    }

    // Marked-node template columns/rows at the edge (drift-shifted xcorr, §11.5).
    drawTemplateStrip(p);
}

void DriftMatrixView::drawClusterIds(QPainter& p)
{
    const int     n    = clusterList.size();
    const QPoint  base = matrixTopLeft();
    const QPointF oriF = effMatrixTopLeft();
    const double  eff  = effCellSize();
    const int     w    = std::max(1, static_cast<int>(std::round(eff)));
    const int fontSize = std::max(5, std::min(14,
                            static_cast<int>(std::round(eff / 5.0))));
    QFont f("Helvetica", fontSize);
    p.setFont(f);
    p.setPen(textColor);

    for (int col = 0; col < n; ++col) {
        const int px = static_cast<int>(std::round(oriF.x() + col * eff));
        p.drawText(QRect(px, CTRL_H, w, base.y() - CTRL_H),
                   Qt::AlignHCenter | Qt::AlignBottom,
                   QString::number(clusterList[col]));
    }
    for (int row = 0; row < n; ++row) {
        const int py = static_cast<int>(std::round(oriF.y() + row * eff));
        p.drawText(QRect(0, py, LABEL_MARGIN-2, w),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(clusterList[row]));
    }
}


// ── marked-node template columns (§11.5) ──────────────────────────────────────
void DriftMatrixView::setTemplateColumns(const std::vector<MatrixTemplateCol>& cols)
{
    tplCols_ = cols;
    recomputeTemplateStripCells();
    if (dataReady) updateWindow();   // refit so the strip (gap + M cells) is on screen
    update();
}

void DriftMatrixView::recomputeTemplateStripCells()
{
    const int M = static_cast<int>(tplCols_.size());
    const int N = clusterList.size();
    tplClusterRow_.assign(M, std::vector<double>(N, 0.0));
    tplTemplateRow_.assign(M, std::vector<double>(N, 0.0));
    tplShade_.assign(M, std::vector<unsigned char>(N, MatrixStripGrey));
    tplCorner_.assign(M, std::vector<double>(M, 0.0));
    if (M == 0 || N == 0 || static_cast<int>(meanWav.size()) < N) return;

    const int   nChan    = nChanCached, nSamp = nSampCached;
    const int   maxShift = std::max(1, maxShiftCached);
    const float delta    = static_cast<float>(currentDriftUm);
    // Without probe geometry the shift is meaningless (the body shows unshifted
    // correlations and the slider is disabled); mirror that for the strip.
    const bool  canShift = geometryOk && delta != 0.0f
                           && static_cast<int>(depths.size()) == nChan;

    // Per-cluster spike-time RANGE [lo,hi] in samples (two feature reads: the
    // cluster's spike block is time-ordered) — for the in-window greying.
    const int timeDim = doc.data().timeDimension();
    std::vector<double> cLo(N, 0.0), cHi(N, -1.0);              // cHi < cLo = "no spikes"
    for (int j = 0; j < N; ++j) {
        const auto idx = doc.data().clusterSpkIndices(clusterList[j]);
        if (idx.isEmpty()) continue;
        const double t0 = static_cast<double>(doc.data().featureValue(idx.first() + 1, timeDim));
        const double t1 = static_cast<double>(doc.data().featureValue(idx.last()  + 1, timeDim));
        cLo[j] = std::min(t0, t1); cHi[j] = std::max(t0, t1);
    }
    const double sr = doc.getSamplingRate();

    std::vector<float> sh;                                     // per-template scratch
    for (int t = 0; t < M; ++t) {
        const double winLo = tplCols_[t].a * sr, winHi = tplCols_[t].b * sr;
        const std::vector<float>& tpl = tplCols_[t].mean;
        const bool tplOk = (static_cast<int>(tpl.size()) == nChan * nSamp);
        std::vector<float> tplMinus;                           // template row shifted −Δ
        if (tplOk && canShift) dmDriftShift(tpl, nChan, nSamp, depths, -delta, tplMinus);

        for (int j = 0; j < N; ++j) {
            const std::vector<float>& cl = meanWav[static_cast<size_t>(j)];
            const bool sizeOk = tplOk && (cl.size() == tpl.size());
            if (sizeOk) {
                // cluster-row cell: shift the cluster +Δ (upper triangle), xcorr with template.
                if (canShift) { dmDriftShift(cl, nChan, nSamp, depths, +delta, sh);
                                tplClusterRow_[t][j] = dmNormXcorr(sh, tpl, maxShift); }
                else            tplClusterRow_[t][j] = dmNormXcorr(cl, tpl, maxShift);
                // template-row cell: shift the template −Δ (lower triangle), xcorr with cluster.
                tplTemplateRow_[t][j] = canShift ? dmNormXcorr(tplMinus, cl, maxShift)
                                                 : dmNormXcorr(tpl, cl, maxShift);
            }
            const bool noSpk = (cHi[j] < cLo[j]) || (cHi[j] < winLo) || (cLo[j] > winHi);
            // No data -> solid grey; data but no time overlap -> dim the (still-shown) value.
            tplShade_[t][j] = !sizeOk ? MatrixStripGrey : (noSpk ? MatrixStripDim : MatrixStripValue);
        }

        for (int u = 0; u < M; ++u) {
            const std::vector<float>& tu = tplCols_[u].mean;
            if (!tplOk || tu.size() != tpl.size()) { tplCorner_[t][u] = 0.0; continue; }
            if (t == u)                            { tplCorner_[t][u] = 1.0; continue; }  // diagonal
            if (canShift) { dmDriftShift(tpl, nChan, nSamp, depths, (t < u ? +delta : -delta), sh);
                            tplCorner_[t][u] = dmNormXcorr(sh, tu, maxShift); }
            else            tplCorner_[t][u] = dmNormXcorr(tpl, tu, maxShift);
        }
    }
}

void DriftMatrixView::drawTemplateStrip(QPainter& p)
{
    const int M = static_cast<int>(tplCols_.size());
    const int N = clusterList.size();
    if (M == 0 || N == 0 || static_cast<int>(tplClusterRow_.size()) != M) return;
    const QPointF oriF = effMatrixTopLeft();
    const double  eff  = effCellSize();

    // ASYMMETRIC: the row is the shifted side.  A cluster-row (r<N) against a
    // template-col (c>=N) always has r<c -> +Δ on the cluster; a template-row
    // (r>=N) against a cluster-col (c<N) always has r>c -> −Δ on the template.
    auto value = [&](int r, int c, MatrixStripShade& shade)->double{
        if (r < N && c >= N) { const int t = c - N; shade = static_cast<MatrixStripShade>(tplShade_[t][r]); return tplClusterRow_[t][r]; }
        if (r >= N && c < N) { const int t = r - N; shade = static_cast<MatrixStripShade>(tplShade_[t][c]); return tplTemplateRow_[t][c]; }
        shade = MatrixStripValue; return tplCorner_[r - N][c - N];     // template×template corner
    };
    auto colourFor = [&](double v)->QColor{                // same ramp as drawMatrix
        double r = std::max(0.0, std::min(1.0, v));
        int idx = static_cast<int>(r * (NB_COLORS - 1) + 0.5);
        idx = std::max(0, std::min(NB_COLORS - 1, idx));
        return colorMap[idx];
    };
    drawMatrixTemplateStrip(p, oriF, eff, N, M, value, colourFor);
}

void DriftMatrixView::setInfoText(const QString& text)
{
    infoText = text;
    updateInfoElide();
}

void DriftMatrixView::updateInfoElide()
{
    // The label is QSizePolicy::Ignored horizontally so it never contributes to
    // the widget's (and hence the dock's, and hence the tabbed matrix frame's)
    // minimum width.  That means it can be handed less room than its text needs,
    // so elide rather than letting Qt clip mid-word.
    // Use the view's width, not infoLabel->width(): Qt resizes children only
    // after the parent's resizeEvent, so the label's own width lags by one
    // event.  The label spans the full width (the main layout has no margins),
    // so they are the same value once settled.
    const QFontMetrics fm(infoLabel->font());
    const int avail = width() - infoLabel->contentsMargins().left()
                              - infoLabel->contentsMargins().right();
    infoLabel->setText(avail > 0 ? fm.elidedText(infoText, Qt::ElideRight, avail)
                                 : QString());
    infoLabel->setToolTip(infoText);
}

// ── pan / zoom ───────────────────────────────────────────────────────────────

void DriftMatrixView::zoomAroundPoint(double newZoom, const QPointF& pivot)
{
    newZoom = std::max(zoomMin, std::min(zoomMax, newZoom));
    const QPointF base = QPointF(matrixTopLeft());
    const double oldEff = cellWidth * zoom;
    const double newEff = cellWidth * newZoom;
    if (oldEff > 0.0) {
        const double mx = (pivot.x() - base.x() - panX) / oldEff;
        const double my = (pivot.y() - base.y() - panY) / oldEff;
        panX = pivot.x() - base.x() - mx * newEff;
        panY = pivot.y() - base.y() - my * newEff;
    }
    zoom = newZoom;
    update();
    emit viewChanged(zoom, panX, panY);
}

void DriftMatrixView::resetPanZoom()
{
    panX = panY = 0.0;
    zoom = 1.0;
    update();
    emit viewChanged(zoom, panX, panY);
}

// ---------------------------------------------------------------------------
// DriftMatrixView::swapViewStateForScope
//
// Stash the current zoom/pan under the scope we are leaving and restore the one
// we are entering.  Called from the paint path, which is the one place that runs
// for every scope change however it was reached -- the V toggle, a curated-parent
// change, or the parent ceasing to exist.
// ---------------------------------------------------------------------------
void DriftMatrixView::swapViewStateForScope(bool scopeActive)
{
    if (scopeActive == lastScopeActive) return;

    ViewState& leaving  = lastScopeActive ? childScopeView : parentScopeView;
    ViewState& entering = scopeActive     ? childScopeView : parentScopeView;

    leaving.panX = panX; leaving.panY = panY; leaving.zoom = zoom; leaving.valid = true;

    if (entering.valid) {
        panX = entering.panX; panY = entering.panY; zoom = entering.zoom;
    } else {
        // First time in this scope: start from the default framing rather than
        // inheriting a position computed for a matrix of a different size.
        panX = 0.0; panY = 0.0; zoom = 1.0;
    }
    lastScopeActive = scopeActive;
}

void DriftMatrixView::setViewState(double newZoom, double px, double py)
{
    // Full (zoom + pan) state pushed from another cross-connected matrix view.
    // All matrix views share an identical pixel layout at equal size, so
    // panX/panY transfer directly.  No signal is emitted so the change is not
    // echoed back to the sender.
    zoom = std::max(zoomMin, std::min(zoomMax, newZoom));
    panX = px;
    panY = py;
    update();
}

// ── input ────────────────────────────────────────────────────────────────────

void DriftMatrixView::mousePressEvent(QMouseEvent* e)
{
    emit viewInteracted();
    if ((e->button() == Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier)) {
        panAnchorPx = e->position().toPoint();
        panAnchorX  = panX;
        panAnchorY  = panY;
        panning     = false;   // becomes true once past the drag threshold
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void DriftMatrixView::mouseReleaseEvent(QMouseEvent* e)
{
    if (panning) {
        panning = false;
        setCursor(Qt::ArrowCursor);
        e->accept();
        return;
    }
    setCursor(Qt::ArrowCursor);

    // A plain click selects the clicked cell's pair -- row cluster A and column
    // cluster B -- and shows just those clusters in the scatter / waveform
    // views; Ctrl-click adds them to the current selection instead.  Mirrors
    // the error and residual matrix click behaviour, so a promising cell (high
    // correlation at some drift) can be inspected without hunting for the pair
    // by eye.  A stationary Ctrl-click reaches here with panning == false
    // because the pan only engages past the drag threshold in mouseMoveEvent.
    emit viewInteracted();
    if (!dataReady || clusterList.isEmpty())
        return;

    // Marked-node template region (§11.5): a click on one of the extra template
    // rows/columns selects that cell's cluster and overlays its node (handled by
    // KlustersView), before the cluster-pair hit-test below.
    if (!tplCols_.empty()) {
        const MatrixStripHit sh = matrixStripHitTest(
            e->position().x(), e->position().y(),
            effMatrixTopLeft(), effCellSize(), clusterList, tplCols_);
        if (sh.ok) { emit templateCellActivated(sh.clusterId, sh.node); update(); return; }
    }

    const int col = cellAtX(e->position().toPoint().x());
    const int row = cellAtY(e->position().toPoint().y());
    if (row < 0 || col < 0)
        return;
    QList<int> clustersToShow;
    clustersToShow.append(clusterList[row]);
    if (clusterList[col] != clusterList[row])
        clustersToShow.append(clusterList[col]);
    if (e->modifiers() & Qt::ControlModifier)
        doc.addFromMatrix(clustersToShow);
    else
        doc.selectFromMatrix(clustersToShow);
}

void DriftMatrixView::mouseMoveEvent(QMouseEvent* e)
{
    if ((e->buttons() & Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier)) {
        const QPoint d = e->position().toPoint() - panAnchorPx;
        if (panning || d.manhattanLength() >= panDragThreshold) {
            panning = true;
            panX = panAnchorX + d.x();
            panY = panAnchorY + d.y();
            update();
            emit viewChanged(zoom, panX, panY);
        }
        e->accept();
        return;
    }

    // Hover readout: pair, the signed shift applied to the row unit, and value.
    if (dataReady && scores) {
        const int row = cellAtY(e->position().toPoint().y());
        const int col = cellAtX(e->position().toPoint().x());
        if (row >= 0 && col >= 0) {
            const int a = clusterList[row], b = clusterList[col];
            if (row == col) {
                setInfoText(tr("cluster %1 (diagonal)").arg(a));
            } else {
                const int sign = (row < col) ? +1 : -1;
                setInfoText(
                    tr("A=%1 shifted %2%3 µm vs B=%4: xcorr %5")
                        .arg(a)
                        .arg(sign < 0 ? QStringLiteral("−") : QStringLiteral("+"))
                        .arg(currentDriftUm)
                        .arg(b)
                        .arg((*scores)(row+1, col+1), 0, 'g', 3));
            }
        }
    }
}

void DriftMatrixView::wheelEvent(QWheelEvent* event)
{
    if (!(event->modifiers() & Qt::ControlModifier)) {
        QWidget::wheelEvent(event);
        return;
    }
    emit viewInteracted();
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0) { event->accept(); return; }
    zoomAroundPoint(zoom * std::pow(zoomStep, steps), event->position());
    event->accept();
}

void DriftMatrixView::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomAroundPoint(zoom * zoomStep,
                        QPointF(width()/2.0, (height()-INFO_H+CTRL_H)/2.0));
        event->accept();
        return;
    case Qt::Key_Minus:
        zoomAroundPoint(zoom / zoomStep,
                        QPointF(width()/2.0, (height()-INFO_H+CTRL_H)/2.0));
        event->accept();
        return;
    case Qt::Key_0:
        resetPanZoom();
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

// ── hit testing ──────────────────────────────────────────────────────────────

int DriftMatrixView::cellAtX(int viewX) const
{
    const double eff = effCellSize();
    if (eff <= 0.0) return -1;
    const double mx = effMatrixTopLeft().x();
    const int ci = static_cast<int>(std::floor((viewX - mx) / eff));
    return (ci >= 0 && ci < clusterList.size()) ? ci : -1;
}

int DriftMatrixView::cellAtY(int viewY) const
{
    const double eff = effCellSize();
    if (eff <= 0.0) return -1;
    const double my = effMatrixTopLeft().y();
    const int ci = static_cast<int>(std::floor((viewY - my) / eff));
    return (ci >= 0 && ci < clusterList.size()) ? ci : -1;
}
