#include "residualmatrixview.h"
#include <QDebug>
#include <QStringList>
#include "matrixgrid.h"
#include "matrixbadge.h"
#include "matrixtemplatecols.h"  // drawMatrixTemplateStrip — the shared edge-strip renderer
#include "channelmask.h"         // cmCompactChannels — match the template to the matrix's channels
#include "residualmatrixthread.h"
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
      dataReady(false), goingToDie(false), isStale(false),
      jobToken(std::make_shared<KlustersJobToken>()),
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
    // Ignored horizontally: a plain QLabel reports its entire (non-wrapping)
    // text width as its minimum, which would floor the whole matrix dock frame
    // at ~1300 px and stop the user narrowing the pane.
    infoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    setInfoText(tr("Separability — cell(row A, col B) = fraction of A's residual about B's template "
                          "that is systematic vs noise, in [0,1]; red ≈ 0 (indistinguishable given A's noise, "
                          "merge candidate), blue ≈ 1 (distinct); diagonal = A's within-cluster variance."));
    mainLayout->addWidget(infoLabel, 0);

    initializeColorMap();
    updateMatrixContents();
}

ResidualMatrixView::~ResidualMatrixView()
{
    willBeKilled();
    //Fence the completion posts: once viewDead is set under the lock, no job
    //will post to this view again.  The jobs themselves are not waited for:
    //they were superseded above, the pool owns and deletes them, and their
    //results travel inside the events, which delete what nobody takes.
    {
        QMutexLocker lock(&jobToken->postMutex);
        jobToken->viewDead = true;
    }
    delete scoresAll;
    delete scoresSel;   // `scores` is non-owning: it aliases one of these
    QApplication::removePostedEvents(this);
}

void ResidualMatrixView::willBeKilled()
{
    if (!goingToDie) {
        goingToDie = true;
        //Supersede the in-flight jobs: each stops at its next cancellation
        //check, and its completion event fails the generation guard.
        jobToken->generation.fetch_add(1, std::memory_order_acq_rel);
    }
}

bool ResidualMatrixView::isThreadsRunning() const
{
    return jobToken->active.load(std::memory_order_acquire) > 0;
}

void ResidualMatrixView::stopRunningThreadsSync()
{
    // Supersede every in-flight job, then wait for them to retire: callers
    // are about to rewrite .spk.pending, so no job of this view may still be
    // inside a file read once this returns (the synchronous-quiesce
    // contract).
    jobToken->generation.fetch_add(1, std::memory_order_acq_rel);
    while (jobToken->active.load(std::memory_order_acquire) > 0)
        QThread::msleep(1);
    // Drop completion events the superseded jobs posted before retiring;
    // they would fail the generation guard anyway.
    QApplication::removePostedEvents(this, QEvent::User + 603);
}

void ResidualMatrixView::supersedeRunningThreads()
{
    //Non-blocking twin of stopRunningThreadsSync() (epoch-snapshot
    //step 6b) — see TemplateMatrixView::supersedeRunningThreads().
    jobToken->generation.fetch_add(1, std::memory_order_acq_rel);
    QApplication::removePostedEvents(this, QEvent::User + 603);
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

void ResidualMatrixView::launchCompute()
{
    if (goingToDie) return;
    //Supersede the in-flight computes (their results are stale by definition
    //now); the replacement job enqueued below captures the new generation.
    jobToken->generation.fetch_add(1, std::memory_order_acq_rel);
    computing = true;      // paint a badge over the old matrix, do not blank

    setCursor(Qt::WaitCursor);
    launchComputeThread();

    isStale = false;
    update();
}

void ResidualMatrixView::updateMatrixContents()
{
    // Swap zoom/pan when the scope changes, before anything is drawn: the parent
    // matrix and the child-scoped one are different matrices and a position in
    // one does not mean anything in the other.
    swapViewStateForScope(doc.matrixScopeActive());

    if (goingToDie) return;
    // The spikes changed: both cached matrices are stale.  Only this path
    // invalidates — a mere selection change must keep the other slot.
    invalidateCaches();
    launchCompute();
}

void ResidualMatrixView::launchComputeThread()
{
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
            << "[matrixscope] ResidualMatrixView"
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
    new ResidualMatrixThread(*this, doc.matrixData(), jobToken,
                             doc.selectedChannels(),
                             doc.matrixScopeClusters());
}
void ResidualMatrixView::invalidateCaches()
{
    // Mark both slots out of date, but do NOT free them and do NOT drop
    // dataReady: `scores` keeps pointing at the matrix already on screen so the
    // view can go on painting it while the replacement computes.  The memory is
    // released when the new result is filed into its slot.  Only the flags gate
    // the instant-swap path, so nothing stale is ever swapped in as current.
    haveAllCache = false;
    haveSelCache = false;
    cachedSelection.clear();
}

void ResidualMatrixView::selectedChannelsChanged(const QList<int>& channels)
{
    if (goingToDie) return;

    // Swap in a cached result when we already have one for this selection; only
    // an uncached selection costs a recompute.  Note this does NOT invalidate:
    // keeping the other slot is what makes swapping back free.
    if (channels.isEmpty()) {
        if (haveAllCache && scoresAll) {
            scores = scoresAll;
            dataReady = true;
            recomputeTemplateStripCells();   // strip follows the swapped-in matrix's means
            updateWindow();
            update();
            return;
        }
    } else if (haveSelCache && channels == cachedSelection && scoresSel) {
        scores = scoresSel;
        dataReady = true;
        recomputeTemplateStripCells();       // strip follows the swapped-in matrix's means
        updateWindow();
        update();
        return;
    }
    launchCompute();
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

    auto* ev = static_cast<ResidualMatrixThread::ResidualMatrixEvent*>(event);

    //The job retired itself and the results travel in the event, so there is
    //no thread to wait for or delete anymore.
    const bool genMatch = (ev->generation()
                           == jobToken->generation.load(std::memory_order_acquire));
    Array<double>* newScores = ev->takeScores();
    const bool accepted = (newScores != nullptr && genMatch);
    // Only the generation we are waiting on ends the "computing" state: a
    // superseded result arriving must not cancel the badge for the newer
    // compute that replaced it.
    if (genMatch) computing = false;

    if (accepted) {
        // File under the selection it was computed for, so the other slot
        // stays available for an instant swap.  The per-cluster means/noise the
        // strip needs travel with it and are filed in the matching slot, so a
        // later swap restores them together with the matrix.
        const QList<int> ranFor = ev->getSelection();
        if (ranFor.isEmpty()) {
            delete scoresAll;
            scoresAll    = newScores;
            haveAllCache = true;
            meanAll_  = ev->takeMeanWav();
            noiseAll_ = ev->takeMeanVar();
            keepAll_  = ev->takeKeepChannels();
        } else {
            delete scoresSel;
            scoresSel       = newScores;
            cachedSelection = ranFor;
            haveSelCache    = true;
            meanSel_  = ev->takeMeanWav();
            noiseSel_ = ev->takeMeanVar();
            keepSel_  = ev->takeKeepChannels();
        }
        nChanFull_  = ev->getNbChannels();
        nSampFull_  = ev->getNbSamples();
        scores      = newScores;
        clusterList = ev->getClusterList();
        recomputeDisplayMax();
        recomputeTemplateStripCells();   // fresh means/clusters: refresh the strip
    } else {
        delete newScores;
    }

    if (!goingToDie) {
        if (accepted) {
            updateWindow();
            dataReady = true;
            setCursor(Qt::ArrowCursor);
        } else if (jobToken->active.load(std::memory_order_acquire) == 0) {
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
    // Reserve room for the marked-node template strip (gap + M cells) so it fits
    // on screen instead of running off the right/bottom edge (as the others do).
    const int nTot   = n + strip_.stripCells();
    const int matH   = std::max(height() - INFO_H, 1);
    matrixViewport   = QRect(0, 0, width(), matH);
    const int availW = width() - LABEL_MARGIN - 10;
    const int availH = matH - 14 - 10;
    const int fitW   = (availW > 0) ? availW / nTot : CELL_WIDTH;
    const int fitH   = (availH > 0) ? availH / nTot : CELL_WIDTH;
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
    }
    // A recompute no longer wipes the frame: whatever was there stays up and a
    // small badge says fresh numbers are coming.  With nothing to show yet (the
    // first compute) the badge centres itself instead.
    if (computing)
        mbDrawComputingBadge(buf, rect(), tr("Computing residual matrix\u2026"),
                             dataReady);
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
        // One-pixel dashed grid so adjacent cells read as separate elements.
        // Full-span lines only; boxing each cell would draw every interior edge
        // twice for the same picture.
        drawMatrixGrid(p, oriF, eff, n);
        p.setRenderHint(QPainter::SmoothPixmapTransform, prevSmooth);

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

    // Marked-node template columns/rows at the edge (residual separability, §11.5).
    drawTemplateStrip(p);
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
        nav_.begin(e->position().toPoint(), vp_);
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
}

void ResidualMatrixView::mouseMoveEvent(QMouseEvent* e)
{
    if ((e->buttons() & Qt::LeftButton) &&
        (e->modifiers() & Qt::ControlModifier)) {
        if (nav_.drag(e->position().toPoint(), vp_, panDragThreshold)) {
            update();
            emit viewChanged(vp_.zoom, vp_.panX, vp_.panY);
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
                setInfoText(tr("cluster %1: within-cluster variance %2")
                    .arg(a).arg((*scores)(row+1, col+1), 0, 'g', 4));
            else
                setInfoText(tr("A=%1 vs B=%2: separability %3   (A's noise floor %4)")
                    .arg(a).arg(b)
                    .arg((*scores)(row+1, col+1), 0, 'g', 3)
                    .arg((*scores)(row+1, row+1), 0, 'g', 4));
        }
    }
}

void ResidualMatrixView::mouseReleaseEvent(QMouseEvent* e)
{
    if (nav_.isPanning()) {
        nav_.end(e->position().toPoint());
        setCursor(Qt::ArrowCursor);
        e->accept();
        return;
    }
    nav_.end(e->position().toPoint());
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

    // Marked-node template region (§11.5): a click on one of the extra template
    // rows/columns selects that cell's cluster and overlays its node (handled by
    // KlustersView), before the cluster-pair hit-test below.
    if (!strip_.empty()) {
        const MatrixStripHit sh = strip_.hitTest(
            e->position().x(), e->position().y(),
            effMatrixTopLeft(), effCellSize(), clusterList);
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


// ── marked-node template columns (§11.5) ──────────────────────────────────────
void ResidualMatrixView::setTemplateColumns(const std::vector<MatrixTemplateCol>& cols)
{
    strip_.setColumns(cols);
    recomputeTemplateStripCells();
    if (dataReady) updateWindow();   // refit so the region (gap + M cells) is on screen
    update();
}

void ResidualMatrixView::recomputeTemplateStripCells()
{
    const auto& tc = strip_.cols();
    const int M = static_cast<int>(tc.size());
    const int N = clusterList.size();
    tplClusterRow_.assign(M, std::vector<double>(N, 0.0));
    tplTemplateRow_.assign(M, std::vector<double>(N, 0.0));
    tplCorner_.assign(M, std::vector<double>(M, 0.0));
    if (M == 0 || N == 0 || nSampFull_ <= 0) { strip_.computeShade(doc, clusterList, [](int,int){ return false; }); return; }

    // Read the slot `scores` currently aliases, so the region matches the matrix
    // on screen even after a channel-selection swap.
    const bool useSel = (scores != nullptr && scores == scoresSel);
    const std::vector<std::vector<float>>& clMean  = useSel ? meanSel_  : meanAll_;
    const std::vector<double>&             clNoise = useSel ? noiseSel_ : noiseAll_;
    const std::vector<int>&                keep    = useSel ? keepSel_  : keepAll_;
    if (static_cast<int>(clMean.size()) < N || static_cast<int>(clNoise.size()) < N) {
        strip_.computeShade(doc, clusterList, [](int,int){ return false; }); return; }
    const int effPts = clMean.empty() ? 0 : static_cast<int>(clMean[0].size());
    if (effPts <= 0) { strip_.computeShade(doc, clusterList, [](int,int){ return false; }); return; }
    const double invPts = 1.0 / static_cast<double>(effPts);

    // Compact each template mean + its per-point variance (std²) to the same
    // channels the matrix used, so the residual is over the same points.
    std::vector<std::vector<float>> tMean(M);
    std::vector<double>             tNoise(M, 0.0);   // template noise floor mean_p var_T[p]
    std::vector<char>               tOk(M, 0);
    std::vector<float> tmp;
    for (int t = 0; t < M; ++t) {
        if (static_cast<int>(tc[static_cast<size_t>(t)].mean.size()) != nChanFull_ * nSampFull_) continue;
        cmCompactChannels(tc[static_cast<size_t>(t)].mean, nChanFull_, nSampFull_, keep, tmp);
        if (static_cast<int>(tmp.size()) != effPts) continue;      // geometry mismatch -> leave greyed
        tMean[t] = tmp;
        if (static_cast<int>(tc[static_cast<size_t>(t)].std.size()) == nChanFull_ * nSampFull_) {
            std::vector<float> vtmp;
            cmCompactChannels(tc[static_cast<size_t>(t)].std, nChanFull_, nSampFull_, keep, vtmp);
            double s = 0.0;
            for (int p = 0; p < effPts; ++p) s += static_cast<double>(vtmp[static_cast<size_t>(p)])
                                                 * static_cast<double>(vtmp[static_cast<size_t>(p)]);
            tNoise[t] = s * invPts;                                // mean_p std_T[p]²
        }
        tOk[t] = 1;
    }

    // A template column compares to a cluster only when both compacted to the
    // same point count — the single predicate for both the value and the shade.
    auto hasData = [&](int t, int j) {
        return tOk[static_cast<size_t>(t)] && static_cast<int>(clMean[static_cast<size_t>(j)].size()) == effPts;
    };

    auto sepIndex = [](double gap, double floor)->double {        // M = gap/(noise+gap), as the body
        const double d = floor + gap;
        return (d > 0.0) ? gap / d : 0.0;
    };

    for (int t = 0; t < M; ++t) {
        for (int j = 0; j < N; ++j) {
            if (!hasData(t, j)) continue;                         // leave at 0 (shaded grey)
            const std::vector<float>& cm = clMean[static_cast<size_t>(j)];
            double gap = 0.0;                                     // mean_p (mean_j − T)²  (symmetric)
            for (int p = 0; p < effPts; ++p) {
                const double d = static_cast<double>(cm[static_cast<size_t>(p)])
                               - static_cast<double>(tMean[t][static_cast<size_t>(p)]);
                gap += d * d;
            }
            gap *= invPts;
            tplClusterRow_[t][j]  = sepIndex(gap, clNoise[static_cast<size_t>(j)]);  // row = cluster
            tplTemplateRow_[t][j] = sepIndex(gap, tNoise[t]);                        // row = template
        }
        for (int u = 0; u < M; ++u) {
            if (!tOk[t] || !tOk[u]) { tplCorner_[t][u] = 0.0; continue; }
            double gap = 0.0;
            for (int p = 0; p < effPts; ++p) {
                const double d = static_cast<double>(tMean[t][static_cast<size_t>(p)])
                               - static_cast<double>(tMean[u][static_cast<size_t>(p)]);
                gap += d * d;
            }
            gap *= invPts;
            tplCorner_[t][u] = sepIndex(gap, tNoise[t]);          // row = template t floor
        }
    }
    // The in-window dimming (spike-time range vs each node's [a,b]) lives in the
    // shared helper, fed the same hasData predicate.
    strip_.computeShade(doc, clusterList, hasData);
}

void ResidualMatrixView::drawTemplateStrip(QPainter& p)
{
    const int M = strip_.size();
    const int N = clusterList.size();
    if (M == 0 || N == 0 || static_cast<int>(tplClusterRow_.size()) != M) return;
    const QPointF oriF = effMatrixTopLeft();
    const double  eff  = effCellSize();

    // ASYMMETRIC: a cluster-row cell normalises by the cluster's noise, a
    // template-row cell by the template's noise, exactly like the matrix body.
    auto value = [&](int r, int c, MatrixStripShade& shade)->double{
        if (r < N && c >= N) { const int t = c - N; shade = strip_.shade(t, r); return tplClusterRow_[t][r]; }
        if (r >= N && c < N) { const int t = r - N; shade = strip_.shade(t, c); return tplTemplateRow_[t][c]; }
        shade = MatrixStripValue; return tplCorner_[r - N][c - N];     // template×template corner
    };
    const double inv = (displayMax > 0.0) ? 1.0 / displayMax : 1.0;
    auto colourFor = [&](double v)->QColor{                // same ramp as drawMatrix
        double r = v * inv;
        r = std::max(0.0, std::min(1.0, r));
        int idx = static_cast<int>((1.0 - r) * (NB_COLORS - 1) + 0.5);
        idx = std::max(0, std::min(NB_COLORS - 1, idx));
        return colorMap[idx];
    };
    drawMatrixTemplateStrip(p, oriF, eff, N, M, value, colourFor);
}

void ResidualMatrixView::setInfoText(const QString& text)
{
    infoText = text;
    updateInfoElide();
}

void ResidualMatrixView::updateInfoElide()
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

void ResidualMatrixView::zoomAroundPoint(double newZoom, const QPointF& pivot)
{
    // No adaptive floor here (unlike the error/template matrices) — clamp to zoomMin.
    vp_.zoomAround(newZoom, pivot, matrixTopLeft(), zoomMin, zoomMax);
    update();
    emit viewChanged(vp_.zoom, vp_.panX, vp_.panY);
}

void ResidualMatrixView::resetPanZoom()
{
    vp_.reset();
    update();
    emit viewChanged(vp_.zoom, vp_.panX, vp_.panY);
}

// ResidualMatrixView::swapViewStateForScope is now an inline forwarder to
// vp_.swapForScope() (see the header); the shared logic lives in MatrixViewport.

void ResidualMatrixView::setViewState(double newZoom, double px, double py)
{
    // Full (zoom + pan) state pushed from another cross-connected matrix view.
    // All matrix views share an identical pixel layout at equal size, so
    // panX/panY transfer directly.  No signal is emitted so the change is not
    // echoed back to the sender.
    vp_.setState(newZoom, px, py, zoomMin, zoomMax);
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
    zoomAroundPoint(vp_.zoom * std::pow(zoomStep, steps), event->position());
    event->accept();
}

void ResidualMatrixView::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomAroundPoint(vp_.zoom * zoomStep,
                        QPointF(width()/2.0, (height()-INFO_H)/2.0));
        event->accept();
        return;
    case Qt::Key_Minus:
        zoomAroundPoint(vp_.zoom / zoomStep,
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
void ResidualMatrixView::markStale()                                            { isStale=true; update(); }

void ResidualMatrixView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    updateInfoElide();
    if (dataReady) updateWindow();
    update();
}

QSize ResidualMatrixView::sizeHint() const
{
    return QSize(400, 400);
}
