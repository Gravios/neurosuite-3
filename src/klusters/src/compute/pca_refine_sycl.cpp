/***************************************************************************
 * pca_refine_sycl.cpp
 *
 * SYCL (Intel oneAPI) backend for the PCA-projection-energy refine pass
 * (interface in pca_refine_gpu.h).  The CUDA kernel (pca_refine_cuda.cu) in
 * SYCL 2020: one work-group of BLOCK_SIZE work-items per spike, the
 * candidates one after the other, the argmax in local memory.
 *
 * One difference: CUDA adds each (channel, component) score² into the
 * candidate's energy with a shared-memory atomicAdd; here each work-item sums
 * its own pairs and the work-group adds the partial sums in a tree.  The
 * order of the additions differs, so an energy can differ in its last bits
 * and a near-tie between two shifts may go the other way.
 *
 * All float: it runs on GPUs without double precision (Intel Arc).
 * Compiled with icpx -fsycl (oneAPI 2024+).
 ***************************************************************************/

#include "pca_refine_gpu.h"

#include <sycl/sycl.hpp>

#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>

namespace {

constexpr int BLOCK_SIZE = 256;

/** The GPU queue, made once; null when there is no GPU. */
sycl::queue* refineQueue()
{
    static std::unique_ptr<sycl::queue> q;
    static bool tried = false;
    if (!tried) {
        tried = true;
        try {
            q = std::make_unique<sycl::queue>(sycl::gpu_selector_v);
        } catch (const sycl::exception&) {
            q.reset();
        }
    }
    return q.get();
}

} // namespace

extern "C" int sycl_pca_refine_available()
{
    return refineQueue() ? 1 : 0;
}

extern "C" int sycl_pca_refine(
    int K, int M, int wideLen, int nSamp, int nChan, int chForPca,
    int kComp, int d2u, int rShift, int maxShift,
    int centered, int useStder,
    const int16_t* rawWindowsCM,
    const float*   pcaEvec,
    const float*   pcaMeans,
    int*           bestShifts)
{
    (void)nSamp;
    if (K <= 0 || M <= 0 || nChan <= 0 || chForPca <= 0 || kComp <= 0 || d2u <= 0)
        return -1;
    if (!rawWindowsCM || !pcaEvec || !bestShifts) return -1;
    if (centered && !pcaMeans) return -1;
    if (chForPca > nChan) chForPca = nChan;
    if (nChan > BLOCK_SIZE) return -2;   // one work-group sums the channels
    // The widest candidate (c = M-1) reads rShift+d2u samples from c: past the
    // window, it would read beyond the buffer (pca_refine_cuda.cu says why the
    // CPU path is taken instead).
    if (wideLen < M + rShift + d2u - 1) return -2;
    sycl::queue* qp = refineQueue();
    if (!qp) return -3;
    sycl::queue& q = *qp;

    const int neededSamples = rShift + d2u;
    // Local memory: energy[M] + the candidate's samples + sdPrev + three
    // BLOCK_SIZE scratch arrays.  A window that needs more than the device has
    // goes to the CPU path.
    const size_t localBytes = static_cast<size_t>(M) * sizeof(float)
                            + static_cast<size_t>(chForPca) * neededSamples * sizeof(int16_t)
                            + static_cast<size_t>(chForPca) * sizeof(int16_t)
                            + static_cast<size_t>(BLOCK_SIZE) * (sizeof(int) + sizeof(float) + sizeof(int));
    try {
        const size_t localMax = q.get_device().get_info<sycl::info::device::local_mem_size>();
        if (localBytes > localMax) return -2;
        const size_t wgMax = q.get_device().get_info<sycl::info::device::max_work_group_size>();
        if (wgMax < static_cast<size_t>(BLOCK_SIZE)) return -2;
    } catch (const sycl::exception&) {
        return -3;
    }

    const size_t raw_n   = static_cast<size_t>(K) * nChan * wideLen;
    const size_t evec_n  = static_cast<size_t>(chForPca) * kComp * d2u;
    const size_t means_n = static_cast<size_t>(chForPca) * d2u;

    int16_t* d_raw   = nullptr;
    float*   d_evec  = nullptr;
    float*   d_means = nullptr;
    int*     d_best  = nullptr;
    auto cleanup = [&]() {
        if (d_raw)   sycl::free(d_raw, q);
        if (d_evec)  sycl::free(d_evec, q);
        if (d_means) sycl::free(d_means, q);
        if (d_best)  sycl::free(d_best, q);
    };

    try {
        d_raw  = sycl::malloc_device<int16_t>(raw_n, q);
        d_evec = sycl::malloc_device<float>(evec_n, q);
        if (centered) d_means = sycl::malloc_device<float>(means_n, q);
        d_best = sycl::malloc_device<int>(static_cast<size_t>(K), q);
        if (!d_raw || !d_evec || (centered && !d_means) || !d_best) { cleanup(); return -3; }

        q.memcpy(d_raw, rawWindowsCM, raw_n * sizeof(int16_t));
        q.memcpy(d_evec, pcaEvec, evec_n * sizeof(float));
        if (centered) q.memcpy(d_means, pcaMeans, means_n * sizeof(float));
        q.wait_and_throw();

        q.submit([&](sycl::handler& h) {
            sycl::local_accessor<float, 1>   energy(sycl::range<1>(static_cast<size_t>(M)), h);
            sycl::local_accessor<int16_t, 1> row(sycl::range<1>(static_cast<size_t>(chForPca) * neededSamples), h);
            sycl::local_accessor<int16_t, 1> sdPrev(sycl::range<1>(static_cast<size_t>(chForPca)), h);
            sycl::local_accessor<int, 1>     s_int(sycl::range<1>(BLOCK_SIZE), h);
            sycl::local_accessor<float, 1>   s_flt(sycl::range<1>(BLOCK_SIZE), h);
            sycl::local_accessor<int, 1>     s_idx(sycl::range<1>(BLOCK_SIZE), h);
            const int16_t* raw = d_raw; const float* evec = d_evec; const float* means = d_means; int* best = d_best;
            const int nM = M, nW = wideLen, nC = nChan, nCP = chForPca, nK = kComp, nU = d2u, rS = rShift,
                      mS = maxShift, stder = useStder, cen = centered, need = neededSamples;
            h.parallel_for(
                sycl::nd_range<1>(sycl::range<1>(static_cast<size_t>(K) * BLOCK_SIZE), sycl::range<1>(BLOCK_SIZE)),
                [=](sycl::nd_item<1> it) {
                    const int spike = static_cast<int>(it.get_group(0));
                    const int tid   = static_cast<int>(it.get_local_id(0));
                    const auto grp  = it.get_group();
                    const long long spikeBase = static_cast<long long>(spike) * nC * nW;

                    for (int c = tid; c < nM; c += BLOCK_SIZE) energy[c] = 0.0f;
                    sycl::group_barrier(grp);

                    for (int c = 0; c < nM; ++c) {
                        if (stder) {
                            if (tid < nCP) sdPrev[tid] = 0;
                            sycl::group_barrier(grp);
                        }
                        // Phase 1 — the stderived (or raw) samples the PCA reads.
                        for (int t = 0; t < need; ++t) {
                            int rawV = 0;
                            if (tid < nC) rawV = static_cast<int>(raw[spikeBase + static_cast<long long>(tid) * nW + c + t]);
                            if (stder) {
                                s_int[tid] = (tid < nC) ? rawV : 0;
                                sycl::group_barrier(grp);
                                for (int stride = BLOCK_SIZE / 2; stride > 0; stride >>= 1) {
                                    if (tid < stride) s_int[tid] += s_int[tid + stride];
                                    sycl::group_barrier(grp);
                                }
                                const int totalSum = s_int[0];
                                if (tid < nCP) {
                                    const int sd = nC * rawV - totalSum;
                                    const int sdCl = sd > 32767 ? 32767 : (sd < -32768 ? -32768 : sd);
                                    const int prev = static_cast<int>(sdPrev[tid]);
                                    int diff = sdCl - prev;
                                    if (diff > 32767)  diff = 32767;
                                    if (diff < -32768) diff = -32768;
                                    sdPrev[tid] = static_cast<int16_t>(sdCl);
                                    row[static_cast<size_t>(tid) * need + t] = static_cast<int16_t>(diff);
                                }
                            } else if (tid < nCP) {
                                row[static_cast<size_t>(tid) * need + t] = static_cast<int16_t>(rawV);
                            }
                            // (s_int[0] is read above before the next sample overwrites s_int)
                            sycl::group_barrier(grp);
                        }

                        // Phase 2 — the projection energy: each work-item its pairs, then the tree.
                        float partial = 0.0f;
                        for (int idx = tid; idx < nCP * nK; idx += BLOCK_SIZE) {
                            const int ch = idx / nK;
                            const int k  = idx % nK;
                            const size_t rowAt = static_cast<size_t>(ch) * need + rS;
                            const float* ev = evec + static_cast<long long>(ch) * nK * nU + static_cast<long long>(k) * nU;
                            float score = 0.0f;
                            for (int u = 0; u < nU; ++u) {
                                float xv = static_cast<float>(row[rowAt + u]);
                                if (cen) xv -= means[static_cast<long long>(ch) * nU + u];
                                score += ev[u] * xv;
                            }
                            partial += score * score;
                        }
                        s_flt[tid] = partial;
                        sycl::group_barrier(grp);
                        for (int stride = BLOCK_SIZE / 2; stride > 0; stride >>= 1) {
                            if (tid < stride) s_flt[tid] += s_flt[tid + stride];
                            sycl::group_barrier(grp);
                        }
                        if (tid == 0) energy[c] = s_flt[0];
                        sycl::group_barrier(grp);
                    }

                    // Argmax over the candidates (the first of equal energies, as in CUDA's tree).
                    float lb = -std::numeric_limits<float>::infinity();
                    int   li = -1;
                    for (int c = tid; c < nM; c += BLOCK_SIZE) {
                        const float e = energy[c];
                        if (e > lb) { lb = e; li = c; }
                    }
                    s_flt[tid] = lb;
                    s_idx[tid] = li;
                    sycl::group_barrier(grp);
                    for (int stride = BLOCK_SIZE / 2; stride > 0; stride >>= 1) {
                        if (tid < stride && s_flt[tid + stride] > s_flt[tid]) {
                            s_flt[tid] = s_flt[tid + stride];
                            s_idx[tid] = s_idx[tid + stride];
                        }
                        sycl::group_barrier(grp);
                    }
                    if (tid == 0) best[spike] = s_idx[0] - mS;
                });
        });
        q.wait_and_throw();
        q.memcpy(bestShifts, d_best, static_cast<size_t>(K) * sizeof(int)).wait_and_throw();
    } catch (const sycl::exception& e) {
        fprintf(stderr, "[klusters] pca_refine_sycl: %s\n", e.what());
        cleanup();
        return -5;
    }

    cleanup();
    return 0;
}
