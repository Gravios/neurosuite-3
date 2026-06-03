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

// ── externs: standalone fiber-clustering configuration (KlustaKwik.cpp) ─────
extern int   FiberStandaloneEnable;
extern float FiberMSKappa;        // angular kernel concentration (von Mises-ish)
extern float FiberMSDrFrac;       // in-band radius window = frac * (p99-p1 radius)
extern float FiberMergeAngleDeg;  // trajectory-coherence merge threshold (deg)
extern int   FiberMSSeeds;        // # random seeds for mean-shift
extern int   FiberMinGroupSize;   // min spikes for a provisional group / fiber
extern float ChunkMinutes;        // reused for uniform chunking when no ChunkFile

// ── Standalone fiber clustering branch ──────────────────────────────────────
// A self-contained alternative to the Phase 1-9 pipeline: per chunk it builds
// the off-spike whitener, walks random seeds to fiber ridge centers via in-band
// directional mean-shift, merges co-linear trajectories, and assigns by
// whiteness residual + calibrated posterior.  Populates Class[] (1-based) so
// main's SaveOutput() writes the .clu.  Cross-chunk drift tracking is a
// separate stage (fibers get chunk-disjoint ids here).  Activated by
// -FiberStandaloneEnable 1.
float KK::RunFiberStandalone(const std::vector<float>& chunkBoundsSec, float samplingRate)
{
    const int nsamp = NbSamplesPerSpike, nch = NbChannels, wElems = nsamp * nch;
    const int masklo = 11, maskhi = 26, p = (maskhi - masklo) * nch;
    if (GroupChannelIds.empty() || NbTotalChannels <= 0 || nch <= 0 || nsamp <= 0) {
        Output("[FiberStandalone] missing dims / .fil channel map — abort\n"); return 0.0f;
    }
    char spkPath[STRLEN + 16]; pickInputPath(spkPath, sizeof(spkPath), FileBase, "spk", ElecNo);
    char filPath[STRLEN + 16]; std::snprintf(filPath, sizeof(filPath), "%s.fil", FileBase);
    char resPath[STRLEN + 16]; std::snprintf(resPath, sizeof(resPath), "%s.res.%d", FileBase, ElecNo);
    FILE* resF = std::fopen(resPath, "rb");
    if (!resF) { Output("[FiberStandalone] %s unavailable — abort\n", resPath); return 0.0f; }
    std::vector<long> absS(nPoints);
    for (int i = 0; i < nPoints; ++i) { int64_t ts = 0; fseeko(resF, (off_t)i * 8, SEEK_SET);
        if (std::fread(&ts, 8, 1, resF) != 1) ts = 0; absS[i] = (long)ts; }
    std::fclose(resF);

    // chunk assignment: provided bounds (sec) if any, else uniform by ChunkMinutes
    const double sess = (double)timeRawMax - (double)timeRawMin;
    std::vector<std::vector<int>> chunkPts;
    if (chunkBoundsSec.size() >= 2) {
        chunkPts.resize(chunkBoundsSec.size() - 1);
        for (int i = 0; i < nPoints; ++i) { double tsec = ((double)absS[i] - (double)timeRawMin) / samplingRate;
            int c = 0; while (c < (int)chunkPts.size() - 1 && tsec >= chunkBoundsSec[c + 1]) ++c; chunkPts[c].push_back(i); }
    } else if (ChunkMinutes > 0 && sess > 0 && samplingRate > 0) {
        double chunkSec = ChunkMinutes * 60.0; int nC = (int)std::ceil((sess / samplingRate) / chunkSec); if (nC < 1) nC = 1;
        chunkPts.resize(nC);
        for (int i = 0; i < nPoints; ++i) { double tsec = ((double)absS[i] - (double)timeRawMin) / samplingRate;
            int c = (int)(tsec / chunkSec); if (c < 0) c = 0; if (c >= nC) c = nC - 1; chunkPts[c].push_back(i); }
    } else { chunkPts.resize(1); for (int i = 0; i < nPoints; ++i) chunkPts[0].push_back(i); }

    int fiberBase = 0;
    for (size_t c = 0; c < chunkPts.size(); ++c) {
        const auto& pts = chunkPts[c]; const int N = (int)pts.size();
        if (N < FiberMinGroupSize * 2) { for (int i : pts) Class[i] = 1; continue; }

        std::vector<double> waves((size_t)N * wElems);
        { FILE* sp = std::fopen(spkPath, "rb"); if (!sp) { for (int i : pts) Class[i] = 1; continue; }
          std::vector<int16_t> row(wElems);
          for (int i = 0; i < N; ++i) { fseeko(sp, (off_t)pts[i] * wElems * 2, SEEK_SET);
              if (std::fread(row.data(), 2, wElems, sp) != (size_t)wElems) for (int e=0;e<wElems;++e) row[e]=0;
              for (int e = 0; e < wElems; ++e) waves[(size_t)i * wElems + e] = (double)row[e]; }
          std::fclose(sp); }

        long s0 = (1L << 62), s1 = 0; std::vector<long> a2(N);
        for (int i = 0; i < N; ++i) { a2[i] = absS[pts[i]]; s0 = std::min(s0, a2[i]); s1 = std::max(s1, a2[i]); }
        s0 = std::max(0L, s0 - nsamp); s1 = s1 + nsamp + 1;
        std::vector<double> W, nm; int pp = 0;
        if (!fiberChunkWhitener(filPath, NbTotalChannels, GroupChannelIds, s0, s1, a2, nsamp, masklo, maskhi, W, nm, pp)) {
            for (int i : pts) Class[i] = 1; continue; }

        std::vector<double> wg(waves); fiberstage::realign(wg, N, nsamp, nch);
        std::vector<double> X; fiberstage::mask_whiten(wg, N, nsamp, nch, masklo, maskhi, nm, W, p, X);
        std::vector<double> rad(N), dir((size_t)N * p);
        for (int i = 0; i < N; ++i) { const double* Xi = &X[(size_t)i*p]; double nn=0; for (int j=0;j<p;j++) nn+=Xi[j]*Xi[j]; nn=std::sqrt(nn);
            rad[i]=nn; for (int j=0;j<p;j++) dir[(size_t)i*p+j]=Xi[j]/(nn+1e-12); }
        std::vector<double> rsort(rad); std::sort(rsort.begin(), rsort.end());
        double dr = FiberMSDrFrac * (rsort[(int)(0.99*(N-1))] - rsort[(int)(0.01*(N-1))]);

        // deterministic support + seed subsamples (RNG-free, reproducible)
        int nsup = std::min(N, 20000), S = std::min(N, (int)FiberMSSeeds);
        std::vector<double> dsup((size_t)nsup*p), rsup(nsup), ds((size_t)S*p), rs(S);
        for (int j = 0; j < nsup; ++j) { int idx=(int)((long)j*N/nsup); rsup[j]=rad[idx]; for(int k=0;k<p;k++) dsup[(size_t)j*p+k]=dir[(size_t)idx*p+k]; }
        for (int s = 0; s < S;    ++s) { int idx=(int)((long)s*N/S);    rs[s]=rad[idx];   for(int k=0;k<p;k++) ds[(size_t)s*p+k]=dir[(size_t)idx*p+k]; }
        fiberstage::meanshift_inband(dsup.data(), rsup.data(), nsup, p, ds.data(), rs.data(), S, FiberMSKappa, dr, 15);
        std::vector<double> cd, cr; fiberstage::dedupe_centers(ds.data(), rs.data(), S, p, 8.0, 0.12, cd, cr);
        const int M = (int)cr.size(); if (M < 1) { for (int i : pts) Class[i] = 1; continue; }

        std::vector<int> lab(N);
        for (int i = 0; i < N; ++i) { const double* di=&dir[(size_t)i*p]; double best=-2; int bk=0;
            for (int k=0;k<M;k++){ const double* ck=&cd[(size_t)k*p]; double cs=0; for(int j=0;j<p;j++) cs+=di[j]*ck[j]; if(cs>best){best=cs;bk=k;} } lab[i]=bk; }
        std::vector<fiberstage::Traj> trajs(M);
        for (int k = 0; k < M; ++k) { std::vector<int> idx; for (int i=0;i<N;i++) if (lab[i]==k) idx.push_back(i);
            if ((int)idx.size() < FiberMinGroupSize) { trajs[k]=fiberstage::Traj(); continue; }
            trajs[k]=fiberstage::trajectory(X, idx, p); }
        std::vector<int> fmap = trajectoryCoherenceMerge(trajs, FiberMergeAngleDeg);
        int nFib = 0; for (int v : fmap) nFib = std::max(nFib, v + 1); if (nFib < 1) { for (int i : pts) Class[i] = 1; continue; }
        std::vector<int> seed(N); for (int i = 0; i < N; ++i) seed[i] = fmap[lab[i]];

        fiberstage::Result R = fiberstage::consolidate(waves, N, nsamp, nch, masklo, maskhi, W, nm, p, seed, nFib);
        for (int i = 0; i < N; ++i) Class[pts[i]] = fiberBase + R.hard[i] + 1;
        fiberBase += nFib;
        Output("[FiberStandalone] chunk %zu/%zu: %d spikes -> %d centers -> %d fibers\n",
               c + 1, chunkPts.size(), N, M, nFib);
    }
    nClustersAlive = fiberBase; nStartingClusters = fiberBase;
    Output("[FiberStandalone] done: %d fibers across %zu chunk(s)\n", fiberBase, chunkPts.size());
    return (float)fiberBase;
}
