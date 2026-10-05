/***************************************************************************
 *                     matrixtemplatestrip_test.cpp                        *
 *                                                                         *
 *  Standalone test for the shared marked-node template-region GEOMETRY    *
 *  (§11.5): matrixStripIndexAt (pixel offset -> extended index) and        *
 *  matrixStripHitTest (a click -> which cluster + which template node),    *
 *  the parts of matrixtemplatecols.h the four curation matrices and the    *
 *  MatrixTemplateStrip helper all rely on.  Exits 0 on success, non-zero   *
 *  on first failure.                                                       *
 *                                                                         *
 *  Pure geometry only — the per-cell SHADE (MatrixTemplateStrip::          *
 *  computeShade) needs a live KlustersDoc and is not exercised here.       *
 *                                                                         *
 *  Build (standalone): needs Qt6 Core+Gui for QPointF/QColor.             *
 *    see src/klusters/tests/CMakeLists.txt (klusters_test_matrixtemplatestrip)
 ***************************************************************************/
#include "matrixtemplatecols.h"

#include <QList>
#include <QPointF>
#include <cstdio>
#include <vector>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } } while (0)

// A region of M template columns after an N-cluster block, cell size `eff`, at
// origin (0,0).  With kTemplateStripGapCells == 1 the x/y layout is:
//   clusters [0, N*eff) | gap [N*eff, (N+1)*eff) | templates [(N+1)*eff, (N+1+M)*eff)
int main()
{
    const int    N   = 3;
    const int    M   = 2;
    const double eff = 10.0;
    const double gap = kTemplateStripGapCells * eff;              // 10
    const double stripStart = N * eff + gap;                     // 40

    // ── matrixStripIndexAt: offset -> extended index [0,N+M), -1 off-grid ──
    CHECK(matrixStripIndexAt(-1.0,  eff, N, M) == -1);           // before the grid
    CHECK(matrixStripIndexAt(0.0,   eff, N, M) == 0);            // cluster 0
    CHECK(matrixStripIndexAt(19.9,  eff, N, M) == 1);            // cluster 1
    CHECK(matrixStripIndexAt(29.9,  eff, N, M) == 2);            // cluster 2 (last)
    CHECK(matrixStripIndexAt(N*eff, eff, N, M) == -1);           // first gap pixel
    CHECK(matrixStripIndexAt(stripStart - 0.1, eff, N, M) == -1); // last gap pixel
    CHECK(matrixStripIndexAt(stripStart,       eff, N, M) == N); // template 0 (index N)
    CHECK(matrixStripIndexAt(stripStart + eff, eff, N, M) == N + 1); // template 1
    CHECK(matrixStripIndexAt(stripStart + M*eff, eff, N, M) == -1);  // past the last template
    CHECK(matrixStripIndexAt(0.0, 0.0, N, M) == -1);            // eff<=0 guarded

    // ── matrixStripHitTest: click -> (clusterId, node) ────────────────────
    QList<int> ids;  ids << 10 << 20 << 30;                      // cluster id per row/column
    std::vector<MatrixTemplateCol> tpl(2);
    tpl[0].node = 7;  tpl[0].classId = 1;                        // template column 0
    tpl[1].node = 9;  tpl[1].classId = 1;                        // template column 1
    const QPointF o(0.0, 0.0);
    auto cx = [&](int k){ return k * eff + eff / 2.0; };         // centre of cluster cell k
    auto tx = [&](int t){ return stripStart + t * eff + eff / 2.0; }; // centre of template cell t

    // cluster ROW (r<N) × template COLUMN (c>=N): clusterId = row's cluster, node = col template
    {
        const MatrixStripHit h = matrixStripHitTest(tx(0), cx(1), o, eff, ids, tpl);
        CHECK(h.ok); CHECK(h.clusterId == 20); CHECK(h.node == 7);
    }
    // template ROW (r>=N) × cluster COLUMN (c<N): clusterId = col's cluster, node = row template
    {
        const MatrixStripHit h = matrixStripHitTest(cx(2), tx(1), o, eff, ids, tpl);
        CHECK(h.ok); CHECK(h.clusterId == 30); CHECK(h.node == 9);
    }
    // template × template corner: no cluster (clusterId -1), node = row template
    {
        const MatrixStripHit h = matrixStripHitTest(tx(1), tx(0), o, eff, ids, tpl);
        CHECK(h.ok); CHECK(h.clusterId == -1); CHECK(h.node == 7);
    }
    // the N×N cluster block is NOT the region -> miss
    {
        const MatrixStripHit h = matrixStripHitTest(cx(0), cx(1), o, eff, ids, tpl);
        CHECK(!h.ok);
    }
    // a click in the gap -> miss
    {
        const MatrixStripHit h = matrixStripHitTest(tx(0), N*eff + gap/2.0, o, eff, ids, tpl);
        CHECK(!h.ok);
    }
    // no template columns -> never a hit
    {
        const MatrixStripHit h = matrixStripHitTest(tx(0), cx(1), o, eff, ids, {});
        CHECK(!h.ok);
    }

    if (g_fail == 0) std::printf("matrixtemplatestrip_test: OK\n");
    return g_fail == 0 ? 0 : 1;
}
