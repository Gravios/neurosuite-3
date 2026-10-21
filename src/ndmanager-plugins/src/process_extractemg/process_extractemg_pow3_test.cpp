/***************************************************************************
    process_extractemg_pow3_test.cpp
    --------------------------------
    The FastICA pow3 kernels built into this test (always the OpenMP CPU
    kernel; the Apple Accelerate one on macOS) against a direct evaluation
    of their numerical contract:

        newW[c] = ( Σ_t Z[t,c] * (Σ_c' Z[t,c']*w[c'])^3 ) / N  -  3 * w[c]

    and a timing line per kernel.  Registered as ndm_test_extractemg_pow3.

        process_extractemg_pow3_test                 check (N 100000, nch 32)
        process_extractemg_pow3_test --bench N NCH   also time N x NCH, e.g.
                                                     --bench 20000000 64 on a
                                                     Mac to decide whether
                                                     --backend auto should
                                                     take Accelerate.

    Exits nonzero on failure.

    copyright  (C) 2026 neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "process_extractemg_gpu.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *name, double got = 0, double want = 0)
{
    if (ok) std::printf("  PASS  %s\n", name);
    else { std::printf("  FAIL  %s   (got %.6g, want %.6g)\n", name, got, want); ++failures; }
}

struct Problem {
    std::vector<double> Z, w;
    int n = 0, nch = 0;
};

Problem makeProblem(int n, int nch, unsigned seed)
{
    Problem p;
    p.n = n;
    p.nch = nch;
    std::mt19937 rng(seed);
    std::normal_distribution<double> g(0.0, 1.0);
    p.Z.resize(static_cast<std::size_t>(n) * nch);
    for (double &v : p.Z) v = g(rng);
    p.w.resize(static_cast<std::size_t>(nch));
    double norm = 0.0;
    for (double &v : p.w) { v = g(rng); norm += v * v; }
    for (double &v : p.w) v /= std::sqrt(norm);
    return p;
}

std::vector<double> direct(const Problem &p)
{
    std::vector<double> out(static_cast<std::size_t>(p.nch), 0.0);
    for (int t = 0; t < p.n; ++t) {
        const double *row = p.Z.data() + static_cast<std::size_t>(t) * p.nch;
        double s = 0.0;
        for (int c = 0; c < p.nch; ++c) s += row[c] * p.w[c];
        const double y = s * s * s;
        for (int c = 0; c < p.nch; ++c) out[c] += row[c] * y;
    }
    for (int c = 0; c < p.nch; ++c) out[c] = out[c] / p.n - 3.0 * p.w[c];
    return out;
}

// Milliseconds per step(), over enough steps to take about half a second.
double timeSteps(Pow3Kernel &k, const Problem &p)
{
    std::vector<double> out(static_cast<std::size_t>(p.nch));
    k.step(p.w.data(), out.data());                       // warm-up
    int reps = 0;
    const auto t0 = std::chrono::steady_clock::now();
    double elapsed = 0.0;
    do {
        k.step(p.w.data(), out.data());
        ++reps;
        elapsed = std::chrono::duration<double, std::milli>(
                      std::chrono::steady_clock::now() - t0).count();
    } while (elapsed < 500.0 && reps < 1000);
    return elapsed / reps;
}

} // namespace

int main(int argc, char **argv)
{
    int benchN = 0, benchNch = 0;
    if (argc == 4 && std::strcmp(argv[1], "--bench") == 0) {
        benchN = std::atoi(argv[2]);
        benchNch = std::atoi(argv[3]);
    } else if (argc != 1) {
        std::fprintf(stderr, "usage: %s [--bench N NCH]\n", argv[0]);
        return 2;
    }

    std::vector<IcaBackend> backends{IcaBackend::Cpu};
    if (icaBackendAvailable(IcaBackend::Accelerate)) backends.push_back(IcaBackend::Accelerate);
    std::printf("  ....  pow3 kernels built: cpu, accelerate %s\n",
                icaBackendAvailable(IcaBackend::Accelerate) ? "yes" : "no");

    // Correctness, including a sample count that is not a multiple of any
    // block size and a channel count that is not a multiple of a SIMD width.
    for (const auto &[n, nch] : {std::pair{100000, 32}, std::pair{12345, 7}}) {
        const Problem p = makeProblem(n, nch, 11u);
        const std::vector<double> want = direct(p);
        double scale = 0.0;
        for (double v : want) scale = std::max(scale, std::abs(v));
        for (IcaBackend b : backends) {
            std::unique_ptr<Pow3Kernel> k = makePow3Kernel(b);
            char name[128];
            std::snprintf(name, sizeof name, "%s kernel is the one asked for", icaBackendName(b));
            check(k->backend() == b, name, static_cast<int>(k->backend()), static_cast<int>(b));
            std::vector<double> got(static_cast<std::size_t>(nch));
            const bool ok = k->prepare(p.Z, nch, n) && k->step(p.w.data(), got.data());
            double err = ok ? 0.0 : 1.0;
            for (int c = 0; c < nch; ++c) err = std::max(err, std::abs(got[c] - want[c]) / scale);
            std::snprintf(name, sizeof name, "%s step matches the direct sum (N %d, nch %d)",
                          icaBackendName(b), n, nch);
            check(err < 1e-10, name, err, 0);
            k->destroy();
        }
    }

    // Timing (not a check).
    const int tn = benchN > 0 ? benchN : 100000;
    const int tnch = benchNch > 0 ? benchNch : 32;
    const Problem p = makeProblem(tn, tnch, 5u);
    for (IcaBackend b : backends) {
        std::unique_ptr<Pow3Kernel> k = makePow3Kernel(b);
        if (!k->prepare(p.Z, tnch, tn)) continue;
        std::printf("  ....  timing  %-10s N %d nch %d  %10.3f ms per step\n",
                    icaBackendName(b), tn, tnch, timeSteps(*k, p));
        k->destroy();
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
