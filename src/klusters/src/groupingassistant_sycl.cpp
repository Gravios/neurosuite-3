/***************************************************************************
 * groupingassistant_sycl.cpp
 *
 * SYCL (Intel oneAPI DPC++) backend for the errormatrix probability
 * computation. Targets Intel Arc GPUs via Level Zero, but will also run on
 * any SYCL-capable device (NVIDIA via CUDA backend, AMD via HIP backend of
 * the oneAPI compiler, or CPU as a last resort).
 *
 * Compiled with:  icpx -fsycl  (Intel oneAPI 2024+)
 *
 * Device selection priority (automatic):
 *   1. gpu_selector_v  — any GPU (Arc preferred on Intel systems)
 *   2. cpu_selector_v  — SYCL CPU device (fallback, slower than OpenMP)
 ***************************************************************************/

#include "groupingassistant_gpu.h"

#include <sycl/sycl.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <stdexcept>

using namespace sycl;

// ---------------------------------------------------------------------------
// Helper: forward substitution squared norm  (device lambda)
// Solves L*x = b (L lower-triangular, col-major 0-based) and returns ||x||^2.
// dim must be <= 64.
// ---------------------------------------------------------------------------
static inline double forwardSubstituteSq(
    const double* L, const double* b, int dim) noexcept
{
    double x[64];
    double sq = 0.0;
    for (int i = 0; i < dim; ++i) {
        double s = b[i];
        for (int j = 0; j < i; ++j)
            s -= L[i + j * dim] * x[j];
        x[i] = s / L[i + i * dim];
        sq  += x[i] * x[i];
    }
    return sq;
}

// ---------------------------------------------------------------------------
// Probe: return 1 if a GPU SYCL device is visible, 0 otherwise.
// ---------------------------------------------------------------------------
extern "C" int sycl_device_available()
{
    try {
        // gpu_selector_v throws if no GPU device found.
        queue q(gpu_selector_v);
        (void)q;
        return 1;
    } catch (const exception&) {
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Main computation
// ---------------------------------------------------------------------------
extern "C" int sycl_compute_probabilities(
    const double* features,
    const double* choleskyAll,
    const double* means,
    const double* logTerms,
    double*       probOut,
    const int*    ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col)
{
    try {
        // Select GPU; fall back to any device if no dedicated GPU.
        queue q;
        try {
            q = queue(gpu_selector_v, property::queue::in_order{});
        } catch (const exception&) {
            q = queue(default_selector_v, property::queue::in_order{});
        }

        const size_t featSz  = static_cast<size_t>(nbSpikes)   * nbDim;
        const size_t cholSz  = static_cast<size_t>(nbClusters) * nbDim * nbDim;
        const size_t meansSz = static_cast<size_t>(nbClusters) * nbDim;
        const size_t probSz  = static_cast<size_t>(nbSpikes)   * nbClusters;

        // Allocate USM (Unified Shared Memory) — simpler than explicit buffers
        // and avoids a manual copy-back step on Intel Arc (shares system RAM).
        double* d_feat  = malloc_device<double>(featSz,  q);
        double* d_chol  = malloc_device<double>(cholSz,  q);
        double* d_means = malloc_device<double>(meansSz, q);
        double* d_log   = malloc_device<double>(static_cast<size_t>(nbClusters), q);
        double* d_prob  = malloc_device<double>(probSz,  q);
        int*    d_ign   = malloc_device<int>   (static_cast<size_t>(nbClusters), q);

        if (!d_feat || !d_chol || !d_means || !d_log || !d_prob || !d_ign)
            throw std::runtime_error("SYCL USM allocation failed");

        // Copy inputs to device (in_order queue so these are sequential).
        q.memcpy(d_feat,  features,    featSz  * sizeof(double));
        q.memcpy(d_chol,  choleskyAll, cholSz  * sizeof(double));
        q.memcpy(d_means, means,       meansSz * sizeof(double));
        q.memcpy(d_log,   logTerms,    static_cast<size_t>(nbClusters) * sizeof(double));
        q.memcpy(d_prob,  probOut,     probSz  * sizeof(double));
        q.memcpy(d_ign,   ignoreFlags, static_cast<size_t>(nbClusters) * sizeof(int));
        q.wait();

        // ------------------------------------------------------------------
        // Kernel 1: Mahalanobis distances → raw probabilities
        // nd_range: global = (nbClusters, nbSpikes), local = (1, 256)
        // ------------------------------------------------------------------
        const size_t LSIZE = 256;
        const size_t gSpikes   = (static_cast<size_t>(nbSpikes) + LSIZE - 1) / LSIZE * LSIZE;

        q.submit([&](handler& h) {
            // Capture by value so the lambda is device-safe.
            auto feat  = d_feat;
            auto chol  = d_chol;
            auto mu    = d_means;
            auto logT  = d_log;
            auto prob  = d_prob;
            auto ign   = d_ign;
            int  nS    = nbSpikes;
            int  nC    = nbClusters;
            int  nD    = nbDim;

            h.parallel_for(
                nd_range<2>(range<2>(nC, gSpikes), range<2>(1, LSIZE)),
                [=](nd_item<2> item) {
                    int cluster = static_cast<int>(item.get_global_id(0));
                    int spike   = static_cast<int>(item.get_global_id(1));
                    if (spike >= nS || cluster >= nC) return;
                    if (ign[cluster]) return;

                    const double* L  = chol + cluster * nD * nD;
                    const double* m  = mu   + cluster * nD;
                    const double* x  = feat + spike   * nD;

                    double b[64];
                    for (int d = 0; d < nD; ++d) b[d] = x[d] - m[d];

                    double mahal = forwardSubstituteSq(L, b, nD);
                    prob[spike * nC + cluster] =
                        sycl::exp(-0.5 * (mahal + logT[cluster]));
                });
        });

        // ------------------------------------------------------------------
        // Kernel 2: row-wise normalization
        // ------------------------------------------------------------------
        q.submit([&](handler& h) {
            auto prob = d_prob;
            int  nS   = nbSpikes;
            int  nC   = nbClusters;
            int  c1   = cluster1Col;

            h.parallel_for(range<1>(gSpikes), [=](id<1> idx) {
                int spike = static_cast<int>(idx[0]);
                if (spike >= nS) return;

                double* row = prob + spike * nC;
                double sum = 0.0;
                for (int c = 0; c < nC; ++c) sum += row[c];
                if (sum == 0.0) { sum = 1.0; row[c1] = 1.0; }
                double inv = 1.0 / sum;
                for (int c = 0; c < nC; ++c) row[c] *= inv;
            });
        });

        q.wait();

        // Copy result back.
        q.memcpy(probOut, d_prob, probSz * sizeof(double));
        q.wait();

        free(d_feat,  q); free(d_chol,  q); free(d_means, q);
        free(d_log,   q); free(d_prob,  q); free(d_ign,   q);
        return 0;

    } catch (const std::exception& e) {
        fprintf(stderr, "SYCL error: %s\n", e.what());
        return -1;
    }
}
