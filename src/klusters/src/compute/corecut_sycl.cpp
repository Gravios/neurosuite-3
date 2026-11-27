/***************************************************************************
 * corecut_sycl.cpp
 *
 * SYCL (Intel oneAPI) backend for the ordering loop of the core cut
 * (corecut.h, step 4; interface in corecut_gpu.h).  The CUDA kernel
 * (corecut_cuda.cu) in SYCL 2020: one work-group of CC_BLOCK work-items per
 * CC_BLOCK spikes, one launch per spike taken, the per-group minima copied
 * back and reduced on the host.  Keep the two the same.
 *
 * Double precision: the running products and the keys are double, as on
 * the CPU and in CUDA, so that the same spikes are taken.  A device without
 * fp64 (the Arc integrated GPUs of Meteor Lake, Intel's Xe-LPG) is reported
 * as unavailable and the OpenMP loop runs.
 *
 * Compiled with icpx -fsycl (oneAPI 2024+).
 ***************************************************************************/

#include "corecut_gpu.h"

#include <sycl/sycl.hpp>

#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

namespace {

constexpr int CC_BLOCK = 256;

/** The GPU queue, made once; null when there is no usable GPU. */
sycl::queue* corecutQueue()
{
    static std::unique_ptr<sycl::queue> q;
    static bool tried = false;
    if (!tried) {
        tried = true;
        try {
            auto made = std::make_unique<sycl::queue>(sycl::gpu_selector_v);
            if (made->get_device().has(sycl::aspect::fp64)) q = std::move(made);
        } catch (const sycl::exception&) {
            q.reset();
        }
    }
    return q.get();
}

} // namespace

extern "C" int sycl_corecut_available()
{
    return corecutQueue() ? 1 : 0;
}

extern "C" int sycl_corecut_order(long n, int d, const float* x, long seed, long* order, double* step)
{
    if (n <= 1 || d <= 0 || !x || !order || !step || seed < 0 || seed >= n) return -1;
    if (n > 0x7fffffffL / 2) return -2;                    // the kernel indexes spikes with int
    sycl::queue* qp = corecutQueue();
    if (!qp) return -3;
    sycl::queue& q = *qp;

    const int    nI     = static_cast<int>(n);
    const int    blocks = (nI + CC_BLOCK - 1) / CC_BLOCK;

    // Host: |x_i|^2 and the column-major copy the device reads.
    std::vector<double> hx2(static_cast<size_t>(n));
    std::vector<float>  hxt(static_cast<size_t>(n) * d);
    for (long i = 0; i < n; ++i) {
        const float* xi = x + static_cast<size_t>(i) * d;
        double s = 0.0;
        for (int u = 0; u < d; ++u) {
            s += static_cast<double>(xi[u]) * xi[u];
            hxt[static_cast<size_t>(u) * n + i] = xi[u];
        }
        hx2[static_cast<size_t>(i)] = s;
    }

    float*         d_x     = nullptr;
    double*        d_x2    = nullptr;
    double*        d_xm    = nullptr;
    unsigned char* d_taken = nullptr;
    double*        d_bkey  = nullptr;
    int*           d_bidx  = nullptr;
    auto cleanup = [&]() {
        if (d_x)     sycl::free(d_x, q);
        if (d_x2)    sycl::free(d_x2, q);
        if (d_xm)    sycl::free(d_xm, q);
        if (d_taken) sycl::free(d_taken, q);
        if (d_bkey)  sycl::free(d_bkey, q);
        if (d_bidx)  sycl::free(d_bidx, q);
    };

    try {
        d_x     = sycl::malloc_device<float>(static_cast<size_t>(n) * d, q);
        d_x2    = sycl::malloc_device<double>(static_cast<size_t>(n), q);
        d_xm    = sycl::malloc_device<double>(static_cast<size_t>(n), q);
        d_taken = sycl::malloc_device<unsigned char>(static_cast<size_t>(n), q);
        d_bkey  = sycl::malloc_device<double>(static_cast<size_t>(blocks), q);
        d_bidx  = sycl::malloc_device<int>(static_cast<size_t>(blocks), q);
        if (!d_x || !d_x2 || !d_xm || !d_taken || !d_bkey || !d_bidx) { cleanup(); return -3; }

        q.memcpy(d_x, hxt.data(), static_cast<size_t>(n) * d * sizeof(float));
        q.memcpy(d_x2, hx2.data(), static_cast<size_t>(n) * sizeof(double));
        q.memset(d_xm, 0, static_cast<size_t>(n) * sizeof(double));
        q.memset(d_taken, 0, static_cast<size_t>(n));
        q.wait_and_throw();
        hxt.clear();
        hxt.shrink_to_fit();

        std::vector<double> m(static_cast<size_t>(d));
        for (int u = 0; u < d; ++u) m[static_cast<size_t>(u)] = x[static_cast<size_t>(seed) * d + u];
        std::vector<double> hkey(static_cast<size_t>(blocks));
        std::vector<int>    hidx(static_cast<size_t>(blocks));

        order[0] = seed;
        step[0]  = 0.0;
        long j = seed;
        const sycl::nd_range<1> range{sycl::range<1>(static_cast<size_t>(blocks) * CC_BLOCK), sycl::range<1>(CC_BLOCK)};
        for (long k = 1; k < n; ++k) {
            const double inv = 1.0 / static_cast<double>(k);
            const int    jj  = static_cast<int>(j);
            q.submit([&](sycl::handler& h) {
                sycl::local_accessor<double, 1> s_key(sycl::range<1>(CC_BLOCK), h);
                sycl::local_accessor<int, 1>    s_idx(sycl::range<1>(CC_BLOCK), h);
                const float* xs = d_x; const double* x2 = d_x2; double* xm = d_xm;
                unsigned char* taken = d_taken; double* outKey = d_bkey; int* outIdx = d_bidx;
                const int dd = d;
                h.parallel_for(range, [=](sycl::nd_item<1> it) {
                    const int tid = static_cast<int>(it.get_local_id(0));
                    const int i   = static_cast<int>(it.get_global_id(0));
                    double key = std::numeric_limits<double>::infinity();
                    int    idx = -1;
                    if (i < nI) {
                        float dot = 0.0f;
                        for (int u = 0; u < dd; ++u)
                            dot += xs[static_cast<size_t>(u) * nI + i] * xs[static_cast<size_t>(u) * nI + jj];
                        double v = xm[i];
                        v += (static_cast<double>(dot) - v) * inv;
                        xm[i] = v;
                        if (i == jj) taken[i] = 1;
                        if (!taken[i]) { key = x2[i] - 2.0 * v; idx = i; }
                    }
                    s_key[tid] = key;
                    s_idx[tid] = idx;
                    sycl::group_barrier(it.get_group());
                    for (int stride = CC_BLOCK / 2; stride > 0; stride >>= 1) {
                        if (tid < stride) {
                            const double ko = s_key[tid + stride];
                            const int    io = s_idx[tid + stride];
                            // smaller key wins; equal keys: the lower index (io >= 0 guards the empty slot)
                            if (io >= 0 && (s_idx[tid] < 0 || ko < s_key[tid] || (ko == s_key[tid] && io < s_idx[tid]))) {
                                s_key[tid] = ko;
                                s_idx[tid] = io;
                            }
                        }
                        sycl::group_barrier(it.get_group());
                    }
                    if (tid == 0) {
                        const size_t g = it.get_group(0);
                        outKey[g] = s_key[0];
                        outIdx[g] = s_idx[0];
                    }
                });
            });
            q.memcpy(hkey.data(), d_bkey, static_cast<size_t>(blocks) * sizeof(double));
            q.memcpy(hidx.data(), d_bidx, static_cast<size_t>(blocks) * sizeof(int));
            q.wait_and_throw();

            long   best = -1;
            double bestKey = 0.0;
            for (int b = 0; b < blocks; ++b) {
                const int ib = hidx[static_cast<size_t>(b)];
                if (ib < 0) continue;
                if (best < 0 || hkey[static_cast<size_t>(b)] < bestKey) { best = ib; bestKey = hkey[static_cast<size_t>(b)]; }   // blocks ascend: ties keep the lower index
            }
            if (best < 0) { cleanup(); return -7; }

            // m with k spikes in it (j, taken last, now added), for the step size.
            if (k > 1) {
                const float* xj = x + static_cast<size_t>(j) * d;
                for (int u = 0; u < d; ++u) m[static_cast<size_t>(u)] += (static_cast<double>(xj[u]) - m[static_cast<size_t>(u)]) * inv;
            }
            double mm = 0.0;
            for (int u = 0; u < d; ++u) mm += m[static_cast<size_t>(u)] * m[static_cast<size_t>(u)];
            const double d2 = bestKey + mm;
            order[k] = best;
            step[k]  = std::sqrt(d2 > 0.0 ? d2 : 0.0);
            j = best;
        }
    } catch (const sycl::exception& e) {
        fprintf(stderr, "[klusters] corecut_sycl: %s\n", e.what());
        cleanup();
        return -5;
    }

    cleanup();
    return 0;
}
