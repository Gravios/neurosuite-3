#include "residualmatrixview.h"
#include "residualmatrixthread.h"
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
#include <cmath>
#include <algorithm>

static constexpr int CELL_WIDTH   = 50;
static constexpr int LABEL_MARGIN = 16;
static constexpr int INFO_H       = 24;

// ── ctor / dtor ──────────────────────────────────────────────────────────────

ResidualMatrixView::ResidualMatrixView(KlustersDoc& doc_, KlustersView& view_,
                                       const QColor& backgroundColor,
                                       QStatusBar* statusBar_,
                                       QWidget* parent)
    : QWidget(parent),
      doc(doc_), view(view_), statusBar(statusBar_),
      scores(nullptr),
      dataReady(false), goingToDie(false), isStale(false), generation(0),
      displayMax(1.0),
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
    mainLayout->addStretch(1);

    infoLabel = new QLabel(this);
    infoLabel->setFixedHeight(INFO_H);
    infoLabel->setContentsMargins(8,0,8,0);
    infoLabel->setText(tr("Separability — cell(row A, col B) = fraction of A's residual about B's template "
                          "that is systematic vs noise, in [0,1]; red ≈ 0 (indistinguishable given A's noise, "
                          "merge candidate), blue ≈ 1 (distinct); diagonal = A's within-cluster variance."));
    mainLayout->addWidget(infoLabel, 0);

    initializeColorMap();
    updateMatrixContents();
}

ResidualMatrixView::~ResidualMatrixView()
{
    willBeKilled();
    for (ResidualMatrixThread* t : threadsToBeKill)
        while (!t->wait()) {}
    qDeleteAll(threadsToBeKill);
    threadsToBeKill.clear();
    delete scores;
    QApplication::removePostedEvents(this);
}

void ResidualMatrixView::willBeKilled()
{
    if (!goingToDie) {
        goingToDie = true;
        for (ResidualMatrixThread* t : threadsToBeKill)
            t->stopProcessing();
    }
}

bool ResidualMatrixView::isThreadsRunning() const
{
    return !threadsToBeKill.isEmpty();
}

void ResidualMatrixView::stopRunningThreadsSync()
{
    for (ResidualMatrixThread* t : threadsToBeKill)
        t->stopProcessing();
    for (ResidualMatrixThread* t : threadsToBeKill)
        while (!t->wait()) {}
    qDeleteAll(threadsToBeKill);
    threadsToBeKill.clear();
    QApplication::removePostedEvents(this);
}

// ── colour map (cool→warm; index high = warm/red, low = cool/blue) ───────────

void ResidualMatrixView::initializeColorMap()
{
    for (int i = 0; i < NB_COLORS; ++i) {
        int hue = static_cast<int>(240.0 * (1.0 - static_cast<double>(i) / (NB_COLORS-1)));
        QColor c;
        c.setHsv(hue, 220, 255);
        colorMap.insert(i, c);
    }
}

// ── compute ──────────────────────────────────────────────────────────────────

void ResidualMatrixView::updateMatrixContents()
{
    if (goingToDie) return;
    ++generation;

    for (ResidualMatrixThread* t : threadsToBeKill)
        t->stopProcessing();

    setCursor(Qt::WaitCursor);
    threadsToBeKill.append(launchComputeThread());

    isStale = false;
    update();
}

ResidualMatrixThread* ResidualMatrixView::launchComputeThread()
{
    return new ResidualMatrixThread(*this, doc.data(), generation);
}

void ResidualMatrixView::recomputeDisplayMax()
{
    // M(i,j) is a bounded separability fraction in [0,1], so the colour scale is
    // fixed and absolute at 1.0: red at 0 (merge candidate), blue at 1 (distinct).
    // No data-dependent rescale, so an all-mergeable matrix is not stretched up to
    // full blue.  (The diagonal holds the raw within-cluster variance but is drawn
    // black and never colour-mapped, so it does not affect this scale.)
    displayMax = 1.0;
}

// ── customEvent (User+603) ───────────────────────────────────────────────────

void ResidualMatrixView::customEvent(QEvent* event)
{
    if (event->type() != QEvent::Type(QEvent::User + 603)) return;

    auto* ev     = static_cast<ResidualMatrixThread::ResidualMatrixEvent*>(event);
    auto* thread = ev->parentThread();

    Array<double>* newScores = thread->getScores();
    const bool accepted = (newScores != nullptr
                           && thread->getGeneration() == generation);

    if (accepted) {
        delete scores;
        scores      = newScores;
        clusterList = thread->getClusterList();
        recomputeDisplayMax();
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
        if (accepted)
            emit matrixUpdated();
    }
}

// ── geometry ─────────────────────────────────────────────────────────────────

void ResidualMatrixView::updateWindow()
{
    const int n = clusterList.size();
    if (n <= 0) return;
    const int matH   = std::max(height() - INFO_H, 1);
    matrixViewport   = QRect(0, 0, width(), matH);
    const int availW = width() - LABEL_MARGIN - 10;
    const int availH = matH - 14 - 10;
    const int fitW   = (availW > 0) ? availW / n : CELL_WIDTH;
    const int fitH   = (availH > 0) ? availH / n : CELL_WIDTH;
    cellWidth        = std::max(4, std::min({fitW, fitH, CELL_WIDTH}));
    widthBorder      = cellWidth / 3 + 5;
    heightBorder     = cellWidth / 3 + 14;
}

QPoint ResidualMatrixView::matrixTopLeft() const
{
    return QPoint(LABEL_MARGIN + widthBorder, heightBorder);
}

int ResidualMatrixView::cellAtX(int viewX) const
{
    const double eff = effCellSize();
    if (eff <= 0.0) return -1;
    const double mx = effMatrixTopLeft().x();
    const int ci = static_cast<int>(std::floor((viewX - mx) / eff));
    return (ci >= 0 && ci < clusterList.size()) ? ci : -1;
}

int ResidualMatrixView::cellAtY(int viewY) const
{
    const double eff = effCellSize();
    if (eff <= 0.0) return -1;
    const double my = effMatrixTopLeft().y();
    const int ci = static_cast<int>(std::floor((viewY - my) / eff));
    return (ci >= 0 && ci < clusterList.size()) ? ci : -1;
}

// ── painting ─────────────────────────────────────────────────────────────────

void ResidualMatrixView::paintEvent(QPaintEvent*)
{
    const int matH   = height() - INFO_H;
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
        buf.drawText(matRect, Qt::AlignCenter, tr("Computing residual matrix\u2026"));
    }
    buf.end();

    QPainter p(this);
    p.drawPixmap(0, 0, doublebuffer);
}

void ResidualMatrixView::drawMatrix(QPainter& p)
{
    const int n      = clusterList.size();
    const QPointF oriF = effMatrixTopLeft();
    const double  eff  = effCellSize();

    if (isStale) {
        QPen pen(Qt::red); pen.setWidth(2);
        p.setPen(pen);
        p.drawRect(QRectF(oriF.x()-1, oriF.y()-1, n * eff + 2, n * eff + 2));
        p.setPen(Qt::NoPen);
    }

    if (n > 0 && scores) {
        const double inv = (displayMax > 0.0) ? 1.0 / displayMax : 1.0;
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
                // Normalised residual in [0,1]; warm (high idx) = small residual.
                double r = (*scores)(row+1, col+1) * inv;
                r = std::max(0.0, std::min(1.0, r));
                int idx = static_cast<int>((1.0 - r) * (NB_COLORS-1) + 0.5);
                idx = std::max(0, std::min(NB_COLORS-1, idx));
                line[col] = static_cast<uchar>(idx);
            }
        }

        const bool prevSmooth = p.testRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawImage(QRectF(oriF.x(), oriF.y(), n * eff, n * eff), img);
        p.setRenderHint(QPainter::SmoothPixmapTransform, prevSmooth);
    }
}

void ResidualMatrixView::drawClusterIds(QPainter& p)
{
    const int n      = clusterList.size();
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

// ── mouse: Ctrl+drag pans; plain move updates the hovered-value readout; ──────
//    any press emits viewInteracted (last-matrix-used tracking) ───────────────

void ResidualMatrixView::mousePressEvent(QMouseEvent* e)
{
    emit viewInteracted();
    if ((e->buttons() & Qt::LeftButton) &&
        (e->modifiers() & Qt::ControlModifier)) {
        panAnchorPx = e->position().toPoint();
        panAnchorX  = panX;
        panAnchorY  = panY;
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
}

void ResidualMatrixView::mouseMoveEvent(QMouseEvent* e)
{
    if ((e->buttons() & Qt::LeftButton) &&
        (e->modifiers() & Qt::ControlModifier)) {
        const QPoint d = e->position().toPoint() - panAnchorPx;
        if (panning ||
            d.manhattanLength() >= panDragThreshold) {
            panning = true;
            panX = panAnchorX + d.x();
            panY = panAnchorY + d.y();
            update();
        }
        e->accept();
        return;
    }

    // Hover readout: show raw residual + diagonal (noise floor) for the pair.
    if (dataReady && scores) {
        const int row = cellAtY(e->position().toPoint().y());
        const int col = cellAtX(e->position().toPoint().x());
        if (row >= 0 && col >= 0) {
            const int a = clusterList[row], b = clusterList[col];
            if (row == col)
                infoLabel->setText(tr("cluster %1: within-cluster variance %2")
                    .arg(a).arg((*scores)(row+1, col+1), 0, 'g', 4));
            else
                infoLabel->setText(tr("A=%1 vs B=%2: separability %3   (A's noise floor %4)")
                    .arg(a).arg(b)
                    .arg((*scores)(row+1, col+1), 0, 'g', 3)
                    .arg((*scores)(row+1, row+1), 0, 'g', 4));
        }
    }
}

void ResidualMatrixView::mouseReleaseEvent(QMouseEvent* e)
{
    if (panning) {
        panning = false;
        setCursor(Qt::ArrowCursor);
        e->accept();
        return;
    }
    setCursor(Qt::ArrowCursor);

    // A plain click selects the clicked cell's pair -- row cluster A and column
    // cluster B -- and shows just those clusters in the scatter / waveform views;
    // Ctrl-click adds them to the current selection instead.  Mirrors the error-
    // matrix click behaviour so the residual matrix is usable for curation, not
    // just inspection.  (A stationary Ctrl-click reaches here with panning == false
    // because the pan only engages past the drag threshold in mouseMoveEvent.)
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

void ResidualMatrixView::zoomAroundPoint(double newZoom, const QPointF& pivot)
{
    newZoom = std::max(zoomMin, std::min(zoomMax, newZoom));
    const QPointF base = QPointF(matrixTopLeft());
    // Keep the matrix-space point under the pivot fixed across the zoom.
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

void ResidualMatrixView::resetPanZoom()
{
    panX = panY = 0.0;
    zoom = 1.0;
    update();
}

void ResidualMatrixView::wheelEvent(QWheelEvent* event)
{
    if (!(event->modifiers() & Qt::ControlModifier)) {
        QWidget::wheelEvent(event);
        return;
    }
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) { event->accept(); return; }
    zoomAroundPoint(zoom * std::pow(zoomStep, steps), event->position());
    event->accept();
}

void ResidualMatrixView::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomAroundPoint(zoom * zoomStep,
                        QPointF(width()/2.0, (height()-INFO_H)/2.0));
        event->accept();
        return;
    case Qt::Key_Minus:
        zoomAroundPoint(zoom / zoomStep,
                        QPointF(width()/2.0, (height()-INFO_H)/2.0));
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

// ── stale slots ──────────────────────────────────────────────────────────────

void ResidualMatrixView::clustersGrouped(QList<int>&, int)                       { isStale=true; update(); }
void ResidualMatrixView::clustersDeleted(QList<int>&, int)                       { isStale=true; update(); }
void ResidualMatrixView::removeSpikesFromClusters(QList<int>&,int,QList<int>&)   { isStale=true; update(); }
void ResidualMatrixView::newClusterAdded(QList<int>&,int,QList<int>&)            { isStale=true; update(); }
void ResidualMatrixView::newClustersAdded(QMap<int,int>&,QList<int>&)            { isStale=true; update(); }
void ResidualMatrixView::newClustersAdded(QList<int>&)                           { isStale=true; update(); }
void ResidualMatrixView::renumber(QMap<int,int>&)                               { isStale=true; update(); }

void ResidualMatrixView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (dataReady) updateWindow();
    update();
}

QSize ResidualMatrixView::sizeHint() const
{
    return QSize(400, 400);
}
