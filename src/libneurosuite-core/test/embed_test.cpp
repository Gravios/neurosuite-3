// embed_test.cpp — core/tsne_embed.hpp and core/oblique_embed.hpp on point
// clouds whose structure is known: separated blobs must stay separated, a
// shuffled-label control must not, and the oblique projection must put each
// cluster at its own unit coordinate and report collinearity in the condition
// number.
#include <neurosuite/core/oblique_embed.hpp>
#include <neurosuite/core/tsne_embed.hpp>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace ne = neurosuite::embed;

static int fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++fails; }
}

// Fraction of points whose nearest 2-D neighbour has the same label.
static double knnPurity(const std::vector<double>& xy, const std::vector<int>& lab) {
    const int n = static_cast<int>(lab.size());
    int same = 0;
    for (int i = 0; i < n; ++i) {
        double best = 1e300; int bj = -1;
        for (int j = 0; j < n; ++j) {
            if (j == i) continue;
            const double dx = xy[2 * i] - xy[2 * j], dy = xy[2 * i + 1] - xy[2 * j + 1];
            const double d = dx * dx + dy * dy;
            if (d < best) { best = d; bj = j; }
        }
        same += (lab[bj] == lab[i]);
    }
    return static_cast<double>(same) / n;
}

int main() {
#ifdef _OPENMP
    // One thread: t-SNE repeats bit-identically only then (CORE_API C23), and
    // the checks below should not depend on scheduling.
    omp_set_num_threads(1);
#endif
    std::mt19937 g(3);
    std::normal_distribution<double> N(0.0, 1.0);

    // ── t-SNE ──────────────────────────────────────────────────────────────
    {
        const int n = 600, D = 10, k = 3;
        std::vector<double> X(static_cast<size_t>(n) * D);
        std::vector<int> lab(n);
        for (int i = 0; i < n; ++i) {
            lab[i] = i % k;
            for (int d = 0; d < D; ++d) X[static_cast<size_t>(i) * D + d] = N(g) + (d == lab[i] ? 8.0 : 0.0);
        }
        ne::TsneParams p;
        p.nIter = 300;
        std::vector<double> xy;
        std::string err;
        check(ne::tsneEmbed2D(X, n, D, xy, p, {}, nullptr, &err) && xy.size() == 2u * n, "t-SNE runs: " + err);
        const double pur = knnPurity(xy, lab);
        check(pur > 0.95, "t-SNE keeps three separated blobs apart (kNN purity " + std::to_string(pur) + ")");

        std::vector<int> shuffled(lab);
        std::shuffle(shuffled.begin(), shuffled.end(), g);
        const double ctrl = knnPurity(xy, shuffled);
        check(ctrl < 0.5, "shuffled labels: purity near chance (" + std::to_string(ctrl) + ")");

        std::vector<double> a, b;
        ne::tsneEmbed2D(X, n, D, a, p);
        ne::tsneEmbed2D(X, n, D, b, p);
        check(a == b, "the same seed gives the same embedding (one thread)");

        std::atomic<bool> cancel{true};
        std::vector<double> c;
        std::string cerr;
        check(!ne::tsneEmbed2D(X, n, D, c, p, {}, &cancel, &cerr) && cerr == "cancelled",
              "a set cancel flag aborts with \"cancelled\" (got \"" + cerr + "\")");

        std::vector<double> none;
        check(!ne::tsneEmbed2D(std::vector<double>(5), 5, 2, none, p), "inconsistent size: refused");
    }

    // ── oblique projection ─────────────────────────────────────────────────
    {
        const int n = 400, D = 6;
        auto run = [&](double cosTheta, std::vector<double>& xy, double& cond, std::vector<int>& lab) {
            // Two clusters along templates t1 = e0, t2 = cos e0 + sin e1, plus
            // a third, far "background" cluster so the global centre is not
            // between the two (as in a real group).
            const double s = std::sqrt(1.0 - cosTheta * cosTheta);
            std::vector<double> X(static_cast<size_t>(n) * D);
            lab.assign(n, 0);
            for (int i = 0; i < n; ++i) {
                lab[i] = i % 3;
                for (int d = 0; d < D; ++d) X[static_cast<size_t>(i) * D + d] = 0.05 * N(g);
                double* x = &X[static_cast<size_t>(i) * D];
                if (lab[i] == 0) { x[0] += 10.0; }
                if (lab[i] == 1) { x[0] += 10.0 * cosTheta; x[1] += 10.0 * s; }
            }
            std::vector<double> mean(D, 0.0), inv(D, 1.0);
            std::string e;
            return ne::obliqueProject(X, n, D, lab, {0, 1}, mean, inv, xy, cond, &e);
        };
        std::vector<double> xy; double cond = 0; std::vector<int> lab;
        check(run(0.0, xy, cond, lab), "oblique: orthogonal templates project");
        check(cond > 0.5 && cond < 2.0, "orthogonal templates: condition number ~1 (" + std::to_string(cond) + ")");
        // Cluster 0 sits at (1, 0) and cluster 1 at (0, 1) in template coordinates.
        double e0 = 0, e1 = 0; int n0 = 0, n1 = 0;
        for (int i = 0; i < n; ++i) {
            if (lab[i] == 0) { e0 += std::hypot(xy[2 * i] - 1.0, xy[2 * i + 1]); ++n0; }
            if (lab[i] == 1) { e1 += std::hypot(xy[2 * i], xy[2 * i + 1] - 1.0); ++n1; }
        }
        check(e0 / n0 < 0.05 && e1 / n1 < 0.05, "each cluster lands on its own unit coordinate");

        std::vector<double> xy2; double cond2 = 0;
        check(run(0.999, xy2, cond2, lab), "oblique: nearly collinear templates project");
        check(cond2 > 100.0, "nearly collinear templates: large condition number (" + std::to_string(cond2) + ")");

        std::vector<double> bad; double c3 = 0; std::string e;
        check(!ne::obliqueProject(std::vector<double>(D * 4, 1.0), 4, D, {0, 0, 0, 0}, {0, 7},
                                  std::vector<double>(D, 0.0), std::vector<double>(D, 1.0), bad, c3, &e)
                  && !e.empty(),
              "a basis cluster with no points is refused with a message");
    }

    if (fails) { std::printf("%d check(s) failed\n", fails); return 1; }
    std::printf("embed: all checks passed\n");
    return 0;
}
