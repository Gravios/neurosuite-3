// fiber_stage_kk.cpp — KKK integration of the (audited) fiber method.
// New per-chunk Stage 2.12, after the Stage 2.11 harvest, before cross-chunk
// Phase 5/6.  Consolidates the over-split fragments — which are clean
// energy-slice samples — into fibers via energy-local trajectory + whiteness
// residual, the curve representation the mean-template merge provably lacks
// (see audit).  Gated by -FiberStageEnable (default 0).
//
// The numerical core (fiber_stage.h) is audited to machine precision against
// the validated Python on chunk_g5_min183-193 (100% identical labels, 96.28%).
// The NEW glue is the fragment->fiber grouping (trajectoryCoherenceMerge); it
// is the piece the fil-vs-stderiv comparison is meant to validate.
//
// Inputs are gathered internally, mirroring KKK's own .fil re-extraction
// (KK_chunked.cpp WritePhase15Checkpoint): GroupChannelIds for the group's
// channel map into the .fil, and <FileBase>.res.<ElecNo> for absolute spike
// samples (excised from the off-spike baseline).  The whitener reads the FULL
// chunk .fil span and transforms it whole before sampling baseline windows,
// matching the Python whole-trace transform exactly (memory is not a
// constraint on the target box).
#include "KK.h"
#include "KlustaKwik.h"
#include "fiber_stage.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── off-spike whitener for one chunk, in the masked spkD feature space ──────
// Reads the FULL [s0,s1) .fil span for the group's channels, applies the spkD
// transform (Δt(ALLPAIRS)) to the whole span (so first-differences are correct
// at every window — matching Python's fil_to_spkD_space on the whole trace),
// excises ±guard around each spike, samples nBase clean masked windows, and
// builds the Ledoit-Wolf whitener.  Returns false if inputs are unavailable.
static bool fiberChunkWhitener(const char* filPath, int nTotalCh,
                               const std::vector<int>& groupChannels,
                               long s0, long s1,
                               const std::vector<long>& spikeAbsSample,
                               int nsamp, int masklo, int maskhi,
                               std::vector<double>& W, std::vector<double>& nmean,
                               int& p, int nBase = 6000, int guard = 24)
{
    const int nch = (int)groupChannels.size();
    if (nch <= 0 || nTotalCh <= 0) return false;
    long T = s1 - s0; if (T < nsamp + 2) return false;
    FILE* f = std::fopen(filPath, "rb"); if (!f) return false;
    std::vector<double> buf((size_t)T * nch);
    std::vector<int16_t> row(nTotalCh);
    fseeko(f, (off_t)s0 * nTotalCh * (off_t)sizeof(int16_t), SEEK_SET);
    long got_t = 0;
    for (long t = 0; t < T; ++t) {
        if (std::fread(row.data(), sizeof(int16_t), nTotalCh, f) != (size_t)nTotalCh) break;  // short read → truncate span
        for (int c = 0; c < nch; ++c) { int gc = groupChannels[c];
            buf[(size_t)t * nch + c] = (gc >= 0 && gc < nTotalCh) ? (double)row[gc] : 0.0; }
        ++got_t;
    }
    std::fclose(f);
    T = got_t; if (T < nsamp + 2) return false;
    // ALLPAIRS in place:  T1[t,c] = nch*x[t,c] - Σ_c x[t,c]
    for (long t = 0; t < T; ++t) { double s = 0; double* r = &buf[(size_t)t * nch];
        for (int c = 0; c < nch; ++c) s += r[c];
        for (int c = 0; c < nch; ++c) r[c] = nch * r[c] - s; }
    // temporal first-difference in place (backwards), T2[0]=0
    for (long t = T - 1; t >= 1; --t) { double* r = &buf[(size_t)t * nch]; const double* pr = &buf[(size_t)(t-1) * nch];
        for (int c = 0; c < nch; ++c) r[c] -= pr[c]; }
    for (int c = 0; c < nch; ++c) buf[c] = 0.0;
    // forbidden mask over [s0,s0+T)
    std::vector<char> forb(T, 0);
    for (long r : spikeAbsSample) { long c = r - s0;
        for (long t = std::max(0L, c - guard); t < std::min(T, c + guard); ++t) forb[t] = 1; }
    // sample clean masked windows → baseline matrix bm (got × p)
    p = (maskhi - masklo) * nch;
    std::vector<double> bm; bm.reserve((size_t)nBase * p);
    unsigned long long rng = 0x9E3779B97F4A7C15ULL;          // deterministic LCG (RNG-independent of numpy)
    auto nextR = [&](long mod) { rng = rng * 6364136223846793005ULL + 1442695040888963407ULL; return (long)((rng >> 33) % mod); };
    int got = 0; long tries = 0;
    while (got < nBase && tries < 50L * nBase) {
        long s = nextR(T - nsamp); ++tries; bool bad = false;
        for (int t = 0; t < nsamp && !bad; ++t) if (forb[s + t]) bad = true;
        if (bad) continue;
        for (int t = masklo; t < maskhi; ++t) for (int c = 0; c < nch; ++c)
            bm.push_back(buf[(size_t)(s + t) * nch + c]);
        ++got;
    }
    if (got < p + 2) return false;
    fiberstage::whitener_from_baseline(bm, got, p, W, nmean);
    return true;
}

// ── trajectory-coherence merge: group fragments whose trajectories are co-linear ──
// Union-find over fragment pairs whose predicted unit directions stay within
// `angleDeg` across their overlapping radius range — the curve analog of the
// mean-template cosine merge, but on the trajectory, so it closes a fiber the
// 0.98 cosine gate cannot.  Returns a remap frag→fiber (compacted ids).
static std::vector<int> trajectoryCoherenceMerge(
    const std::vector<fiberstage::Traj>& trajs, double angleDeg = 20.0, int nProbe = 12)
{
    const int K = (int)trajs.size();
    std::vector<int> fib(K); for (int i = 0; i < K; ++i) fib[i] = i;
    std::function<int(int)> find = [&](int x) { while (fib[x] != x) { fib[x] = fib[fib[x]]; x = fib[x]; } return x; };
    const double cosThr = std::cos(angleDeg * M_PI / 180.0);
    std::vector<double> a, b;
    for (int i = 0; i < K; ++i) for (int j = i + 1; j < K; ++j) {
        if (trajs[i].ng == 0 || trajs[j].ng == 0) continue;
        double lo = std::max(trajs[i].grid.front(), trajs[j].grid.front());
        double hi = std::min(trajs[i].grid.back(),  trajs[j].grid.back());
        if (hi <= lo) continue;
        double acc = 0; int n = 0;
        for (int s = 0; s < nProbe; ++s) { double r = lo + (hi - lo) * s / (nProbe - 1);
            fiberstage::predict(trajs[i], r, a); fiberstage::predict(trajs[j], r, b);
            double d = 0; for (size_t q = 0; q < a.size(); ++q) d += a[q] * b[q]; acc += d; ++n; }
        if (n && acc / n >= cosThr) { int ri = find(i), rj = find(j); if (ri != rj) fib[rj] = ri; }
    }
    std::map<int,int> id; std::vector<int> out(K);
    for (int i = 0; i < K; ++i) { int r = find(i); auto it = id.find(r);
        if (it == id.end()) { int n = (int)id.size(); id[r] = n; out[i] = n; } else out[i] = it->second; }
    return out;
}

// ── Stage 2.12 entry — signature matches the other per-chunk ops ────────────
void KK::FiberStagePerChunk(
    const std::vector<std::vector<int>>&        chunkPoints,
    std::vector<std::vector<int>>&              perChunkClass,
    std::vector<std::vector<ChunkModel>>&       perChunkModels,
    int                                         /*nFullDims*/,
    const char*                                 stageTag)
{
    const int nChunks = (int)chunkPoints.size();
    const int nsamp = NbSamplesPerSpike, nch = NbChannels, wElems = nsamp * nch;
    const int masklo = 11, maskhi = 26;                      // MASK_FULL (validated WIDE window)
    if (GroupChannelIds.empty() || NbTotalChannels <= 0 || nch <= 0 || nsamp <= 0) {
        Output("[%s] FiberStage: missing .fil channel map / dims — skipping\n", stageTag);
        return;
    }

    char spkPath[STRLEN + 16]; pickInputPath(spkPath, sizeof(spkPath), FileBase, "spk", ElecNo); // → .spkD
    char filPath[STRLEN + 16]; std::snprintf(filPath, sizeof(filPath), "%s.fil", FileBase);
    char resPath[STRLEN + 16]; std::snprintf(resPath, sizeof(resPath), "%s.res.%d", FileBase, ElecNo);
    FILE* resF = std::fopen(resPath, "rb");
    if (!resF) { Output("[%s] FiberStage: %s unavailable — skipping\n", stageTag, resPath); return; }

    for (int c = 0; c < nChunks; ++c) {
        const auto& pts = chunkPoints[c];
        const int N = (int)pts.size();
        if (N < 50) continue;

        // 1) absolute spike samples for this chunk (from .res, by global id) + span
        std::vector<long> abs(N); long s0 = (1L<<62), s1 = 0;
        for (int i = 0; i < N; ++i) {
            int64_t ts = 0; fseeko(resF, (off_t)pts[i] * (off_t)sizeof(int64_t), SEEK_SET);
            if (std::fread(&ts, sizeof(int64_t), 1, resF) != 1) ts = 0;
            abs[i] = (long)ts; if (abs[i] < s0) s0 = abs[i]; if (abs[i] > s1) s1 = abs[i];
        }
        s0 = std::max(0L, s0 - nsamp); s1 = s1 + nsamp + 1;

        // 2) read this chunk's .spkD waveforms verbatim (sample-major == fiber core layout)
        std::vector<double> waves((size_t)N * wElems);
        { FILE* sp = std::fopen(spkPath, "rb"); if (!sp) continue;
          std::vector<int16_t> r(wElems);
          for (int i = 0; i < N; ++i) {
              fseeko(sp, (off_t)pts[i] * wElems * (off_t)sizeof(int16_t), SEEK_SET);
              if (std::fread(r.data(), sizeof(int16_t), wElems, sp) != (size_t)wElems) for (int e=0;e<wElems;++e) r[e]=0;
              for (int e = 0; e < wElems; ++e) waves[(size_t)i * wElems + e] = (double)r[e];
          }
          std::fclose(sp);
        }

        // 3) per-chunk off-spike whitener (full .fil span, validated feature space)
        std::vector<double> W, nmean; int p = 0;
        if (!fiberChunkWhitener(filPath, NbTotalChannels, GroupChannelIds, s0, s1, abs,
                                nsamp, masklo, maskhi, W, nmean, p)) continue;

        // 4) seed from current fragments; one trajectory per fragment (per-fragment realign)
        std::map<int,int> fragOf; std::vector<int> fragId(N);
        for (int i = 0; i < N; ++i) { int lab = perChunkClass[c][i];
            auto it = fragOf.find(lab); if (it == fragOf.end()) { int n=(int)fragOf.size(); fragOf[lab]=n; fragId[i]=n; } else fragId[i]=it->second; }
        const int nFrag = (int)fragOf.size();
        if (nFrag < 2) continue;
        std::vector<fiberstage::Traj> trajs(nFrag);
        for (int g = 0; g < nFrag; ++g) {
            std::vector<int> idx; for (int i = 0; i < N; ++i) if (fragId[i] == g) idx.push_back(i);
            if ((int)idx.size() < 25) { trajs[g] = fiberstage::Traj(); continue; }
            std::vector<double> gw((size_t)idx.size() * wElems);
            for (size_t i = 0; i < idx.size(); ++i) for (int e = 0; e < wElems; ++e) gw[i*wElems+e] = waves[(size_t)idx[i]*wElems+e];
            fiberstage::realign(gw, (int)idx.size(), nsamp, nch);
            std::vector<double> Xg; fiberstage::mask_whiten(gw, (int)idx.size(), nsamp, nch, masklo, maskhi, nmean, W, p, Xg);
            std::vector<int> id((int)idx.size()); for (size_t i = 0; i < idx.size(); ++i) id[i] = (int)i;
            trajs[g] = fiberstage::trajectory(Xg, id, p);
        }

        // 5) trajectory-coherence merge: fragments → provisional fibers (the new glue)
        std::vector<int> fragToFiber = trajectoryCoherenceMerge(trajs, /*angleDeg=*/20.0);
        int nFib = 0; for (int v : fragToFiber) nFib = std::max(nFib, v + 1);
        if (nFib < 1) continue;
        std::vector<int> seed(N); for (int i = 0; i < N; ++i) seed[i] = fragToFiber[fragId[i]];

        // 6) audited consolidation: per-fiber realign + whiteness assign + calibrated posterior
        fiberstage::Result R = fiberstage::consolidate(waves, N, nsamp, nch, masklo, maskhi, W, nmean, p, seed, nFib);

        // 7) write consolidated labels + rebuild ChunkModel meanWav (channel-major)
        for (int i = 0; i < N; ++i) perChunkClass[c][i] = R.hard[i] + 1;     // 0 reserved (noise)
        perChunkModels[c].clear();
        for (int fib = 0; fib < nFib; ++fib) {
            ChunkModel m; m.chunkIdx = c; m.localClusterId = fib + 1; m.globalClusterId = -1;
            std::vector<long long> acc((size_t)wElems, 0); int cnt = 0;
            for (int i = 0; i < N; ++i) if (R.hard[i] == fib) {
                const double* w = &waves[(size_t)i * wElems];                // sample-major
                for (int s = 0; s < nsamp; ++s) for (int ch = 0; ch < nch; ++ch)
                    acc[(size_t)ch * nsamp + s] += (long long)std::lround(w[(size_t)s * nch + ch]); // → channel-major
                ++cnt;
            }
            m.nMembers = cnt; m.meanWav.assign((size_t)wElems, 0);
            if (cnt) for (int e = 0; e < wElems; ++e) m.meanWav[e] = (int16_t)(acc[e] / cnt);
            perChunkModels[c].push_back(std::move(m));
        }
        if (Verbose >= 1) {
            double cs = 0; for (float v : R.conf) cs += v;
            Output("[%s] chunk %d: %d fragments → %d fibers (mean conf %.2f)\n",
                   stageTag, c, nFrag, nFib, R.conf.empty() ? 0.0 : cs / R.conf.size());
        }
    }
    std::fclose(resF);
}

// ── standalone fiber-clustering branch: config + parallel + cross-chunk ─────
#include <unordered_map>
#include <string>
#include "fiber_gpu.h"
#ifdef _OPENMP
#include <omp.h>
#endif

extern int   FiberStandaloneEnable;
extern float FiberMSKappa;            // mean-shift angular kernel concentration
extern float FiberMSDrFrac;           // in-band radius window = frac*(p99-p1)
extern float FiberMergeAngleDeg;      // trajectory-coherence merge threshold (deg)
extern int   FiberMSSeeds;            // # random seeds for ridge mean-shift
extern int   FiberMinGroupSize;       // min spikes per provisional group / fiber
extern float ChunkMinutes;            // chunk size (min)
extern float ChunkOverlapMinutes;     // overlap (min) -> cross-chunk anchors
extern int   FiberXChunkEnable;       // link fibers across chunks (drift tracking)
extern int   FiberXChunkSubspaceDim;  // L: shared population subspace dim
extern int   FiberXChunkNKnots;       // energy knots for the warp field R(r)
extern int   FiberXChunkMinAnchors;   // min overlap anchor pairs to fit R(r)
extern float FiberXChunkGateRatio;    // accept link iff best < ratio*second
extern float FiberXChunkSmooth;       // neighbour-knot pooling weight for R(r)
extern int   MaxPossibleClusters;     // hard cap on cluster-id space (SaveOutput)
extern int   FiberThreads;            // OpenMP threads for chunk loop (0 = default)
extern int   FiberGPUEnable;          // 1 = try GPU kernels; falls back to CPU

// per-chunk result; cross-chunk descriptors are in UNWHITENED masked space so
// they are comparable across chunks (which have different per-chunk whiteners).
struct ChunkFiberResult {
    std::vector<int>                 extId;   // global spike ids in the extended set
    std::vector<int>                 extLab;  // local fiber label per extended spike (-1=none)
    int                              nFib = 0;
    std::vector<std::vector<double>> knot;    // [nFib] -> NKnots*p unit dirs (unwhitened masked)
    bool                             ok = false;
};
static int ufFind(std::vector<int>& uf, int x){ while(uf[x]!=x){ uf[x]=uf[uf[x]]; x=uf[x]; } return x; }

#ifdef USE_CUDA
// GPU whiteness-assignment callback for consolidate(); gated, returns non-zero
// to trigger the CPU fallback inside consolidate().
static int fiberAssignGPU(const double* X,int N,int p,const double* grids,const int* gridLen,const int* gridOff,
                          const double* Ds,const int* DOff,int nfib,double* res,int* hard){
    if(!(FiberGPUEnable && p<=FIBER_GPU_MAXP && N>=256 && fiber_gpu_available())) return -1;
    return fiber_gpu_assign(X,N,p,grids,gridLen,gridOff,Ds,DOff,nfib,res,hard);
}
#endif

// ── per-chunk fiber clustering (thread-safe: opens its own file handles) ─────
// GPU offload targets: meanshift_inband (seed x support matmuls) and the
// whiteness assignment inside consolidate(); a CUDA backend replaces those,
// dispatched by FiberGPUEnable, per the realign_xcorr / groupingassistant pattern.
static ChunkFiberResult clusterChunkFibers(
        const std::vector<int>& extId, const std::vector<long>& absS,
        const std::string& spkPath, const std::string& filPath,
        int nTotalCh, const std::vector<int>& gch,
        int nsamp, int nch, int masklo, int maskhi, int p, int nKnots)
{
    ChunkFiberResult R; R.extId = extId; const int N = (int)extId.size();
    R.extLab.assign(N, -1);
    if (N < FiberMinGroupSize * 2) return R;
    const int wElems = nsamp * nch;
    std::vector<double> waves((size_t)N * wElems);
    { FILE* sp = std::fopen(spkPath.c_str(), "rb"); if(!sp) return R; std::vector<int16_t> row(wElems);
      for(int i=0;i<N;i++){ fseeko(sp,(off_t)extId[i]*wElems*2,SEEK_SET);
          if(std::fread(row.data(),2,wElems,sp)!=(size_t)wElems) for(int e=0;e<wElems;++e) row[e]=0;
          for(int e=0;e<wElems;++e) waves[(size_t)i*wElems+e]=(double)row[e]; } std::fclose(sp); }
    long s0=(1L<<62),s1=0; std::vector<long> a2(N);
    for(int i=0;i<N;i++){ a2[i]=absS[extId[i]]; s0=std::min(s0,a2[i]); s1=std::max(s1,a2[i]); }
    s0=std::max(0L,s0-nsamp); s1=s1+nsamp+1;
    std::vector<double> W,nm; int pp=0;
    if(!fiberChunkWhitener(filPath.c_str(),nTotalCh,gch,s0,s1,a2,nsamp,masklo,maskhi,W,nm,pp)) return R;
    std::vector<double> wg(waves); fiberstage::realign(wg,N,nsamp,nch);
    std::vector<double> X; fiberstage::mask_whiten(wg,N,nsamp,nch,masklo,maskhi,nm,W,p,X);
    std::vector<double> Xu((size_t)N*p);   // unwhitened masked (frame-independent)
    for(int i=0;i<N;i++){ const double* w=&wg[(size_t)i*wElems]; int j=0;
        for(int t=masklo;t<maskhi;++t) for(int c=0;c<nch;++c) Xu[(size_t)i*p+(j++)]=w[t*nch+c]; }
    std::vector<double> rad(N), dir((size_t)N*p);
    for(int i=0;i<N;i++){ const double* Xi=&X[(size_t)i*p]; double nn=0; for(int j=0;j<p;j++) nn+=Xi[j]*Xi[j]; nn=std::sqrt(nn);
        rad[i]=nn; for(int j=0;j<p;j++) dir[(size_t)i*p+j]=Xi[j]/(nn+1e-12); }
    std::vector<double> rsort(rad); std::sort(rsort.begin(),rsort.end());
    double dr=FiberMSDrFrac*(rsort[(int)(0.99*(N-1))]-rsort[(int)(0.01*(N-1))]);
    int nsup=std::min(N,20000), S=std::min(N,(int)FiberMSSeeds);
    std::vector<double> dsup((size_t)nsup*p),rsup(nsup),ds((size_t)S*p),rs(S);
    for(int j=0;j<nsup;j++){ int idx=(int)((long)j*N/nsup); rsup[j]=rad[idx]; for(int k=0;k<p;k++) dsup[(size_t)j*p+k]=dir[(size_t)idx*p+k]; }
    for(int s=0;s<S;s++){ int idx=(int)((long)s*N/S); rs[s]=rad[idx]; for(int k=0;k<p;k++) ds[(size_t)s*p+k]=dir[(size_t)idx*p+k]; }
    bool gpuDone=false;
#ifdef USE_CUDA
    if(FiberGPUEnable && p<=FIBER_GPU_MAXP && S>=256 && fiber_gpu_available())
        gpuDone = (fiber_gpu_meanshift(dsup.data(),rsup.data(),nsup,p,ds.data(),rs.data(),S,FiberMSKappa,dr,15)==0);
#endif
    if(!gpuDone)
        fiberstage::meanshift_inband(dsup.data(),rsup.data(),nsup,p,ds.data(),rs.data(),S,FiberMSKappa,dr,15);
    std::vector<double> cd,cr; fiberstage::dedupe_centers(ds.data(),rs.data(),S,p,8.0,0.12,cd,cr);
    const int M=(int)cr.size(); if(M<1) return R;
    std::vector<int> lab(N);
    for(int i=0;i<N;i++){ const double* di=&dir[(size_t)i*p]; double best=-2; int bk=0;
        for(int k=0;k<M;k++){ const double* ck=&cd[(size_t)k*p]; double cs=0; for(int j=0;j<p;j++) cs+=di[j]*ck[j]; if(cs>best){best=cs;bk=k;} } lab[i]=bk; }
    // Keep only SUBSTANTIAL centers (>= FiberMinGroupSize spikes).  The mean-shift
    // drops many seeds along each fiber's energy curve and on noise, so most dedup
    // centers are tiny fragments (median ~12 spikes); emitting them is the
    // over-segmentation that produced ~700 "fibers"/chunk.  Reassign every spike to
    // its nearest substantial center, then consolidate over those.
    std::vector<int> csize(M,0); for(int i=0;i<N;i++) csize[lab[i]]++;
    std::vector<int> keep; for(int k=0;k<M;k++) if(csize[k]>=FiberMinGroupSize) keep.push_back(k);
    if(keep.empty()) return R;
    const int Msub=(int)keep.size();
    std::vector<double> scd((size_t)Msub*p);
    for(int m=0;m<Msub;m++) for(int j=0;j<p;j++) scd[(size_t)m*p+j]=cd[(size_t)keep[m]*p+j];
    std::vector<int> lab2(N);
    for(int i=0;i<N;i++){ const double* di=&dir[(size_t)i*p]; double best=-2; int bk=0;
        for(int m=0;m<Msub;m++){ const double* ck=&scd[(size_t)m*p]; double cs=0; for(int j=0;j<p;j++) cs+=di[j]*ck[j]; if(cs>best){best=cs;bk=m;} } lab2[i]=bk; }
    std::vector<fiberstage::Traj> trajs(Msub);
    for(int k=0;k<Msub;k++){ std::vector<int> idx; for(int i=0;i<N;i++) if(lab2[i]==k) idx.push_back(i);
        if((int)idx.size()<FiberMinGroupSize){ trajs[k]=fiberstage::Traj(); continue; } trajs[k]=fiberstage::trajectory(X,idx,p); }
    std::vector<int> fmap=trajectoryCoherenceMerge(trajs,FiberMergeAngleDeg);
    int nFib=0; for(int v:fmap) nFib=std::max(nFib,v+1); if(nFib<1) return R;
    std::vector<int> seed(N); for(int i=0;i<N;i++) seed[i]=fmap[lab2[i]];
    fiberstage::Result CR=fiberstage::consolidate(waves,N,nsamp,nch,masklo,maskhi,W,nm,p,seed,nFib,3
#ifdef USE_CUDA
        ,&fiberAssignGPU
#endif
        );
    R.nFib=nFib; for(int i=0;i<N;i++) R.extLab[i]=CR.hard[i];
    R.knot.assign(nFib,std::vector<double>()); std::vector<double> pr(p);
    for(int f=0;f<nFib;f++){
        std::vector<int> idx; for(int i=0;i<N;i++) if(CR.hard[i]==f) idx.push_back(i);
        R.knot[f].assign((size_t)nKnots*p,0.0);
        if((int)idx.size()<FiberMinGroupSize) continue;
        fiberstage::Traj tu=fiberstage::trajectory(Xu,idx,p);
        std::vector<double> en; en.reserve(idx.size());
        for(int i:idx){ const double* xu=&Xu[(size_t)i*p]; double nn=0; for(int j=0;j<p;j++) nn+=xu[j]*xu[j]; en.push_back(std::sqrt(nn)); }
        std::sort(en.begin(),en.end()); double e0=en[(int)(0.10*(en.size()-1))], e1=en[(int)(0.90*(en.size()-1))];
        for(int kn=0;kn<nKnots;kn++){ double e=e0+(e1-e0)*kn/std::max(1,nKnots-1); fiberstage::predict(tu,e,pr);
            for(int j=0;j<p;j++) R.knot[f][(size_t)kn*p+j]=pr[j]; }
    }
    R.ok=true; return R;
}

// ── cross-chunk fiber linking ───────────────────────────────────────────────
// Primary: one energy-dependent rotation field R(r) per adjacent pair, fit in a
// shared population subspace and applied to ALL fibers (population geometry
// generalises the warp).  Complementary: overlap spikes (same physical events in
// both chunks) give ground-truth f<->g anchor pairs used to fit R(r).
static void fiberXChunkLink(const std::vector<ChunkFiberResult>& C, int p, int nKnots,
                            std::vector<std::vector<int>>& globalId, int& nGlobalOut)
{
    const int nC=(int)C.size();
    std::vector<int> base(nC,0); int tot=0; for(int c=0;c<nC;c++){ base[c]=tot; tot+=C[c].nFib; }
    std::vector<int> uf(std::max(1,tot)); for(int i=0;i<tot;i++) uf[i]=i;
    const int L=std::max(2,std::min((int)FiberXChunkSubspaceDim,p));
    std::vector<double> rows; int nrow=0;
    for(const auto& cc:C) for(const auto& kv:cc.knot){ bool nz=false; for(double v:kv){ if(v!=0.0){nz=true;break;} } if(!nz) continue;
        for(int kn=0;kn<nKnots;kn++){ for(int j=0;j<p;j++) rows.push_back(kv[(size_t)kn*p+j]); nrow++; } }
    std::vector<double> basis,mean; if(nrow>=L) fiberstage::pca_basis(rows,nrow,p,L,basis,mean);
    std::vector<std::vector<std::vector<double>>> P(nC);
    for(int c=0;c<nC;c++){ P[c].resize(C[c].nFib);
        for(int f=0;f<C[c].nFib;f++){ const auto& kv=C[c].knot[f]; std::vector<double>& o=P[c][f];
            bool nz=false; for(double v:kv){ if(v!=0.0){nz=true;break;} }
            if(basis.empty()||!nz){ o.clear(); continue; }
            o.assign((size_t)nKnots*L,0.0); std::vector<double> t(L);
            for(int kn=0;kn<nKnots;kn++){ fiberstage::pca_project_unit(&kv[(size_t)kn*p],basis,mean,p,L,t.data());
                for(int m=0;m<L;m++) o[(size_t)kn*L+m]=t[m]; } } }
    for(int c=0;c+1<nC;c++){
        const int Ka=C[c].nFib, Kb=C[c+1].nFib; if(Ka<1||Kb<1) continue;
        std::unordered_map<int,int> labB;
        for(int i=0;i<(int)C[c+1].extId.size();i++) if(C[c+1].extLab[i]>=0) labB[C[c+1].extId[i]]=C[c+1].extLab[i];
        std::vector<std::unordered_map<int,int>> co(Ka);
        for(int i=0;i<(int)C[c].extId.size();i++){ int la=C[c].extLab[i]; if(la<0) continue;
            auto it=labB.find(C[c].extId[i]); if(it!=labB.end()) co[la][it->second]++; }
        std::vector<std::pair<int,int>> anchors;
        for(int fa=0;fa<Ka;fa++){ int bb=-1,bn=0; for(const auto& kv:co[fa]){ if(kv.second>bn){ bn=kv.second; bb=kv.first; } }
            if(bb>=0 && bn>=2) anchors.push_back(std::make_pair(fa,bb)); }
        const bool haveR = (int)anchors.size()>=FiberXChunkMinAnchors && !basis.empty();
        std::vector<std::vector<double>> Rk(nKnots);
        if(haveR) for(int t=0;t<nKnots;t++){
            std::vector<double> Mm((size_t)L*L,0.0);
            for(int dt=-1;dt<=1;dt++){ int tt=t+dt; if(tt<0||tt>=nKnots) continue; double wgt=(dt==0)?1.0:(double)FiberXChunkSmooth;
                for(const auto& an:anchors){ if(P[c][an.first].empty()||P[c+1][an.second].empty()) continue;
                    const double* a=&P[c][an.first][(size_t)tt*L]; const double* b=&P[c+1][an.second][(size_t)tt*L];
                    for(int x=0;x<L;x++) for(int y=0;y<L;y++) Mm[(size_t)x*L+y]+=wgt*b[x]*a[y]; } }
            fiberstage::procrustes_R(Mm,L,Rk[t]); }
        const double gate2=(double)FiberXChunkGateRatio*(double)FiberXChunkGateRatio;
        for(int fa=0;fa<Ka;fa++){ if(P[c][fa].empty()) continue;
            double best=1e300,second=1e300; int bb=-1;
            for(int fb=0;fb<Kb;fb++){ if(P[c+1][fb].empty()) continue; double dsum=0;
                for(int t=0;t<nKnots;t++){ const double* a=&P[c][fa][(size_t)t*L]; const double* b=&P[c+1][fb][(size_t)t*L];
                    if(haveR){ const std::vector<double>& Rt=Rk[t];
                        for(int x=0;x<L;x++){ double ax=0; for(int y=0;y<L;y++) ax+=Rt[(size_t)x*L+y]*a[y]; double d=ax-b[x]; dsum+=d*d; } }
                    else for(int x=0;x<L;x++){ double d=a[x]-b[x]; dsum+=d*d; } }
                if(dsum<best){ second=best; best=dsum; bb=fb; } else if(dsum<second) second=dsum; }
            if(bb>=0 && best<gate2*second){ int ra=ufFind(uf,base[c]+fa), rb=ufFind(uf,base[c+1]+bb); if(ra!=rb) uf[rb]=ra; } }
    }
    std::unordered_map<int,int> gmap; int ng=0;
    globalId.assign(nC,std::vector<int>()); for(int c=0;c<nC;c++) globalId[c].assign(C[c].nFib,-1);
    for(int c=0;c<nC;c++) for(int f=0;f<C[c].nFib;f++){ int rt=ufFind(uf,base[c]+f);
        auto it=gmap.find(rt); int id; if(it==gmap.end()){ id=ng++; gmap[rt]=id; } else id=it->second; globalId[c][f]=id; }
    nGlobalOut=ng;
}

float KK::RunFiberStandalone(const std::vector<float>& chunkBoundsSec, float samplingRate)
{
    const int nsamp=NbSamplesPerSpike, nch=NbChannels;
    const int masklo=11, maskhi=26, p=(maskhi-masklo)*nch;
    if(GroupChannelIds.empty()||NbTotalChannels<=0||nch<=0||nsamp<=0){ Output("[FiberStandalone] missing dims/channel map — abort\n"); return 0.0f; }
    char spk[STRLEN+16]; pickInputPath(spk,sizeof(spk),FileBase,"spk",ElecNo);
    char fil[STRLEN+16]; std::snprintf(fil,sizeof(fil),"%s.fil",FileBase);
    char rp[STRLEN+16];  std::snprintf(rp,sizeof(rp),"%s.res.%d",FileBase,ElecNo);
    std::string spkPath=spk, filPath=fil;
    FILE* rf=std::fopen(rp,"rb"); if(!rf){ Output("[FiberStandalone] %s unavailable\n",rp); return 0.0f; }
    std::vector<long> absS(nPoints);
    for(int i=0;i<nPoints;i++){ int64_t ts=0; fseeko(rf,(off_t)i*8,SEEK_SET); if(std::fread(&ts,8,1,rf)!=1) ts=0; absS[i]=(long)ts; }
    std::fclose(rf);
    const double sess=(double)timeRawMax-(double)timeRawMin;
    std::vector<float> bnd;
    if(chunkBoundsSec.size()>=2) bnd=chunkBoundsSec;
    else if(ChunkMinutes>0&&sess>0&&samplingRate>0){ double tot=sess/samplingRate; int nC=(int)std::ceil(tot/(ChunkMinutes*60.0)); if(nC<1)nC=1;
        for(int c=0;c<=nC;c++) bnd.push_back((float)(c*ChunkMinutes*60.0)); }
    else { bnd.push_back(0.0f); bnd.push_back((float)((sess>0?sess:1.0)/(samplingRate>0?samplingRate:1.0))); }
    const int nChunks=(int)bnd.size()-1;
    const double ovSec=(ChunkOverlapMinutes>0?(double)ChunkOverlapMinutes*60.0:0.0);
    std::vector<int> coreChunk(nPoints,0);
    std::vector<std::vector<int>> ext(nChunks);
    for(int i=0;i<nPoints;i++){ double ts=((double)absS[i]-(double)timeRawMin)/(samplingRate>0?samplingRate:1.0);
        int c=0; while(c<nChunks-1 && ts>=bnd[c+1]) ++c; coreChunk[i]=c; }
    for(int c=0;c<nChunks;c++){ double lo=bnd[c]-ovSec, hi=bnd[c+1]+ovSec;
        for(int i=0;i<nPoints;i++){ double ts=((double)absS[i]-(double)timeRawMin)/(samplingRate>0?samplingRate:1.0);
            if(ts>=lo&&ts<hi) ext[c].push_back(i); } }
    const int nKnots=std::max(3,(int)FiberXChunkNKnots);
#ifdef _OPENMP
    if(FiberThreads>0) omp_set_num_threads(FiberThreads);
#endif
    if(FiberGPUEnable){
#ifdef USE_CUDA
        Output(fiber_gpu_available()? "[FiberStandalone] CUDA mean-shift backend active\n"
                                    : "[FiberStandalone] GPU requested but no CUDA device — CPU\n");
#else
        Output("[FiberStandalone] GPU requested but USE_CUDA not built — CPU (OpenMP)\n");
#endif
    }
    std::vector<ChunkFiberResult> res(nChunks);
    #pragma omp parallel for schedule(dynamic)
    for(int c=0;c<nChunks;c++)
        res[c]=clusterChunkFibers(ext[c],absS,spkPath,filPath,NbTotalChannels,GroupChannelIds,nsamp,nch,masklo,maskhi,p,nKnots);
    for(int c=0;c<nChunks;c++) Output("[FiberStandalone] chunk %d/%d: %d spikes -> %d fibers%s\n",
        c+1,nChunks,(int)ext[c].size(),res[c].nFib,res[c].ok?"":" (skipped)");
    std::vector<int> coreLab(nPoints,-1);
    for(int c=0;c<nChunks;c++) for(int i=0;i<(int)res[c].extId.size();i++){ int g=res[c].extId[i];
        if(coreChunk[g]==c) coreLab[g]=res[c].extLab[i]; }
    std::vector<std::vector<int>> globalId; int nGlobal=0;
    if(FiberXChunkEnable && nChunks>1) fiberXChunkLink(res,p,nKnots,globalId,nGlobal);
    else { globalId.assign(nChunks,std::vector<int>()); int t=0;
        for(int c=0;c<nChunks;c++){ globalId[c].assign(res[c].nFib,0); for(int f=0;f<res[c].nFib;f++) globalId[c][f]=t++; } nGlobal=t; }
    // Cluster-id space: noise -> 0 (SaveOutput's noise bucket), fibers -> globalId+1.
    // SaveOutput hard-caps ids at MaxPossibleClusters and routes anything >= it to
    // noise, so a long session with many fibers must raise the cap or it silently
    // dumps every high-id fiber.  RunFiberStandalone owns its output, so size it here.
    if(nGlobal+1 > MaxPossibleClusters){
        Output("[FiberStandalone] %d fibers exceed MaxPossibleClusters=%d; raising cap to %d\n",
               nGlobal, MaxPossibleClusters, nGlobal+1);
        MaxPossibleClusters = nGlobal+1;
    }
    for(int i=0;i<nPoints;i++){ int c=coreChunk[i], l=coreLab[i];
        Class[i]=(l>=0 && l<(int)globalId[c].size() && globalId[c][l]>=0) ? globalId[c][l]+1 : 0; }
    nClustersAlive=nGlobal+1; nStartingClusters=nGlobal+1;
    Output("[FiberStandalone] done: %d global fibers across %d chunk(s)%s\n",
        nGlobal,nChunks,(FiberXChunkEnable&&nChunks>1)?" (cross-chunk linked)":"");
    return (float)nGlobal;
}
