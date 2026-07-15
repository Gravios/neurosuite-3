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
#include "driftmatrixthread.h"
#include "driftmatrixkernel.h"
#include "driftgeometry.h"
#include "klustersdoc.h"
#include "klustersview.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QImage>
#include <QEvent>
#include <QFont>
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
      dataReady(false), goingToDie(false), isStale(false), generation(0),
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
    infoLabel->setText(tr("Drift matrix — cell(row A, col B) = xcorr of A's mean shifted "
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
    for (DriftMatrixThread* t : threadsToBeKill)
        while (!t->wait()) {}
    qDeleteAll(threadsToBeKill);
    threadsToBeKill.clear();
    delete scores;
    QApplication::removePostedEvents(this);
}

void DriftMatrixView::willBeKilled()
{
    if (!goingToDie) {
        goingToDie = true;
        for (DriftMatrixThread* t : threadsToBeKill)
            t->stopProcessing();
    }
}

bool DriftMatrixView::isThreadsRunning() const
{
    return !threadsToBeKill.isEmpty();
}

void DriftMatrixView::stopRunningThreadsSync()
{
    for (DriftMatrixThread* t : threadsToBeKill)
        t->stopProcessing();
    for (DriftMatrixThread* t : threadsToBeKill)
        while (!t->wait()) {}
    qDeleteAll(threadsToBeKill);
    threadsToBeKill.clear();
    QApplication::removePostedEvents(this);
}

// ── compute ──────────────────────────────────────────────────────────────────

DriftMatrixThread* DriftMatrixView::launchComputeThread()
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

    return new DriftMatrixThread(*this, doc.data(), std::move(chanDepths),
                                 static_cast<float>(currentDriftUm), generation);
}

void DriftMatrixView::updateMatrixContents()
{
    if (goingToDie) return;
    ++generation;

    for (DriftMatrixThread* t : threadsToBeKill)
        t->stopProcessing();

    setCursor(Qt::WaitCursor);
    threadsToBeKill.append(launchComputeThread());

    isStale = false;
    update();
}

void DriftMatrixView::customEvent(QEvent* event)
{
    if (event->type() != QEvent::Type(QEvent::User + 604)) return;

    auto* ev     = static_cast<DriftMatrixThread::DriftMatrixEvent*>(event);
    auto* thread = ev->parentThread();

    Array<double>* newScores = thread->getScores();
    const bool accepted = (newScores != nullptr
                           && thread->getGeneration() == generation);

    if (accepted) {
        delete scores;
        scores         = newScores;
        clusterList    = thread->getClusterList();
        meanWav        = thread->getMeanWav();
        depths         = thread->getDepths();
        nChanCached    = thread->getNbChannels();
        nSampCached    = thread->getNbSamples();
        maxShiftCached = thread->getMaxShift();
        geometryOk     = thread->geometryOk();
    } else {
        delete newScores;
    }

    while (!thread->wait()) {}
    threadsToBeKill.removeAll(thread);
    delete thread;

    if (!goingToDie) {
        if (accepted) {
            // Without geometry the shift is meaningless: keep the (unshifted)
            // matrix visible but say why the slider is dead rather than leaving
            // the user to guess.
            driftSlider->setEnabled(geometryOk);
            maxUmSpin->setEnabled(geometryOk);
            if (!geometryOk) {
                driftLabel->setText(tr("Drift: n/a"));
                const QString why = geometryError.isEmpty()
                                        ? tr("no probe geometry for this group")
                                        : geometryError;
                driftSlider->setToolTip(tr("Drift shifting needs probe geometry: %1.").arg(why));
                infoLabel->setText(tr("No probe geometry (%1) — showing unshifted "
                                      "correlations; the drift slider is disabled.").arg(why));
            }

            updateWindow();
            dataReady = true;
            setCursor(Qt::ArrowCursor);
        } else if (threadsToBeKill.isEmpty()) {
            setCursor(Qt::ArrowCursor);
        }
        update();
        if (accepted)
            emit matrixUpdated();
    }
}

void DriftMatrixView::recomputeAtCurrentDrift()
{
    if (!dataReady || !geometryOk || scores == nullptr) return;
    if (meanWav.empty() || static_cast<int>(meanWav.size()) != clusterList.size())
        return;

    dmComputeDriftMatrix(meanWav, depths, nChanCached, nSampCached,
                         maxShiftCached, static_cast<float>(currentDriftUm),
                         *scores);
    update();
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
    const int matH   = std::max(height() - CTRL_H - INFO_H, 1);
    const int availW = width() - LABEL_MARGIN - 10;
    const int availH = matH - 14 - 10;
    const int fitW   = (availW > 0) ? availW / n : CELL_WIDTH;
    const int fitH   = (availH > 0) ? availH / n : CELL_WIDTH;
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
    } else {
        buf.setPen(textColor);
        buf.drawText(QRect(0, CTRL_H, width(),
                           std::max(height() - CTRL_H - INFO_H, 1)),
                     Qt::AlignCenter, tr("Computing drift matrix\u2026"));
    }
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
    }
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
}

void DriftMatrixView::resetPanZoom()
{
    panX = panY = 0.0;
    zoom = 1.0;
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
    const int col = cellAtX(e->position().toPoint().x());
    const int row = cellAtY(e->position().toPoint().y());
    if (row < 0 || col < 0)
        return;
    QList<int> clustersToShow;
    clustersToShow.append(clusterList[row]);
    if (clusterList[col] != clusterList[row])
        clustersToShow.append(clusterList[col]);
    if (e->modifiers() & Qt::ControlModifier)
        doc.addClustersToActiveView(clustersToShow);
    else
        doc.shownClustersUpdate(clustersToShow);
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
                infoLabel->setText(tr("cluster %1 (diagonal)").arg(a));
            } else {
                const int sign = (row < col) ? +1 : -1;
                infoLabel->setText(
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
