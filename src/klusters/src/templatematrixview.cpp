#include "templatematrixview.h"
#include "templatematrixthread.h"
#include "pairxcorrthread.h"
#include "klustersdoc.h"
#include "klustersview.h"
#include "configuration.h"

#include <QApplication>
#include <QComboBox>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QImage>
#include <QEvent>
#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>   // patch80
#include <QWheelEvent>   // patch80
#include <QVector>
#include <cmath>
#include <algorithm>

static constexpr int CELL_WIDTH   = 50;
static constexpr int LABEL_MARGIN = 16;
static constexpr int CONTROLS_H   = 50;

// ── ctor / dtor ──────────────────────────────────────────────────────────────

TemplateMatrixView::TemplateMatrixView(KlustersDoc& doc_, KlustersView& view_,
                                       const QColor& backgroundColor,
                                       QStatusBar* statusBar_,
                                       QWidget* parent)
    : QWidget(parent),
      doc(doc_), view(view_), statusBar(statusBar_),
      scores(nullptr),
      dataReady(false), goingToDie(false), isStale(false), generation(0),
      pairThread(nullptr), pairGeneration(0),
      selectedA(-1), selectedB(-1),
      cellWidth(CELL_WIDTH), widthBorder(0), heightBorder(0),
      currentThreshold(0.90),
      sliderMin(configuration().getTemplateThresholdMin()),
      sliderMax(configuration().getTemplateThresholdMax())
{
    QPalette pal = palette();
    pal.setColor(QPalette::Window, backgroundColor);
    setPalette(pal);
    setAutoFillBackground(true);

    // Pick a painted-text colour that contrasts with the background: white on a
    // dark background, black on a light one (Rec. 601 luma, 0.5 threshold).
    {
        const double lum = 0.299 * backgroundColor.redF()
                         + 0.587 * backgroundColor.greenF()
                         + 0.114 * backgroundColor.blueF();
        textColor = (lum > 0.5) ? QColor(Qt::black) : QColor(Qt::white);
    }
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->setSpacing(0);
    mainLayout->addStretch(1);

    QWidget* controlBar = new QWidget(this);
    controlBar->setFixedHeight(CONTROLS_H);
    QHBoxLayout* bar = new QHBoxLayout(controlBar);
    bar->setContentsMargins(8,4,8,4);

    thresholdLabel = new QLabel(controlBar);
    thresholdLabel->setMinimumWidth(130);

    thresholdSlider = new QSlider(Qt::Horizontal, controlBar);
    thresholdSlider->setRange(0, 100);
    thresholdSlider->setTickPosition(QSlider::TicksBelow);
    thresholdSlider->setTickInterval(10);

    countLabel = new QLabel("", controlBar);
    countLabel->setMinimumWidth(200);

    applyButton = new QPushButton("Apply", controlBar);
    applyButton->setEnabled(false);
    applyButton->setToolTip("Move above-threshold source spikes into target cluster");

    // Metric selector: Cosine similarity vs Pearson correlation vs Raw (non-
    // normalised) cross-correlation.  Shares configuration().templateXcorrMetric
    // with the Display preference page, so the two stay in sync; switching it
    // here recomputes the matrix immediately with the new metric.
    QLabel* metricLabel = new QLabel("Metric:", controlBar);
    metricCombo = new QComboBox(controlBar);
    metricCombo->addItem("Cosine");      // index 0
    metricCombo->addItem("Pearson");     // index 1
    metricCombo->addItem("Raw");         // index 2
    metricCombo->addItem("Disatten.");   // index 3
    metricCombo->addItem("Fast-AP");     // index 4
    metricCombo->setItemData(0, "Peak normalised cross-correlation (cosine similarity) between cluster mean waveforms.", Qt::ToolTipRole);
    metricCombo->setItemData(1, "Pearson correlation: removes the overlap-window mean of each waveform before normalising.", Qt::ToolTipRole);
    metricCombo->setItemData(2, "Raw (non-normalised) peak cross-correlation, amplitude-weighted; the matrix is globally normalised so the strongest pair reads 1.0.", Qt::ToolTipRole);
    metricCombo->setItemData(3, "Energy-corrected cosine: subtracts each mean waveform's noise energy (within-cluster sample variance / N) from the norms, so a same-neuron pair reads ~1.0 regardless of spike count or amplitude. Capped at 1.0.", Qt::ToolTipRole);
    metricCombo->setItemData(4, "Energy-corrected (fast-AP) cosine: correlates only the peak +/- 8 sample window, dropping the energy-scaling post-peak after-potential that depresses high-amplitude same-neuron pairs.", Qt::ToolTipRole);
    {
        const int metric = configuration().getTemplateXcorrMetric();
        metricCombo->setCurrentIndex((metric >= 0 && metric <= 4) ? metric : 0);
    }

    // The control bar sits on the painted backgroundColor (which can be pure
    // black), but the transparent QLabels (Metric:, Threshold:, count) would
    // otherwise render with the application palette's default text colour —
    // black-on-black is invisible in a light theme over a dark plot background.
    // Tint them with the same luminance-derived contrast colour used for painted
    // text (textColor: white on a dark background, black on a light one), so the
    // labels track the background regardless of the active light/dark theme.
    // QLabel text is drawn from WindowText; Text/ButtonText are set too so the
    // result is style-independent.  The metric QComboBox and the applyButton are
    // left untouched — each draws its text against its own opaque widget
    // background, so it already contrasts regardless of the plot colour.
    {
        const auto tintText = [this](QWidget* w){
            QPalette wp = w->palette();
            wp.setColor(QPalette::WindowText, textColor);
            wp.setColor(QPalette::Text,       textColor);
            wp.setColor(QPalette::ButtonText, textColor);
            w->setPalette(wp);
        };
        tintText(metricLabel);
        tintText(thresholdLabel);
        tintText(countLabel);
    }

    bar->addWidget(metricLabel);
    bar->addWidget(metricCombo);
    bar->addWidget(thresholdLabel);
    bar->addWidget(thresholdSlider, 1);
    bar->addWidget(countLabel);
    bar->addWidget(applyButton);

    mainLayout->addWidget(controlBar, 0);

    connect(thresholdSlider, &QSlider::valueChanged,
            this, &TemplateMatrixView::onThresholdChanged);
    connect(applyButton, &QPushButton::clicked,
            this, &TemplateMatrixView::onApplyClicked);
    // currentIndexChanged carries the new index; the slot reads
    // metricCombo->currentIndex() itself, so the int arg is simply dropped.
    connect(metricCombo, &QComboBox::currentIndexChanged,
            this, &TemplateMatrixView::onMetricChanged);

    currentThreshold = std::max(sliderMin, std::min(sliderMax, currentThreshold));
    thresholdSlider->setValue(thresholdToSlider(currentThreshold));
    thresholdLabel->setText(
        QString("Threshold: %1").arg(currentThreshold, 0, 'f', 2));

    initializeColorMap();
    updateMatrixContents();
}

TemplateMatrixView::~TemplateMatrixView()
{
    willBeKilled();
    stopPairThread();
    for (TemplateMatrixThread* t : threadsToBeKill)
        while (!t->wait()) {}
    qDeleteAll(threadsToBeKill);
    threadsToBeKill.clear();
    delete scores;
    QApplication::removePostedEvents(this);
}

void TemplateMatrixView::willBeKilled()
{
    if (!goingToDie) {
        goingToDie = true;
        stopPairThread();
        for (TemplateMatrixThread* t : threadsToBeKill)
            t->stopProcessing();
    }
}

bool TemplateMatrixView::isThreadsRunning() const
{
    return !threadsToBeKill.isEmpty() || (pairThread != nullptr);
}

void TemplateMatrixView::stopPairThread()
{
    if (pairThread) {
        pairThread->stopProcessing();
        // Don't wait here — the thread will post its event and we'll clean up
        // in customEvent (generation mismatch will discard the result).
        pairThread = nullptr;  // ownership stays with the thread until it posts
    }
}

// ---------------------------------------------------------------------------
// stopRunningThreadsSync — synchronous teardown for safe concurrent file writes
//
// Both TemplateMatrixThread and PairXcorrThread fopen()/fread() the .spk
// pending file directly (templatematrixthread.cpp:113 and
// pairxcorrthread.cpp:21).  Operations that REWRITE .spk.pending —
// nudgeClusterTimestamps and realignSpikes — call
// KlustersView::stopAllViewThreads() to quiesce concurrent readers before
// touching the file.
//
// The catch: TemplateMatrixView inherits from QWidget, not ViewWidget, so
// it is NOT in KlustersView::viewList (which is a QList<ViewWidget*>).
// stopAllViewThreads' iteration of viewList never reached us.  Result:
// while nudge rewrote a spike's .spk slot, an in-flight TemplateMatrixThread
// or PairXcorrThread could be mid-read on the same byte range, returning
// a mix of pre- and post-nudge bytes.  Visible symptom — and the user's
// reported one — was a subset of spikes (those whose .spk slots overlapped
// an in-flight read) showing torn waveforms after nudge.
//
// This method closes the race by waiting synchronously for every in-flight
// thread to return from run() before yielding control back to the caller.
// stopAllViewThreads now invokes this method on every TemplateMatrixView it
// finds via findChildren<TemplateMatrixView*>(), so the by-the-time we
// open .spk.pending for writing, no other handle is reading from it.
// ---------------------------------------------------------------------------
void TemplateMatrixView::stopRunningThreadsSync()
{
    // 1. Pair thread: signal stop, then wait for run() to return.  We must
    //    NOT use the existing stopPairThread() here because that orphans
    //    the pointer (deferred cleanup in customEvent).  We need synchronous
    //    completion so the file handle is closed before we return.
    if (pairThread) {
        pairThread->stopProcessing();
        while (!pairThread->wait()) {}
        // Thread has finished run() and posted its event; the customEvent
        // handler that consumes the post takes ownership.  Clear our
        // pointer so subsequent calls don't try to wait again.  The
        // event will arrive and be discarded by the generation check —
        // see TemplateMatrixView::customEvent's sourceCluster mismatch
        // branch.
        pairThread = nullptr;
    }

    // 2. Matrix threads: same pattern as WaveformView::stopAndClearThreads.
    //    The threads currently in threadsToBeKill are NOT necessarily dead
    //    yet — the destructor pattern relies on Qt's event loop to drain
    //    them via customEvent.  For the in-place quiesce we need, drain
    //    them ourselves.
    for (TemplateMatrixThread* t : threadsToBeKill)
        t->stopProcessing();
    for (TemplateMatrixThread* t : threadsToBeKill)
        while (!t->wait()) {}
    qDeleteAll(threadsToBeKill);
    threadsToBeKill.clear();

    // 3. Drop any completion events those threads posted before we waited
    //    so customEvent doesn't fire later with a dangling thread pointer.
    QApplication::removePostedEvents(this);
}

// ── colour map ───────────────────────────────────────────────────────────────

void TemplateMatrixView::initializeColorMap()
{
    for (int i = 0; i < NB_COLORS; ++i) {
        int hue = static_cast<int>(240.0 * (1.0 - static_cast<double>(i) / (NB_COLORS-1)));
        QColor c;
        c.setHsv(hue, 220, 255);
        colorMap.insert(i, c);
    }
}

// ── slider range ─────────────────────────────────────────────────────────────

void TemplateMatrixView::updateSliderRange()
{
    sliderMin = configuration().getTemplateThresholdMin();
    sliderMax = configuration().getTemplateThresholdMax();
    currentThreshold = std::max(sliderMin, std::min(sliderMax, currentThreshold));
    thresholdSlider->setValue(thresholdToSlider(currentThreshold));
    thresholdLabel->setText(
        QString("Threshold: %1").arg(currentThreshold, 0, 'f', 2));
    updateSliderPreview();
    update();
}

// ── compute ──────────────────────────────────────────────────────────────────

void TemplateMatrixView::updateMatrixContents()
{
    if (goingToDie) return;
    ++generation;
    stopPairThread();
    pairCache.clear();

    // Reflect the current metric (it may have been changed via the Display
    // preference page since this view was built).  Block signals so this
    // refresh doesn't re-enter onMetricChanged → updateMatrixContents.
    {
        const int metric = configuration().getTemplateXcorrMetric();
        const QSignalBlocker block(metricCombo);
        metricCombo->setCurrentIndex((metric >= 0 && metric <= 4) ? metric : 0);
    }

    for (TemplateMatrixThread* t : threadsToBeKill)
        t->stopProcessing();

    setCursor(Qt::WaitCursor);
    threadsToBeKill.append(launchComputeThread());

    isStale = false;
    selectedPairs.clear();
    selectedA = selectedB = -1;
    applyButton->setEnabled(false);
    countLabel->setText("");
    update();
}

TemplateMatrixThread* TemplateMatrixView::launchComputeThread()
{
    return new TemplateMatrixThread(*this, doc.data(), generation);
}

void TemplateMatrixView::launchPairXcorr(int sourceCluster, int targetCluster)
{
    if (goingToDie) return;

    // Check cache first
    const QPair<int,int> key(sourceCluster, targetCluster);
    if (pairCache.contains(key)) {
        updateSliderPreview();
        return;
    }

    // Find indices
    const int ciSrc = clusterList.indexOf(sourceCluster);
    const int ciTgt = clusterList.indexOf(targetCluster);
    if (ciSrc < 0 || ciTgt < 0 ||
        ciSrc >= static_cast<int>(allFileIdx.size()) ||
        ciTgt >= static_cast<int>(meanWav.size()))
        return;

    stopPairThread();
    ++pairGeneration;
    countLabel->setText("Computing scores…");
    applyButton->setEnabled(false);

    pairThread = new PairXcorrThread(
        *this,
        sourceCluster, targetCluster,
        allFileIdx[static_cast<size_t>(ciSrc)],
        meanWav[static_cast<size_t>(ciTgt)],
        doc.data().getSpkFileName(),
        doc.data().nbOfChannels(),
        doc.data().nbSamplesPerWaveform(),
        doc.data().isRecordingTwoBytes(),
        pairGeneration);
    // pairThread starts itself; ownership transfers to the thread
    // — we keep the pointer only to call stopProcessing() if needed.
}

// ── customEvent: handles both User+601 (matrix) and User+602 (pair xcorr) ───

void TemplateMatrixView::customEvent(QEvent* event)
{
    // ── Main matrix result ─────────────────────────────────────────────────
    if (event->type() == QEvent::Type(QEvent::User + 601)) {
        auto* ev     = static_cast<TemplateMatrixThread::TemplateMatrixEvent*>(event);
        auto* thread = ev->parentThread();

        Array<double>* newScores = thread->getScores();
        const bool accepted = (newScores != nullptr
                               && thread->getGeneration() == generation);

        if (accepted) {
            delete scores;
            scores      = newScores;
            clusterList = thread->getClusterList();
            meanWav   = thread->getMeanWav();
            allFileIdx= thread->getAllFileIdx();
        } else {
            delete newScores;
        }

        while (!thread->wait()) {}
        threadsToBeKill.removeAll(thread);
        delete thread;

        if (!goingToDie) {
            if (accepted) {
                updateWindow();
                dataReady = true;
                setCursor(Qt::ArrowCursor);
            } else if (threadsToBeKill.isEmpty()) {
                setCursor(Qt::ArrowCursor);
            }
            update();
            // Notify external listeners (e.g. KlustersApp Shift+S reorder
            // waiting for a stale matrix to refresh).  Emitted only on the
            // accepted branch and only after update() so observers receive
            // an in-sync (freshly painted, freshly read-able) view.
            if (accepted)
                emit matrixUpdated();
        }
        return;
    }

    // ── Per-pair xcorr result ──────────────────────────────────────────────
    if (event->type() == QEvent::Type(QEvent::User + 602)) {
        auto* ev     = static_cast<PairXcorrThread::PairXcorrEvent*>(event);
        auto* thread = ev->parentThread();

        const bool accepted = (thread->getGeneration() == pairGeneration);

        if (accepted) {
            const QPair<int,int> key(thread->getSourceCluster(), thread->getTargetCluster());
            pairCache.insert(key, thread->getScores());
        }

        while (!thread->wait()) {}
        if (pairThread == thread) pairThread = nullptr;
        delete thread;

        if (!goingToDie && accepted)
            updateSliderPreview();
        return;
    }
}

// ── geometry ─────────────────────────────────────────────────────────────────

void TemplateMatrixView::updateWindow()
{
    // Called only when new data arrives — computes cellWidth from current
    // widget dimensions.  resizeEvent does NOT call this, so cellWidth is
    // stable and the matrix never progressively shrinks on resize events.
    const int n = clusterList.size();
    if (n <= 0) return;
    const int matH   = std::max(height() - CONTROLS_H, 1);
    matrixViewport   = QRect(0, 0, width(), matH);
    const int availW = width() - LABEL_MARGIN - 10;
    const int availH = matH - 14 - 10;
    const int fitW   = (availW > 0 && n > 0) ? availW / n : CELL_WIDTH;
    const int fitH   = (availH > 0 && n > 0) ? availH / n : CELL_WIDTH;
    cellWidth        = std::max(4, std::min({fitW, fitH, CELL_WIDTH}));
    widthBorder      = cellWidth / 3 + 5;
    heightBorder     = cellWidth / 3 + 14;
}

double TemplateMatrixView::effZoomMin() const
{
    // Mirror updateWindow()'s available-area math.  The grid spans n*cellWidth
    // pixels at zoom 1; the fit-all zoom is the ratio that shrinks it to the
    // available area.  When the grid overflows even at the 0.5 floor (large n,
    // cellWidth pinned at 4px), drop the floor to fit-all so the whole matrix can
    // be viewed; otherwise keep the historical 0.5.
    const int n = clusterList.size();
    if (n <= 0 || cellWidth <= 0) return zoomMin;
    const int matH   = std::max(height() - CONTROLS_H, 1);
    const int availW = width() - LABEL_MARGIN - 10;
    const int availH = matH - 14 - 10;
    const int avail  = std::min(availW, availH);
    if (avail <= 0) return zoomMin;
    const double fitAll = static_cast<double>(avail)
                        / (static_cast<double>(n) * cellWidth);
    return std::min(zoomMin, fitAll);
}

QPoint TemplateMatrixView::matrixTopLeft() const
{
    return QPoint(LABEL_MARGIN + widthBorder, heightBorder);
}

int TemplateMatrixView::cellAtX(int viewX) const
{
    // patch80 — account for pan + zoom.  Use double arithmetic and
    // floor() so cells just inside the grid don't round into -1.
    const double eff = effCellSize();
    if (eff <= 0.0) return -1;
    const double mx = effMatrixTopLeft().x();
    const int ci = static_cast<int>(std::floor((viewX - mx) / eff));
    return (ci >= 0 && ci < clusterList.size()) ? ci : -1;
}

int TemplateMatrixView::cellAtY(int viewY) const
{
    const double eff = effCellSize();
    if (eff <= 0.0) return -1;
    const double my = effMatrixTopLeft().y();
    const int ci = static_cast<int>(std::floor((viewY - my) / eff));
    return (ci >= 0 && ci < clusterList.size()) ? ci : -1;
}

// ── painting ─────────────────────────────────────────────────────────────────

void TemplateMatrixView::paintEvent(QPaintEvent*)
{
    const int matH   = height() - CONTROLS_H;
    const QRect matRect(0, 0, width(), std::max(matH, 1));
    if (doublebuffer.size() != matRect.size())
        doublebuffer = QPixmap(matRect.size());
    doublebuffer.fill(palette().color(QPalette::Window));

    QPainter buf(&doublebuffer);
    if (dataReady) {
        drawMatrix(buf);
        drawClusterIds(buf);
    } else {
        buf.setPen(textColor);
        buf.drawText(matRect, Qt::AlignCenter, "Computing template matrix\u2026");
    }
    buf.end();

    QPainter p(this);
    p.drawPixmap(0, 0, doublebuffer);
}

void TemplateMatrixView::drawMatrix(QPainter& p)
{
    const int n      = clusterList.size();
    // patch80 — render at the pan/zoom transform.  Use floating-point
    // for accumulation so sub-pixel zoom levels don't drift visibly.
    const QPointF oriF = effMatrixTopLeft();
    const double  eff  = effCellSize();
    const int     w    = std::max(1, static_cast<int>(std::round(eff)));

    if (isStale) {
        QPen pen(Qt::red); pen.setWidth(2);
        p.setPen(pen);
        p.drawRect(QRectF(oriF.x()-1, oriF.y()-1,
                          n * eff + 2, n * eff + 2));
        p.setPen(Qt::NoPen);
    }

    if (n > 0) {
        // One indexed image (pixel per cell) blitted once, instead of n*n
        // fillRect() calls (~9M at 3000 clusters) every paint — same change as
        // ErrorMatrixView.  Colour computation is unchanged.
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
                const double sc = (*scores)(row+1, col+1);
                int idx = static_cast<int>(sc * (NB_COLORS-1) + 0.5);
                idx = std::max(0, std::min(NB_COLORS-1, idx));
                line[col] = static_cast<uchar>(idx);
            }
        }

        const bool prevSmooth = p.testRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);   // crisp cells
        p.drawImage(QRectF(oriF.x(), oriF.y(), n * eff, n * eff), img);
        p.setRenderHint(QPainter::SmoothPixmapTransform, prevSmooth);

        // Threshold outlines drawn over the image — the n*n scan is cheap (score
        // read + compare); only cells at/above threshold issue a drawRect.
        QPen wp(Qt::white); wp.setWidth(2);
        p.setPen(wp);
        p.setBrush(Qt::NoBrush);
        for (int row = 0; row < n; ++row) {
            for (int col = 0; col < n; ++col) {
                if (row == col) continue;
                if ((*scores)(row+1, col+1) >= currentThreshold) {
                    const int px = static_cast<int>(std::round(oriF.x() + col * eff));
                    const int py = static_cast<int>(std::round(oriF.y() + row * eff));
                    p.drawRect(px+1, py+1, w-3, w-3);
                }
            }
        }
        p.setPen(Qt::NoPen);
    }

    // Edge-highlight every selected pair (multi-select via Ctrl-click), not
    // just the most-recent one.  Each pair is stored as (rowSourceId,
    // colTargetId); map back to grid indices and outline the cell.  Pairs
    // whose cluster no longer exists are skipped.
    if (!selectedPairs.isEmpty()) {
        QPen yp(Qt::yellow); yp.setWidth(3);
        p.setPen(yp);
        p.setBrush(Qt::NoBrush);
        for (const Pair& sp : selectedPairs) {
            const int prow = clusterList.indexOf(sp.first);
            const int pcol = clusterList.indexOf(sp.second);
            if (prow < 0 || pcol < 0) continue;
            const int hx = static_cast<int>(std::round(oriF.x() + pcol * eff));
            const int hy = static_cast<int>(std::round(oriF.y() + prow * eff));
            p.drawRect(hx, hy, w-1, w-1);
        }
        p.setPen(Qt::NoPen);
    }
}

void TemplateMatrixView::drawClusterIds(QPainter& p)
{
    const int n      = clusterList.size();
    // patch80 — label margin stays anchored at screen edge for legibility,
    // but per-column / per-row labels track the cell positions through
    // the pan/zoom transform.  Column labels live in the top strip
    // (y = 0 .. base.y()), so they pan/zoom horizontally only; row
    // labels live in the left strip (x = 0 .. LABEL_MARGIN-2), so they
    // pan/zoom vertically only.  When pan/zoom would push a label
    // outside the strip it just clips — acceptable for now; a future
    // patch could hide off-screen labels and add a scroll indicator.
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
        p.drawText(QRect(px, 0, w, base.y()),
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

// ── mouse ────────────────────────────────────────────────────────────────────

// patch80 — Ctrl + Left-drag pans; everything else (plain click, Ctrl-click
// without drag) keeps its original meaning.  The state machine:
//
//   press      : if Ctrl held, record anchor pos and current pan offsets.
//                We do NOT start panning yet — only after the cursor moves
//                panDragThreshold pixels, so a quick Ctrl-click still
//                hits the click path (which adds clusters to the active
//                view; see mouseReleaseEvent line ~510 in the original).
//   move       : if anchor recorded AND mouse moved past threshold, set
//                panning = true and update panX/Y from the delta.
//                If not panning, keep the original status-bar hover
//                feedback.
//   release    : if we ended up panning, swallow the click (the user was
//                dragging the view, not selecting a cell).  Otherwise
//                run the original click logic.

void TemplateMatrixView::mousePressEvent(QMouseEvent* e)
{
    if ((e->buttons() & Qt::LeftButton) &&
        (e->modifiers() & Qt::ControlModifier)) {
        panAnchorPx = e->position().toPoint();
        panAnchorX  = panX;
        panAnchorY  = panY;
        // Do NOT set panning yet — wait for move past threshold.
        // This keeps Ctrl + quick-click → add-clusters working.
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    // Pass through to base.  Note: the previous header had this as an
    // empty {} override, so no behaviour change for non-Ctrl clicks.
}

void TemplateMatrixView::mouseMoveEvent(QMouseEvent* e)
{
    // patch80 — pan path takes priority while the left button is held
    // with Ctrl and we've passed the drag threshold.
    if ((e->buttons() & Qt::LeftButton) &&
        (e->modifiers() & Qt::ControlModifier)) {
        const QPoint d = e->position().toPoint() - panAnchorPx;
        if (!panning &&
            (std::abs(d.x()) + std::abs(d.y()) >= panDragThreshold)) {
            panning = true;
        }
        if (panning) {
            panX = panAnchorX + d.x();
            panY = panAnchorY + d.y();
            update();
            emit viewChanged(zoom, panX, panY);
            e->accept();
            return;
        }
    }

    // Original hover-feedback path.
    if (!dataReady || clusterList.isEmpty()) return;
    const int col = cellAtX(e->position().toPoint().x());
    const int row = cellAtY(e->position().toPoint().y());
    if (col < 0 || row < 0) return;
    const int cA = clusterList[col], cB = clusterList[row];
    if (cA == cB)
        statusBar->showMessage(QString("Cluster %1").arg(cA));
    else
        statusBar->showMessage(
            QString("Clusters (row=%1 \u2192 col=%2): %3 = %4")
                .arg(cB).arg(cA)
                .arg([]{ switch (configuration().getTemplateXcorrMetric()) {
                             case 1:  return QStringLiteral("Pearson");
                             case 2:  return QStringLiteral("raw");
                             default: return QStringLiteral("cosine"); } }())
                .arg((*scores)(row+1, col+1), 0, 'f', 5));
}

void TemplateMatrixView::mouseReleaseEvent(QMouseEvent* e)
{
    // patch80 — if the user just finished a pan drag, swallow the click
    // and reset cursor.  The original click logic (cluster selection /
    // add-clusters) is only reachable when panning was never set.
    if (panning) {
        panning = false;
        unsetCursor();
        e->accept();
        return;
    }
    // If Ctrl was held but we never crossed the drag threshold, the
    // cursor is still ClosedHand — restore it before running the
    // original click logic.
    if (e->modifiers() & Qt::ControlModifier) {
        unsetCursor();
    }

    // Tell KlustersApp's last-interacted tracker BEFORE the early-return
    // guards — even a click on an empty/stale matrix counts as the
    // user "focusing" this view for the Shift+S reorder selection.
    emit viewInteracted();
    if (!dataReady || clusterList.isEmpty()) return;
    const int col = cellAtX(e->position().toPoint().x());
    const int row = cellAtY(e->position().toPoint().y());
    if (col < 0 || row < 0) return;
    const int cA = clusterList[col];  // target (column)
    const int cB = clusterList[row];  // source (row)
    if (cA == cB) return;

    // Multi-select for the G-group workflow.  Ctrl-click toggles a pair in or
    // out of selectedPairs (and the shown-cluster set the user then groups
    // with G); a plain click replaces the selection with this one pair.
    // selectedA/selectedB continue to track the most-recent pair for the
    // threshold slider / Apply single-pair tool.
    const Pair pair(cB, cA);  // (rowSourceId, colTargetId)
    const QList<dataType> existing = doc.data().clusterIds();
    const bool ctrl = (e->modifiers() & Qt::ControlModifier);

    QList<int> toShow;
    if (ctrl && selectedPairs.contains(pair)) {
        // Toggle this pair off and rebuild the shown set from what remains.
        selectedPairs.removeAll(pair);
        for (const Pair& sp : selectedPairs) {
            if (existing.contains(static_cast<dataType>(sp.first)))  toShow.append(sp.first);
            if (existing.contains(static_cast<dataType>(sp.second))) toShow.append(sp.second);
        }
        doc.shownClustersUpdate(toShow);
        selectedA = selectedB = -1;
        applyButton->setEnabled(false);
        countLabel->setText("");
    } else {
        if (!ctrl)
            selectedPairs.clear();
        if (!selectedPairs.contains(pair))
            selectedPairs.append(pair);
        if (existing.contains(static_cast<dataType>(cA))) toShow.append(cA);
        if (existing.contains(static_cast<dataType>(cB))) toShow.append(cB);
        if (ctrl) doc.addClustersToActiveView(toShow);
        else      doc.shownClustersUpdate(toShow);
        selectedA = cA;
        selectedB = cB;
        // Launch per-spike xcorr for this pair (uses cache if already computed)
        launchPairXcorr(selectedB, selectedA);
    }

    setFocus();
    update();
}

// patch80 — zoom helpers.  Ctrl + wheel zooms around the cursor; the
// +/= and - keys zoom around the widget centre; the 0 key resets.
//
// Zoom-around-pivot derivation: the matrix point under widget-pixel P
// is matCoord = (P - base - pan) / (cellW * zoom).  We want this same
// matCoord to stay under P after zoom changes:
//   P - base - panNew = matCoord * cellW * zoomNew
//                     = (P - base - panOld) * (zoomNew / zoomOld)
//   ⇒ panNew = P - base - (P - base - panOld) * ratio
// which simplifies to:
//   panNew = panOld + (P - base - panOld) * (1 - ratio)

void TemplateMatrixView::zoomAroundPoint(double newZoom, const QPointF& pivot)
{
    newZoom = std::clamp(newZoom, effZoomMin(), zoomMax);
    if (zoom <= 0.0) return;
    const double ratio = newZoom / zoom;
    const QPoint  base = matrixTopLeft();
    const double dx = (pivot.x() - base.x() - panX) * (1.0 - ratio);
    const double dy = (pivot.y() - base.y() - panY) * (1.0 - ratio);
    panX += dx;
    panY += dy;
    zoom = newZoom;
    update();
    emit viewChanged(zoom, panX, panY);
}

void TemplateMatrixView::resetPanZoom()
{
    panX = panY = 0.0;
    zoom = 1.0;
    update();
    emit viewChanged(zoom, panX, panY);
}

void TemplateMatrixView::setViewState(double newZoom, double px, double py)
{
    // Full (zoom + pan) state pushed from the cross-connected error-matrix view.
    // The two views share an identical pixel layout at equal size, so panX/panY
    // transfer directly.  No signal is emitted so the change is not echoed back.
    zoom = std::clamp(newZoom, effZoomMin(), zoomMax);
    panX = px;
    panY = py;
    update();
}

void TemplateMatrixView::wheelEvent(QWheelEvent* event)
{
    if (!(event->modifiers() & Qt::ControlModifier)) {
        // Without Ctrl, defer to base (lets scroll behaviour pass through
        // to a parent QScrollArea if one is ever introduced).
        QWidget::wheelEvent(event);
        return;
    }
    const int delta = event->angleDelta().y();
    if (delta == 0) { event->accept(); return; }
    const double factor = (delta > 0) ? zoomStep : 1.0 / zoomStep;
    zoomAroundPoint(zoom * factor, event->position());
    event->accept();
}

// ── slider / apply ────────────────────────────────────────────────────────────

void TemplateMatrixView::onMetricChanged()
{
    // Single source of truth: configuration().templateXcorrMetric, shared with
    // the Display preference page.  Persist immediately so the choice survives
    // restart and the preference dialog reflects it, then recompute — the
    // compute thread reads the value at run time (TemplateMatrixThread::run).
    const int metric = metricCombo->currentIndex();   // index == metric int
    if (metric == configuration().getTemplateXcorrMetric())
        return;                       // no-op toggle (e.g. programmatic refresh)
    configuration().setTemplateXcorrMetric(metric);
    configuration().write();
    updateMatrixContents();
}

void TemplateMatrixView::onThresholdChanged(int sliderValue)
{
    currentThreshold = sliderToThreshold(sliderValue);
    thresholdLabel->setText(
        QString("Threshold: %1").arg(currentThreshold, 0, 'f', 2));
    updateSliderPreview();
    update();
}

void TemplateMatrixView::updateSliderPreview()
{
    if (!dataReady || selectedA < 0 || selectedB < 0) {
        countLabel->setText("");
        applyButton->setEnabled(false);
        return;
    }

    const QPair<int,int> key(selectedB, selectedA);
    if (!pairCache.contains(key)) {
        // Still computing — message already set by launchPairXcorr
        return;
    }

    const auto& spScores = pairCache[key];
    int count = 0;
    for (const auto& sp : spScores)
        if (sp.second >= static_cast<float>(currentThreshold))
            ++count;

    const int total = static_cast<int>(spScores.size());
    countLabel->setText(
        QString("%1 / %2 spikes \u2265 threshold").arg(count).arg(total));
    applyButton->setEnabled(count > 0);
}

void TemplateMatrixView::onApplyClicked()
{
    if (selectedA < 0 || selectedB < 0) return;
    const QPair<int,int> key(selectedB, selectedA);
    if (!pairCache.contains(key)) return;

    const auto& spScores = pairCache[key];
    QVector<int> toMove;
    for (const auto& sp : spScores)
        if (sp.second >= static_cast<float>(currentThreshold))
            toMove.append(sp.first);

    if (toMove.isEmpty()) return;

    doc.moveSpikeSubsetToCluster(selectedB, toMove, selectedA, view);

    selectedA = selectedB = -1;
    applyButton->setEnabled(false);
    countLabel->setText("");

    updateMatrixContents();
}

// ── keyboard ──────────────────────────────────────────────────────────────────

void TemplateMatrixView::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (applyButton->isEnabled()) onApplyClicked();
        event->accept(); return;
    }
    if (key == Qt::Key_Escape) {
        selectedA = selectedB = -1;
        stopPairThread();
        applyButton->setEnabled(false);
        countLabel->setText("");
        update(); event->accept(); return;
    }
    // patch80 — keyboard zoom around the widget centre.
    if (key == Qt::Key_Plus || key == Qt::Key_Equal) {
        const QPointF c(width() * 0.5,
                        (height() - CONTROLS_H) * 0.5);
        zoomAroundPoint(zoom * zoomStep, c);
        event->accept(); return;
    }
    if (key == Qt::Key_Minus || key == Qt::Key_Underscore) {
        const QPointF c(width() * 0.5,
                        (height() - CONTROLS_H) * 0.5);
        zoomAroundPoint(zoom / zoomStep, c);
        event->accept(); return;
    }
    if (key == Qt::Key_0) {
        resetPanZoom();
        event->accept(); return;
    }
    QWidget::keyPressEvent(event);
}

// ── stale markers ─────────────────────────────────────────────────────────────

void TemplateMatrixView::clustersGrouped(QList<int>&, int)           { isStale=true; update(); }
void TemplateMatrixView::clustersDeleted(QList<int>&, int)           { isStale=true; update(); }
void TemplateMatrixView::removeSpikesFromClusters(QList<int>&,int,QList<int>&){ isStale=true; update(); }
void TemplateMatrixView::newClusterAdded(QList<int>&,int,QList<int>&){ isStale=true; update(); }
void TemplateMatrixView::newClustersAdded(QMap<int,int>&,QList<int>&){ isStale=true; update(); }
void TemplateMatrixView::newClustersAdded(QList<int>&)               { isStale=true; update(); }
void TemplateMatrixView::renumber(QMap<int,int>&)                    { isStale=true; update(); }

void TemplateMatrixView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    // Only update viewport rect — do NOT recompute cellWidth, which would
    // cause the matrix to shrink each time the widget is laid out.
    const int matH = std::max(height() - CONTROLS_H, 1);
    matrixViewport  = QRect(0, 0, width(), matH);
    update();
}

QSize TemplateMatrixView::sizeHint() const
{
    const int n    = std::max(static_cast<int>(clusterList.size()), 5);
    const int side = LABEL_MARGIN + (cellWidth * n) + 20;
    return QSize(side, side + CONTROLS_H + 30);
}
