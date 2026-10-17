// watershed2d_test.cpp — core/watershed2d.hpp on point clouds with a known
// number of density basins.
#include <neurosuite/core/watershed2d.hpp>

#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace ws = neurosuite::watershed2d;

static int fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++fails; }
}

// k Gaussian blobs (sd 1) with centres `gap` apart along x, n points each.
static void blobs(int k, int n, double gap, std::vector<double>& xs, std::vector<double>& ys,
                  unsigned seed) {
    std::mt19937 g(seed);
    std::normal_distribution<double> N(0.0, 1.0);
    xs.clear(); ys.clear();
    for (int c = 0; c < k; ++c)
        for (int i = 0; i < n; ++i) { xs.push_back(N(g) + c * gap); ys.push_back(N(g)); }
}

int main() {
    std::vector<double> xs, ys;

    for (int k = 1; k <= 3; ++k) {
        blobs(k, 5000, 10.0, xs, ys, 10u + static_cast<unsigned>(k));
        const ws::Result r = ws::run(xs, ys);
        check(r.ok, std::to_string(k) + " blob(s): ok");
        check(r.numBasins == k, std::to_string(k) + " blob(s): " + std::to_string(r.numBasins) + " basins");
        check(r.pointLabels.size() == xs.size(), "one label per point");
        // Each blob lands in one basin, and different blobs in different basins.
        std::set<int> seen;
        bool pure = true;
        for (int c = 0; c < k; ++c) {
            std::vector<int> count(static_cast<size_t>(r.numBasins) + 1, 0);
            for (int i = 0; i < 5000; ++i) ++count[static_cast<size_t>(r.pointLabels[static_cast<size_t>(c * 5000 + i)])];
            int best = 0;
            for (int b = 1; b <= r.numBasins; ++b) if (count[b] > count[best]) best = b;
            pure = pure && best > 0 && count[best] > 4900 && seen.insert(best).second;
        }
        check(pure, std::to_string(k) + " blob(s): each blob is one basin of its own");
    }

    {   // keepGrid fills the preview grid and its cell labels
        blobs(2, 3000, 10.0, xs, ys, 20);
        ws::Config cfg;
        cfg.gridSize = 64;
        cfg.keepGrid = true;
        const ws::Result r = ws::run(xs, ys, cfg);
        check(r.gridSize == 64 && r.smoothedGrid.size() == 64u * 64u && r.cellLabels.size() == 64u * 64u,
              "keepGrid: a 64x64 grid and its labels");
        check(r.effSigma > 0 && r.effPeakHeight > 0 && r.effMinBasinSize > 0,
              "the effective (auto-tuned) parameters are reported");
    }

    {   // degenerate input
        const ws::Result same = ws::run(std::vector<double>(100, 1.0), std::vector<double>(100, 2.0));
        check(!same.ok, "all points at one place: not ok");
        const ws::Result empty = ws::run({}, {});
        check(!empty.ok && empty.pointLabels.empty(), "no points: not ok, no labels");
    }

    if (fails) { std::printf("%d check(s) failed\n", fails); return 1; }
    std::printf("watershed2d: all checks passed\n");
    return 0;
}
