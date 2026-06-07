/***************************************************************************
 *                       selectiongrid2d_test.cpp                          *
 *                                                                         *
 *  Standalone test for SelectionGrid2D. Exits 0 on success, non-zero on   *
 *  first failure.  Build:                                                 *
 *                                                                         *
 *    g++ -std=c++20 -O2 -I src/klusters/src \                             *
 *        -I src/libklustersshared/src/klustersshared \                    *
 *        src/klusters/tests/selectiongrid2d_test.cpp -o sg_test && ./sg_test
 *                                                                         *
 *  For many random polygons over random points, asserts the grid query    *
 *  returns EXACTLY the brute-force point-in-polygon result (no false       *
 *  positives or negatives), then demonstrates the pruning speedup.        *
 ***************************************************************************/
#include "selectiongrid2d.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <set>
#include <vector>

using Spike = SelectionGrid2D::Spike;

static int g_fail = 0;

int main()
{
    std::mt19937_64 rng(2024);
    std::uniform_real_distribution<float> coord(-1000.f, 1000.f);

    // ---- equivalence vs brute force ----------------------------------------
    {
        const std::size_t N = 20000;
        std::vector<float> xs(N), ys(N);
        std::vector<Spike> ids(N);
        for (std::size_t i = 0; i < N; ++i) {
            xs[i] = coord(rng);
            ys[i] = coord(rng);
            ids[i] = Spike(i);
        }
        SelectionGrid2D grid;
        grid.build(xs, ys, ids);

        const int POLYS = 300;
        for (int p = 0; p < POLYS; ++p) {
            // random polygon: 3..8 vertices
            const int v = 3 + int(rng() % 6);
            std::vector<float> px(v), py(v);
            for (int k = 0; k < v; ++k) { px[k] = coord(rng); py[k] = coord(rng); }

            // brute force with the SAME predicate the grid uses
            std::set<Spike> bf;
            for (std::size_t i = 0; i < N; ++i)
                if (SelectionGrid2D::pointInPolygon(xs[i], ys[i], px, py))
                    bf.insert(ids[i]);

            std::vector<Spike> got = grid.query(px, py);
            std::set<Spike> gs(got.begin(), got.end());

            if (gs.size() != got.size()) {
                std::printf("FAIL: grid returned duplicates (poly %d)\n", p);
                ++g_fail;
            }
            if (gs != bf) {
                std::printf("FAIL: grid != brute force (poly %d): grid=%zu bf=%zu\n",
                            p, gs.size(), bf.size());
                ++g_fail;
                break;
            }
        }
        std::printf("equivalence: %d polygons over %zu points -> %s\n", POLYS, N,
                    g_fail == 0 ? "OK" : "FAILED");
    }

    // ---- degenerate inputs -------------------------------------------------
    {
        SelectionGrid2D g;
        std::vector<float> xs(100, 5.0f), ys(100, 7.0f); // all identical
        std::vector<Spike> ids(100);
        for (std::size_t i = 0; i < 100; ++i) ids[i] = Spike(i);
        g.build(xs, ys, ids);
        std::vector<float> px{0, 10, 10, 0}, py{0, 0, 10, 10}; // square around (5,7)
        if (g.query(px, py).size() != 100) { std::printf("FAIL: degenerate all-in\n"); ++g_fail; }
        std::vector<float> px2{100, 110, 105}, py2{100, 100, 110}; // far away
        if (!g.query(px2, py2).empty()) { std::printf("FAIL: degenerate none-in\n"); ++g_fail; }
        SelectionGrid2D empty;
        empty.build({}, {}, {});
        if (!empty.query(px, py).empty()) { std::printf("FAIL: empty grid\n"); ++g_fail; }
        std::printf("degenerate inputs -> %s\n", g_fail == 0 ? "OK" : "FAILED");
    }

    // ---- pruning speedup vs a full O(N) scan -------------------------------
    {
        const std::size_t N = 2'000'000;
        std::vector<float> xs(N), ys(N);
        std::vector<Spike> ids(N);
        for (std::size_t i = 0; i < N; ++i) { xs[i] = coord(rng); ys[i] = coord(rng); ids[i] = Spike(i); }

        SelectionGrid2D grid;
        auto tb0 = std::chrono::high_resolution_clock::now();
        grid.build(xs, ys, ids);
        auto tb1 = std::chrono::high_resolution_clock::now();

        // small lasso: a ~40x40 box (0.04% of the 2000x2000 extent)
        std::vector<float> px{-20, 20, 20, -20}, py{-20, -20, 20, 20};

        auto tg0 = std::chrono::high_resolution_clock::now();
        auto sel = grid.query(px, py);
        auto tg1 = std::chrono::high_resolution_clock::now();

        std::size_t bf = 0;
        auto tf0 = std::chrono::high_resolution_clock::now();
        for (std::size_t i = 0; i < N; ++i)
            if (SelectionGrid2D::pointInPolygon(xs[i], ys[i], px, py)) ++bf;
        auto tf1 = std::chrono::high_resolution_clock::now();

        if (sel.size() != bf) { std::printf("FAIL: perf-case count mismatch %zu vs %zu\n", sel.size(), bf); ++g_fail; }

        const double buildMs = std::chrono::duration<double, std::milli>(tb1 - tb0).count();
        const double gridMs  = std::chrono::duration<double, std::milli>(tg1 - tg0).count();
        const double fullMs  = std::chrono::duration<double, std::milli>(tf1 - tf0).count();
        std::printf("perf @ %zu pts: build %.1f ms (once/projection), small lasso "
                    "grid %.3f ms vs full scan %.3f ms (%.0fx), selected %zu\n",
                    N, buildMs, gridMs, fullMs, fullMs / (gridMs > 0 ? gridMs : 1), sel.size());
    }

    std::printf("%s\n", g_fail == 0 ? "ALL PASS" : "SOME FAILED");
    return g_fail == 0 ? 0 : 1;
}
