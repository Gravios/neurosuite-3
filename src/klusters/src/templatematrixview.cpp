#include "templatematrixview.h"
#include "templatematrixthread.h"
#include "pairxcorrthread.h"
#include "klustersdoc.h"
#include "klustersview.h"
#include "configuration.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QEvent>
#include <QFont>
#include <QKeyEvent>
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
      dataReady(false), goingToDie(false), isStale(false), m_generation(0),
      m_pairThread(nullptr), m_pairGeneration(0),
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

    bar->addWidget(thresholdLabel);
    bar->addWidget(thresholdSlider, 1);
    bar->addWidget(countLabel);
    bar->addWidget(applyButton);

    mainLayout->addWidget(controlBar, 0);

    connect(thresholdSlider, &QSlider::valueChanged,
            this, &TemplateMatrixView::onThresholdChanged);
    connect(applyButton, &QPushButton::clicked,
            this, &TemplateMatrixView::onApplyClicked);

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
    return !threadsToBeKill.isEmpty() || (m_pairThread != nullptr);
}

void TemplateMatrixView::stopPairThread()
{
    if (m_pairThread) {
        m_pairThread->stopProcessing();
        // Don't wait here — the thread will post its event and we'll clean up
        // in customEvent (generation mismatch will discard the result).
        m_pairThread = nullptr;  // ownership stays with the thread until it posts
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
    if (m_pairThread) {
        m_pairThread->stopProcessing();
        while (!m_pairThread->wait()) {}
        // Thread has finished run() and posted its event; the customEvent
        // handler that consumes the post takes ownership.  Clear our
        // pointer so subsequent calls don't try to wait again.  The
        // event will arrive and be discarded by the generation check —
        // see TemplateMatrixView::customEvent's sourceCluster mismatch
        // branch.
        m_pairThread = nullptr;
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
    ++m_generation;
    stopPairThread();
    m_pairCache.clear();

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
    return new TemplateMatrixThread(*this, doc.data(), m_generation);
}

void TemplateMatrixView::launchPairXcorr(int sourceCluster, int targetCluster)
{
    if (goingToDie) return;

    // Check cache first
    const QPair<int,int> key(sourceCluster, targetCluster);
    if (m_pairCache.contains(key)) {
        updateSliderPreview();
        return;
    }

    // Find indices
    const int ciSrc = clusterList.indexOf(sourceCluster);
    const int ciTgt = clusterList.indexOf(targetCluster);
    if (ciSrc < 0 || ciTgt < 0 ||
        ciSrc >= static_cast<int>(m_allFileIdx.size()) ||
        ciTgt >= static_cast<int>(m_meanWav.size()))
        return;

    stopPairThread();
    ++m_pairGeneration;
    countLabel->setText("Computing scores…");
    applyButton->setEnabled(false);

    m_pairThread = new PairXcorrThread(
        *this,
        sourceCluster, targetCluster,
        m_allFileIdx[static_cast<size_t>(ciSrc)],
        m_meanWav[static_cast<size_t>(ciTgt)],
        doc.data().getSpkFileName(),
        doc.data().nbOfChannels(),
        doc.data().nbSamplesPerWaveform(),
        doc.data().isRecordingTwoBytes(),
        m_pairGeneration);
    // m_pairThread starts itself; ownership transfers to the thread
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
                               && thread->generation() == m_generation);

        if (accepted) {
            delete scores;
            scores      = newScores;
            clusterList = thread->getClusterList();
            m_meanWav   = thread->getMeanWav();
            m_allFileIdx= thread->getAllFileIdx();
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
        }
        return;
    }

    // ── Per-pair xcorr result ──────────────────────────────────────────────
    if (event->type() == QEvent::Type(QEvent::User + 602)) {
        auto* ev     = static_cast<PairXcorrThread::PairXcorrEvent*>(event);
        auto* thread = ev->parentThread();

        const bool accepted = (thread->generation() == m_pairGeneration);

        if (accepted) {
            const QPair<int,int> key(thread->sourceCluster(), thread->targetCluster());
            m_pairCache.insert(key, thread->getScores());
        }

        while (!thread->wait()) {}
        if (m_pairThread == thread) m_pairThread = nullptr;
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

QPoint TemplateMatrixView::matrixTopLeft() const
{
    return QPoint(LABEL_MARGIN + widthBorder, heightBorder);
}

int TemplateMatrixView::cellAtX(int viewX) const
{
    int ci = (viewX - matrixTopLeft().x()) / cellWidth;
    return (ci >= 0 && ci < clusterList.size()) ? ci : -1;
}

int TemplateMatrixView::cellAtY(int viewY) const
{
    int ci = (viewY - matrixTopLeft().y()) / cellWidth;
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
        buf.setPen(palette().color(QPalette::WindowText));
        buf.drawText(matRect, Qt::AlignCenter, "Computing template matrix\u2026");
    }
    buf.end();

    QPainter p(this);
    p.drawPixmap(0, 0, doublebuffer);
}

void TemplateMatrixView::drawMatrix(QPainter& p)
{
    const int n      = clusterList.size();
    const QPoint ori = matrixTopLeft();

    if (isStale) {
        QPen pen(Qt::red); pen.setWidth(2);
        p.setPen(pen);
        p.drawRect(ori.x()-1, ori.y()-1, n*cellWidth+2, n*cellWidth+2);
        p.setPen(Qt::NoPen);
    }

    for (int row = 0; row < n; ++row) {
        for (int col = 0; col < n; ++col) {
            const int px = ori.x() + col * cellWidth;
            const int py = ori.y() + row * cellWidth;

            if (row == col) {
                p.fillRect(px, py, cellWidth, cellWidth, Qt::black);
                continue;
            }

            const double sc = (*scores)(row+1, col+1);
            int idx = static_cast<int>(sc * (NB_COLORS-1) + 0.5);
            idx = std::max(0, std::min(NB_COLORS-1, idx));
            p.fillRect(px, py, cellWidth, cellWidth, colorMap[idx]);

            if (sc >= currentThreshold) {
                QPen wp(Qt::white); wp.setWidth(2);
                p.setPen(wp);
                p.drawRect(px+1, py+1, cellWidth-3, cellWidth-3);
                p.setPen(Qt::NoPen);
            }

            // Asymmetric selection: only the exact clicked cell (row=source, col=target)
            if (clusterList[row] == selectedB && clusterList[col] == selectedA) {
                QPen yp(Qt::yellow); yp.setWidth(3);
                p.setPen(yp);
                p.drawRect(px, py, cellWidth-1, cellWidth-1);
                p.setPen(Qt::NoPen);
            }
        }
    }
}

void TemplateMatrixView::drawClusterIds(QPainter& p)
{
    const int n      = clusterList.size();
    const QPoint ori = matrixTopLeft();
    const int fontSize = std::max(5, std::min(9, cellWidth/5));
    QFont f("Helvetica", fontSize);
    p.setFont(f);
    p.setPen(palette().color(QPalette::WindowText));

    for (int col = 0; col < n; ++col)
        p.drawText(QRect(ori.x()+col*cellWidth, 0, cellWidth, ori.y()),
                   Qt::AlignHCenter | Qt::AlignBottom,
                   QString::number(clusterList[col]));

    for (int row = 0; row < n; ++row)
        p.drawText(QRect(0, ori.y()+row*cellWidth, LABEL_MARGIN-2, cellWidth),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(clusterList[row]));
}

// ── mouse ────────────────────────────────────────────────────────────────────

void TemplateMatrixView::mouseMoveEvent(QMouseEvent* e)
{
    if (!dataReady || clusterList.isEmpty()) return;
    const int col = cellAtX(e->position().toPoint().x());
    const int row = cellAtY(e->position().toPoint().y());
    if (col < 0 || row < 0) return;
    const int cA = clusterList[col], cB = clusterList[row];
    if (cA == cB)
        statusBar->showMessage(QString("Cluster %1").arg(cA));
    else
        statusBar->showMessage(
            QString("Clusters (row=%1 \u2192 col=%2): mean xcorr = %3")
                .arg(cB).arg(cA)
                .arg((*scores)(row+1, col+1), 0, 'f', 3));
}

void TemplateMatrixView::mouseReleaseEvent(QMouseEvent* e)
{
    if (!dataReady || clusterList.isEmpty()) return;
    const int col = cellAtX(e->position().toPoint().x());
    const int row = cellAtY(e->position().toPoint().y());
    if (col < 0 || row < 0) return;
    const int cA = clusterList[col];  // target (column)
    const int cB = clusterList[row];  // source (row)
    if (cA == cB) return;

    selectedA = cA;
    selectedB = cB;

    QList<int> toShow;
    const QList<dataType> existing = doc.data().clusterIds();
    if (existing.contains(static_cast<dataType>(cA))) toShow.append(cA);
    if (existing.contains(static_cast<dataType>(cB))) toShow.append(cB);
    if (e->modifiers() & Qt::ControlModifier)
        doc.addClustersToActiveView(toShow);
    else
        doc.shownClustersUpdate(toShow);

    // Launch per-spike xcorr for this pair (uses cache if already computed)
    launchPairXcorr(selectedB, selectedA);

    setFocus();
    update();
}

// ── slider / apply ────────────────────────────────────────────────────────────

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
    if (!m_pairCache.contains(key)) {
        // Still computing — message already set by launchPairXcorr
        return;
    }

    const auto& spScores = m_pairCache[key];
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
    if (!m_pairCache.contains(key)) return;

    const auto& spScores = m_pairCache[key];
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
