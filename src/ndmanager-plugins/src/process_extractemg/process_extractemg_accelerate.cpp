/***************************************************************************
    process_extractemg_accelerate.cpp
    ---------------------------------
    Apple Accelerate (BLAS) implementation of the FastICA pow3 inner step,
    for macOS, where none of the GPU backends exists.  CPU-side: on Apple
    silicon Accelerate's BLAS runs on the matrix (AMX/SME) units.

    Numerical contract (identical to the CPU reference):
        y[t]    = ( Z @ w )[t]   = Σ_c Z[t,c] * w[c]      (length N)
        y[t]    = y[t]^3
        newW[c] = ( Z^T @ y )[c] / N   −   3 * w[c]       (length nch)

    Blocking
    --------
    The two gemvs each read all of Z (N × nch doubles, often gigabytes),
    and the step is limited by memory bandwidth.  The OpenMP CPU kernel
    reads Z once per step (one fused sweep); two whole-matrix gemvs would
    read it twice.  So Z is taken in row blocks small enough to stay in
    cache: for each block, y_b = Z_b @ w, cube, acc += Z_b^T @ y_b — two
    cblas_dgemv calls on the same cached block, so Z still crosses the
    memory bus once.  Blocks are spread over OpenMP threads, each with its
    own accumulator, summed at the end.

    Not chosen by --backend auto: whether this beats the OpenMP kernel on
    a given Mac is for a benchmark to show (extractemg_test_pow3 prints the
    timing of both); select it with --backend accelerate.

    copyright  (C) 2026 neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifdef USE_ACCELERATE

#include "process_extractemg_gpu.h"

// The current (macOS 13.3+) BLAS/LAPACK interface, not the deprecated one.
#define ACCELERATE_NEW_LAPACK
#include <Accelerate/Accelerate.h>

#include <algorithm>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#endif

namespace {

class Pow3KernelAccelerate final : public Pow3Kernel {
public:
    bool prepare(const std::vector<double> &Z, int channels, int samples) override
    {
        data.assign(Z.begin(), Z.end());
        nch = channels;
        n = samples;
        // About 256 KiB of Z per block (fits in L2 with room for y).
        blockRows = std::max(64, 32768 / std::max(1, nch));
        return true;
    }

    bool step(const double *w, double *newW) override
    {
        std::fill(newW, newW + nch, 0.0);
        const double *Z = data.data();
        const int nBlocks = (n + blockRows - 1) / blockRows;

        #pragma omp parallel
        {
            std::vector<double> acc(static_cast<std::size_t>(nch), 0.0);
            std::vector<double> y(static_cast<std::size_t>(blockRows));
            #pragma omp for schedule(static)
            for (int b = 0; b < nBlocks; ++b) {
                const int r0 = b * blockRows;
                const int rows = std::min(blockRows, n - r0);
                const double *Zb = Z + static_cast<std::size_t>(r0) * nch;
                // y = Z_b @ w   (Z_b is rows × nch, row-major)
                cblas_dgemv(CblasRowMajor, CblasNoTrans, rows, nch,
                            1.0, Zb, nch, w, 1, 0.0, y.data(), 1);
                for (int t = 0; t < rows; ++t) y[t] = y[t] * y[t] * y[t];
                // acc += Z_b^T @ y
                cblas_dgemv(CblasRowMajor, CblasTrans, rows, nch,
                            1.0, Zb, nch, y.data(), 1, 1.0, acc.data(), 1);
            }
            #pragma omp critical
            for (int c = 0; c < nch; ++c) newW[c] += acc[c];
        }

        const double invN = 1.0 / static_cast<double>(n);
        for (int c = 0; c < nch; ++c) newW[c] = newW[c] * invN - 3.0 * w[c];
        return true;
    }

    void destroy() override { data.clear(); data.shrink_to_fit(); }
    IcaBackend backend() const override { return IcaBackend::Accelerate; }

private:
    std::vector<double> data;
    int nch = 0;
    int n = 0;
    int blockRows = 64;
};

} // namespace

std::unique_ptr<Pow3Kernel> makePow3KernelAccelerate()
{
    return std::make_unique<Pow3KernelAccelerate>();
}

#endif // USE_ACCELERATE
