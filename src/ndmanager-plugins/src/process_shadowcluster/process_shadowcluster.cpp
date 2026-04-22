/***************************************************************************
 * process_shadowcluster.cpp
 *
 * Assign newly-detected spikes to "shadow" clusters paired one-to-one with
 * existing (already-sorted) clusters.  A new spike joins the shadow of
 * existing cluster k iff k is the nearest parent in feature space AND the
 * squared Mahalanobis distance to k's robust centroid falls below a
 * χ²(nDims, p) cutoff.  New spikes too far from every existing cluster go
 * into a single "unmatched" bin.
 *
 * Algorithm
 * =========
 *   1. Load reference .fet.N + .clu.N.
 *      For each existing cluster k with size ≥ minClusterSize compute a
 *      robust centroid and per-dim scale in the (nDims-1)-D feature
 *      subspace (dropping the trailing timestamp column):
 *        μ_k [d]  = median over cluster k of f[*, d]
 *        σ_k [d]  = MAD(f[*, d]) × 1.4826
 *                   (the 1.4826 Gaussian-consistency factor converts MAD
 *                   into an unbiased σ estimator; robust to outliers that
 *                   would inflate the parametric mean/stddev).
 *      Clusters 0 and 1 are reserved by the neurosuite convention
 *      (artefact / MUA) and excluded from parent eligibility.
 *
 *   2. Load the PCA basis from .pca.N (auto-detects both the legacy
 *      5-int32 header written by process_pca and the magic-prefixed
 *      7-int32 PCAE header expected by process_refeaturize).
 *
 *   3. Project each new spike's waveform through the PCA basis to obtain
 *      its feature-space coordinates (matching the reference .fet.N
 *      dimensionality, minus the timestamp column).  When the reference
 *      .fet has extra peak features appended (auto-detected from the
 *      dimensionality), we append the per-channel peak max-abs value to
 *      mirror the convention used by process_pca -x.
 *
 *   4. For each new spike, compute squared Mahalanobis distance d²_k to
 *      every eligible parent k using a diagonal scale matrix:
 *        d²_k = Σ_d  ((f[d] - μ_k[d]) / σ_k[d])²
 *      Diagonal (not full) covariance is the right choice here: we are
 *      building per-cluster scale from medians of *each feature dim* and
 *      MAD-based σ's, which are inherently per-dim.  A full robust
 *      covariance would require a high-dimensional M-estimator
 *      (e.g. MCD), which is overkill for a second-pass assignment whose
 *      correctness is ultimately validated by the analyst in Klusters.
 *
 *   5. Let k* = argmin_k d²_k.  If d²_{k*} < χ²(nDims-1, p) the new spike
 *      is assigned to cluster (offset + k*); otherwise it goes to cluster
 *      (offset + Kmax + 1), the "unmatched" bin.  `offset` is chosen as
 *      (maxExistingClusterId + 1) so shadow IDs never collide with
 *      existing ones.
 *
 *   6. Merge-sort the combined (original + new) spike set by timestamp and
 *      write combined .res / .spk / .fet / .clu files to the output stem.
 *      Feature rows for new spikes are projected as in step 3, padded
 *      with the new timestamp as the final column to match the reference
 *      .fet layout.  New .spk waveforms are copied byte-for-byte from the
 *      re-extraction step.
 *
 * χ² calibration
 * --------------
 * We use a Wilson-Hilferty cube-root approximation to χ²(k, p)⁻¹, which
 * is accurate to within ~0.1% for k ≥ 5 and all p in the typical range
 * (1e-6 .. 1-1e-6).  This avoids a GSL dependency for this TU.  The
 * 0.9999 default matches the per-dim-MAD+χ² calibration established by
 * the Klusters cluster filter and the earlier Mahalanobis-based tooling
 * in the repository.
 *
 * File formats
 * ============
 * Reads:
 *   <refBase>.fet.<g>   int32 nDims header; nSpikes × nDims × int64, last
 *                       column = timestamp
 *   <refBase>.clu.<g>   int32 nClusters header; nSpikes × int32 ids
 *   <refBase>.spk.<g>   int16 sample-major within each waveform:
 *                       wav[s*nChanGroup + c]; no header
 *   <refBase>.res.<g>   int64 timestamps, no header
 *   <refBase>.pca.<g>   PCA basis (see loadPcaModel below — both header
 *                       dialects supported)
 *   <newBase>.res.<g>   int64 timestamps from process_reextractspikes
 *   <newBase>.spk.<g>   new waveforms
 *
 * Writes:
 *   <outBase>.res.<g>   merged timestamps (sorted)
 *   <outBase>.spk.<g>   merged waveforms (sorted order)
 *   <outBase>.fet.<g>   merged features (new rows computed; timestamp col
 *                       updated to new ts)
 *   <outBase>.clu.<g>   merged cluster ids (originals preserved; new
 *                       spikes get shadow / unmatched ids)
 ***************************************************************************/

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#endif

using std::cerr;
using std::cout;
using std::endl;
using std::pair;
using std::string;
using std::vector;

static const char *programVersion = "process_shadowcluster 1.1 (2026-04 infl-guard)";

// =========================================================================
// Wilson-Hilferty χ² inverse CDF
// =========================================================================
//   χ²_k at quantile p ≈ k × (1 − 2/(9k) + z_p × sqrt(2/(9k)))³
//   where z_p = Φ⁻¹(p).  The Beasley-Springer-Moro normal-inverse
//   approximation below is a standard rational approximation accurate to
//   ~1e-9 in the tails; good enough for our threshold use.
// =========================================================================
static double normInvCDF(double p)
{
    // Beasley-Springer-Moro
    static const double a[] = { -3.969683028665376e+01,  2.209460984245205e+02,
                                -2.759285104469687e+02,  1.383577518672690e+02,
                                -3.066479806614716e+01,  2.506628277459239e+00 };
    static const double b[] = { -5.447609879822406e+01,  1.615858368580409e+02,
                                -1.556989798598866e+02,  6.680131188771972e+01,
                                -1.328068155288572e+01 };
    static const double c[] = { -7.784894002430293e-03, -3.223964580411365e-01,
                                -2.400758277161838e+00, -2.549732539343734e+00,
                                 4.374664141464968e+00,  2.938163982698783e+00 };
    static const double d[] = {  7.784695709041462e-03,  3.224671290700398e-01,
                                 2.445134137142996e+00,  3.754408661907416e+00 };
    const double plow  = 0.02425;
    const double phigh = 1.0 - plow;
    double q, r;
    if (p < plow) {
        q = std::sqrt(-2.0 * std::log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
               ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
    if (p <= phigh) {
        q = p - 0.5;
        r = q*q;
        return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
               (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
    }
    q = std::sqrt(-2.0 * std::log(1.0 - p));
    return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
            ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
}

static double chi2InvCDF(int k, double p)
{
    if (k <= 0) return 0.0;
    const double z  = normInvCDF(p);
    const double x  = 1.0 - 2.0 / (9.0 * k) + z * std::sqrt(2.0 / (9.0 * k));
    return (double)k * x * x * x;
}

// =========================================================================
// PCA model — accepts both header dialects
// =========================================================================
struct PcaModel {
    int nChannels;
    int data2use;      // samples per channel fed into PCA
    int nComponents;
    int recShift;      // first sample offset within waveform
    bool isCentered;
    // mean[ch][s]
    vector<vector<double>> mean;
    // eigvec[ch] laid out col-major: [s * nComponents + k]
    vector<vector<double>> eigvec;
};

static bool loadPcaModel(const string &path, PcaModel &m)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) { cerr << "error: cannot open PCA file '" << path << "'" << endl;
              return false; }

    int32_t w0 = 0;
    if (std::fread(&w0, sizeof(int32_t), 1, f) != 1) {
        cerr << "error: empty PCA file" << endl; std::fclose(f); return false;
    }

    bool hasMagic = (w0 == (int32_t)0x50434145); // 'PCAE'
    int32_t version = 1, nc, d2u, ncomp, rshift, iscent;

    if (hasMagic) {
        if (std::fread(&version, sizeof(int32_t), 1, f) != 1 ||
            std::fread(&nc,      sizeof(int32_t), 1, f) != 1 ||
            std::fread(&d2u,     sizeof(int32_t), 1, f) != 1 ||
            std::fread(&ncomp,   sizeof(int32_t), 1, f) != 1 ||
            std::fread(&rshift,  sizeof(int32_t), 1, f) != 1 ||
            std::fread(&iscent,  sizeof(int32_t), 1, f) != 1) {
            cerr << "error: truncated PCAE header" << endl;
            std::fclose(f); return false;
        }
        if (version != 1) {
            cerr << "error: unsupported PCAE version " << version << endl;
            std::fclose(f); return false;
        }
    } else {
        // Legacy 5-int32 header: [nCh, data2use, nComp, centered, recShift]
        nc = w0;
        if (std::fread(&d2u,    sizeof(int32_t), 1, f) != 1 ||
            std::fread(&ncomp,  sizeof(int32_t), 1, f) != 1 ||
            std::fread(&iscent, sizeof(int32_t), 1, f) != 1 ||
            std::fread(&rshift, sizeof(int32_t), 1, f) != 1) {
            cerr << "error: truncated legacy PCA header" << endl;
            std::fclose(f); return false;
        }
    }

    m.nChannels   = (int)nc;
    m.data2use    = (int)d2u;
    m.nComponents = (int)ncomp;
    m.recShift    = (int)rshift;
    m.isCentered  = (iscent != 0);

    if (m.nChannels <= 0 || m.data2use <= 0 || m.nComponents <= 0) {
        cerr << "error: invalid PCA dimensions in '" << path << "'" << endl;
        std::fclose(f); return false;
    }

    m.mean.assign(m.nChannels, {});
    m.eigvec.assign(m.nChannels, {});
    for (int ch = 0; ch < m.nChannels; ++ch) {
        m.mean[ch].resize(m.data2use);
        if (std::fread(m.mean[ch].data(), sizeof(double), m.data2use, f) !=
            (size_t)m.data2use) {
            cerr << "error: truncated PCA mean (ch=" << ch << ")" << endl;
            std::fclose(f); return false;
        }
    }
    for (int ch = 0; ch < m.nChannels; ++ch) {
        const size_t N = (size_t)m.data2use * (size_t)m.nComponents;
        m.eigvec[ch].resize(N);
        if (std::fread(m.eigvec[ch].data(), sizeof(double), N, f) != N) {
            cerr << "error: truncated PCA eigvec (ch=" << ch << ")" << endl;
            std::fclose(f); return false;
        }
    }
    std::fclose(f);
    return true;
}

// Project a single spike waveform (sample-major, nSamples × nChan int16)
// through the PCA model.  Returns nChan × nComp features; optionally
// appends nChan peak values (matching the -x / "extra features" convention
// of process_pca).
static void projectSpike(const int16_t *wav,
                          int nSamples, int nChan,
                          const PcaModel &m,
                          bool extraFeat,
                          vector<int64_t> &out)
{
    (void)nSamples;
    const int nComp = m.nComponents;
    const int outDims = m.nChannels * nComp + (extraFeat ? m.nChannels : 0);
    out.resize(outDims);

    for (int ch = 0; ch < m.nChannels; ++ch) {
        const auto &mu = m.mean[ch];
        const auto &ev = m.eigvec[ch];
        vector<double> x(m.data2use);
        for (int s = 0; s < m.data2use; ++s) {
            const int sIdx = m.recShift + s;
            const double raw = (double)wav[sIdx * nChan + ch];
            x[s] = m.isCentered ? (raw - mu[s]) : raw;
        }
        for (int k = 0; k < nComp; ++k) {
            double v = 0.0;
            for (int s = 0; s < m.data2use; ++s)
                v += ev[(size_t)s * nComp + k] * x[s];
            out[ch * nComp + k] = (int64_t)std::llround(v);
        }
    }

    if (extraFeat) {
        for (int ch = 0; ch < m.nChannels; ++ch) {
            int16_t peak = 0;
            for (int s = 0; s < m.data2use; ++s) {
                const int sIdx = m.recShift + s;
                const int16_t v = wav[sIdx * nChan + ch];
                if (std::abs((int)v) > std::abs((int)peak)) peak = v;
            }
            out[m.nChannels * nComp + ch] = (int64_t)peak;
        }
    }
}

// =========================================================================
// Per-cluster robust statistics
// =========================================================================
struct ClusterStats {
    int      id;           // original cluster id
    int64_t  size;         // number of member spikes
    vector<double> mu;     // robust centroid per feature dim (length nFeatDim)
    vector<double> sig;    // MAD × 1.4826 per dim; clamped to epsilon
    bool     eligible;     // true if size ≥ minClusterSize and all sig > 0
};

// Compute median and MAD-based σ of a 1-D sample (unsorted input; uses
// nth_element for O(n) median extraction, leaves data permuted).
static void medianAndMAD(vector<double> &data, double &med, double &mad)
{
    const size_t n = data.size();
    if (n == 0) { med = 0.0; mad = 0.0; return; }
    const size_t mid = n / 2;
    std::nth_element(data.begin(), data.begin() + mid, data.end());
    med = data[mid];
    vector<double> dev(n);
    for (size_t i = 0; i < n; ++i) dev[i] = std::fabs(data[i] - med);
    std::nth_element(dev.begin(), dev.begin() + mid, dev.end());
    mad = dev[mid];
}

// =========================================================================
// Binary file readers
// =========================================================================
static bool readFullFile(const string &path, vector<uint8_t> &buf)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    fseeko(f, 0, SEEK_END);
    const off_t sz = ftello(f);
    fseeko(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); return false; }
    buf.resize((size_t)sz);
    if (sz > 0 && std::fread(buf.data(), 1, (size_t)sz, f) != (size_t)sz) {
        std::fclose(f); return false;
    }
    std::fclose(f);
    return true;
}

// =========================================================================
// main
// =========================================================================
static void usage(const char *n)
{
    cerr << programVersion << endl;
    cerr << "usage: " << n
         << " --ref <refBase> --new <newBase> --grp <N>"
            " --nChan <chInGroup> --wav <samples>"
            " [--out <outBase>]"
            " [--minClusterSize 50] [--chi2 0.999] [--extraFeat auto|0|1]"
            " [--inflationRatio 5.0] [--inflationCap 500]"
            " [-v]" << endl;
    cerr << "  --out defaults to --ref (in-place merge).  The output"
            " files are written to temporary .tmp paths first and renamed"
            " atomically on success." << endl;
    cerr << "  --inflationRatio R (default 5.0): a parent cluster may not"
            " absorb more than R × |parent| new spikes; excess spikes"
            " (the most distant by Mahalanobis d²) are demoted to the"
            " unmatched bin.  --inflationCap C (default 500) is the"
            " absolute minimum ceiling: a parent always gets to absorb"
            " at least max(C, R × |parent|).  Set ratio or cap to 0 to"
            " disable the guard." << endl;
}

int main(int argc, char **argv)
{
    string refBase, newBase, outBase;
    int    grp            = -1;
    int    nChanGroup     = -1;
    int    wavSamples     = -1;
    int64_t minClusterSize= 50;
    // χ² gate default: 0.999 (d²≈46.80 for df=21).  0.9999 (used in 1.0)
    // was too permissive on heavy-tailed reference clusters and produced
    // runaway absorption into a single parent in practice — see the
    // inflation guard below for the hard backstop, and CHANGES.md.
    double chi2P          = 0.999;
    int    extraFeatMode  = -1;  // -1 auto, 0 no, 1 yes
    // Inflation guard: a parent may absorb at most max(inflationCap,
    // inflationRatio × |parent|) new spikes.  Set either to 0 (or
    // non-positive) to disable the guard entirely.
    double inflationRatio = 5.0;
    int64_t inflationCap  = 500;
    bool   verbose        = false;

    for (int i = 1; i < argc; ++i) {
        const string a = argv[i];
        if      (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else if (a == "-v") verbose = true;
        else if (a == "--ref"   && i+1<argc) refBase        = argv[++i];
        else if (a == "--new"   && i+1<argc) newBase        = argv[++i];
        else if (a == "--out"   && i+1<argc) outBase        = argv[++i];
        else if (a == "--grp"   && i+1<argc) grp            = std::atoi(argv[++i]);
        else if (a == "--nChan" && i+1<argc) nChanGroup     = std::atoi(argv[++i]);
        else if (a == "--wav"   && i+1<argc) wavSamples     = std::atoi(argv[++i]);
        else if (a == "--minClusterSize" && i+1<argc)
                                              minClusterSize = std::atoll(argv[++i]);
        else if (a == "--chi2"  && i+1<argc) chi2P          = std::atof(argv[++i]);
        else if (a == "--inflationRatio" && i+1<argc)
                                              inflationRatio = std::atof(argv[++i]);
        else if (a == "--inflationCap"   && i+1<argc)
                                              inflationCap   = std::atoll(argv[++i]);
        else if (a == "--extraFeat" && i+1<argc) {
            const string v = argv[++i];
            if (v == "auto") extraFeatMode = -1;
            else if (v == "0" || v == "no")  extraFeatMode = 0;
            else if (v == "1" || v == "yes") extraFeatMode = 1;
            else { cerr << "error: bad --extraFeat '" << v << "'" << endl; return 1; }
        }
        else { cerr << "error: unknown option '" << a << "'" << endl; usage(argv[0]); return 1; }
    }
    if (refBase.empty() || newBase.empty() ||
        grp < 1 || nChanGroup <= 0 || wavSamples <= 0) {
        usage(argv[0]); return 1;
    }
    // In-place merge by default.
    if (outBase.empty()) outBase = refBase;
    const bool inPlace = (outBase == refBase);

    // ── paths ─────────────────────────────────────────────────────────────
    const string refFet = refBase + ".fet." + std::to_string(grp);
    const string refClu = refBase + ".clu." + std::to_string(grp);
    const string refSpk = refBase + ".spk." + std::to_string(grp);
    const string refRes = refBase + ".res." + std::to_string(grp);
    const string refPca = refBase + ".pca." + std::to_string(grp);
    const string newRes = newBase + ".res." + std::to_string(grp);
    const string newSpk = newBase + ".spk." + std::to_string(grp);
    const string outFet = outBase + ".fet." + std::to_string(grp);
    const string outClu = outBase + ".clu." + std::to_string(grp);
    const string outSpk = outBase + ".spk." + std::to_string(grp);
    const string outRes = outBase + ".res." + std::to_string(grp);

    // ── load reference .fet ───────────────────────────────────────────────
    //   int32 nDims header + nSpikes × nDims × int64
    vector<uint8_t> fetRaw;
    if (!readFullFile(refFet, fetRaw)) {
        cerr << "error: cannot read " << refFet << endl; return 1;
    }
    if (fetRaw.size() < sizeof(int32_t)) {
        cerr << "error: " << refFet << " too small" << endl; return 1;
    }
    int32_t nDims = 0; std::memcpy(&nDims, fetRaw.data(), sizeof(int32_t));
    if (nDims <= 1 || nDims > 65536) {
        cerr << "error: bad nDims=" << nDims << " in " << refFet << endl; return 1;
    }
    const size_t bodyBytes = fetRaw.size() - sizeof(int32_t);
    const size_t rowBytes  = (size_t)nDims * sizeof(int64_t);
    if (bodyBytes % rowBytes != 0) {
        cerr << "error: " << refFet << " body not multiple of row size" << endl;
        return 1;
    }
    const size_t nRefSpikes = bodyBytes / rowBytes;
    const int64_t *fetData  = reinterpret_cast<const int64_t *>(
                                  fetRaw.data() + sizeof(int32_t));
    // Timestamp is last column; non-timestamp feature dim = nDims - 1.
    const int nFeatDim = nDims - 1;

    // ── load reference .clu ───────────────────────────────────────────────
    //   int32 nClusters header + nSpikes × int32 ids
    vector<uint8_t> cluRaw;
    if (!readFullFile(refClu, cluRaw)) {
        cerr << "error: cannot read " << refClu << endl; return 1;
    }
    if (cluRaw.size() < sizeof(int32_t)) {
        cerr << "error: " << refClu << " too small" << endl; return 1;
    }
    int32_t nClustersHeader = 0;
    std::memcpy(&nClustersHeader, cluRaw.data(), sizeof(int32_t));
    const size_t idsExpected = cluRaw.size() - sizeof(int32_t);
    if (idsExpected != nRefSpikes * sizeof(int32_t)) {
        cerr << "error: " << refClu << " has "
             << (idsExpected / sizeof(int32_t))
             << " cluster ids but .fet has " << nRefSpikes << " spikes" << endl;
        return 1;
    }
    const int32_t *cluIds = reinterpret_cast<const int32_t *>(
                                cluRaw.data() + sizeof(int32_t));

    // ── load reference .spk (file size is the sanity check) ───────────────
    //   nSpikes × wavSamples × nChanGroup × int16
    vector<uint8_t> refSpkRaw;
    if (!readFullFile(refSpk, refSpkRaw)) {
        cerr << "error: cannot read " << refSpk << endl; return 1;
    }
    const size_t spkRowBytes = (size_t)wavSamples * (size_t)nChanGroup * sizeof(int16_t);
    if (refSpkRaw.size() != nRefSpikes * spkRowBytes) {
        cerr << "error: " << refSpk << " size "
             << refSpkRaw.size() << " ≠ expected "
             << (nRefSpikes * spkRowBytes) << endl;
        return 1;
    }

    // ── decide extraFeat mode (auto-detect if requested) ──────────────────
    bool extraFeat;
    if (extraFeatMode == 0)      extraFeat = false;
    else if (extraFeatMode == 1) extraFeat = true;
    else {
        // Heuristic: if (nFeatDim - nComp*nChanGroup) == nChanGroup, then
        // peak features were appended; else no.  We don't have PCA loaded
        // yet — deferred to after PCA load.  Set sentinel for now.
        extraFeat = false;
    }

    // ── load PCA model ────────────────────────────────────────────────────
    PcaModel pca;
    if (!loadPcaModel(refPca, pca)) return 1;
    // The PCA basis may have FEWER channels than the group when the
    // upstream pipeline used a rank-reducing spatial-derivative
    // transform (process_pca_stderiv with sdiffOrder 1 or 3 drops one
    // linearly-dependent channel — see ndm_pca_stderiv).  In that case
    // the projection loop in projectSpike() iterates pca.nChannels and
    // reads channels 0..pca.nChannels-1 of each sample, which is
    // exactly the set the PCA basis was trained on (process_pca_stderiv
    // with SDIFF_PASS + dropLast keeps the leading nChan-1 channels).
    // We only need to reject the case where PCA wants MORE channels
    // than the .spk/.spkD layout provides.
    if (pca.nChannels > nChanGroup) {
        cerr << "error: PCA nChannels=" << pca.nChannels
             << " > group nChan=" << nChanGroup
             << " (PCA basis cannot have more channels than the waveform layout)"
             << endl;
        return 1;
    }
    if (pca.nChannels < nChanGroup) {
        cerr << "info: PCA nChannels=" << pca.nChannels
             << " < group nChan=" << nChanGroup
             << " (stderiv rank-reduced basis; reading first "
             << pca.nChannels << " of " << nChanGroup << " channels)"
             << endl;
    }

    // Auto-detect extraFeat by dim arithmetic.
    if (extraFeatMode == -1) {
        const int baseDim  = pca.nComponents * pca.nChannels;
        if (nFeatDim == baseDim)                 extraFeat = false;
        else if (nFeatDim == baseDim + pca.nChannels)
                                                 extraFeat = true;
        else {
            cerr << "error: cannot reconcile .fet nFeatDim=" << nFeatDim
                 << " with PCA nComp*nChan=" << baseDim
                 << " (or + nChan=" << (baseDim + pca.nChannels)
                 << ").  Pass --extraFeat 0|1 explicitly." << endl;
            return 1;
        }
    }

    if (verbose) {
        cerr << programVersion << endl
             << "grp              : " << grp << endl
             << "ref spikes       : " << nRefSpikes << endl
             << "feat dims (excl. ts) : " << nFeatDim << endl
             << "nChan group      : " << nChanGroup << endl
             << "wav samples      : " << wavSamples << endl
             << "PCA data2use     : " << pca.data2use
             << "  nComp=" << pca.nComponents
             << "  recShift=" << pca.recShift
             << "  centered=" << pca.isCentered << endl
             << "extraFeat        : " << (extraFeat ? "yes" : "no") << endl
             << "chi2 quantile    : " << chi2P
             << "  threshold(d²)=" << chi2InvCDF(nFeatDim, chi2P) << endl
             << "minClusterSize   : " << minClusterSize << endl
             << "inflation guard  : "
             << ((inflationRatio > 0.0 && inflationCap > 0)
                 ? ("ratio=" + std::to_string(inflationRatio) +
                    ", cap=" + std::to_string(inflationCap))
                 : std::string("disabled")) << endl;
    }

    // ── compute per-cluster robust stats ─────────────────────────────────
    // Bucket indices by cluster id.
    int32_t maxCluId = 0;
    for (size_t i = 0; i < nRefSpikes; ++i)
        if (cluIds[i] > maxCluId) maxCluId = cluIds[i];

    vector<vector<size_t>> idxByClu((size_t)maxCluId + 1);
    for (size_t i = 0; i < nRefSpikes; ++i)
        idxByClu[(size_t)cluIds[i]].push_back(i);

    vector<ClusterStats> stats;
    stats.reserve(maxCluId + 1);
    const double SIG_FLOOR = 1.0; // int64 features quantised to int; avoids /0
    for (int k = 0; k <= maxCluId; ++k) {
        ClusterStats s;
        s.id       = k;
        s.size     = (int64_t)idxByClu[(size_t)k].size();
        s.eligible = false;
        // Cluster 0 (artefact) and 1 (MUA) are never parents per neurosuite convention.
        if (k < 2 || s.size < minClusterSize) { stats.push_back(std::move(s)); continue; }

        s.mu.assign(nFeatDim, 0.0);
        s.sig.assign(nFeatDim, 0.0);
        vector<double> col(idxByClu[(size_t)k].size());
        for (int d = 0; d < nFeatDim; ++d) {
            for (size_t j = 0; j < idxByClu[(size_t)k].size(); ++j) {
                const size_t spkIdx = idxByClu[(size_t)k][j];
                col[j] = (double)fetData[spkIdx * (size_t)nDims + d];
            }
            double med = 0.0, mad = 0.0;
            medianAndMAD(col, med, mad);
            s.mu[d]  = med;
            s.sig[d] = std::max(SIG_FLOOR, mad * 1.4826);
        }
        s.eligible = true;
        stats.push_back(std::move(s));
    }

    int nEligible = 0;
    for (const auto &s : stats) if (s.eligible) ++nEligible;
    if (verbose) {
        cerr << "eligible parents : " << nEligible
             << " / " << (int)stats.size() << " clusters" << endl;
    }
    if (nEligible == 0) {
        cerr << "warning: no eligible parent clusters "
                "(none with size ≥ " << minClusterSize << ") — "
                "all new spikes will land in the unmatched bin" << endl;
    }

    // ── load new .res / .spk ─────────────────────────────────────────────
    vector<uint8_t> newResRaw, newSpkRaw;
    if (!readFullFile(newRes, newResRaw)) {
        cerr << "error: cannot read " << newRes << endl; return 1;
    }
    if (!readFullFile(newSpk, newSpkRaw)) {
        cerr << "error: cannot read " << newSpk << endl; return 1;
    }
    if (newResRaw.size() % sizeof(int64_t) != 0) {
        cerr << "error: " << newRes << " not multiple of int64" << endl; return 1;
    }
    const size_t nNewSpikes = newResRaw.size() / sizeof(int64_t);
    if (newSpkRaw.size() != nNewSpikes * spkRowBytes) {
        cerr << "error: " << newSpk << " size "
             << newSpkRaw.size() << " ≠ expected "
             << (nNewSpikes * spkRowBytes) << endl;
        return 1;
    }
    const int64_t *newRes64 = reinterpret_cast<const int64_t *>(newResRaw.data());
    const int16_t *newSpk16 = reinterpret_cast<const int16_t *>(newSpkRaw.data());

    if (verbose) cerr << "new spikes       : " << nNewSpikes << endl;

    // ── assign each new spike to a shadow cluster ────────────────────────
    //
    // Shadow cluster id offset: just past the highest existing id.
    // Unmatched bin gets (offset + maxCluId + 1).
    //
    // Two-pass design (v1.1):
    //   Pass 1 (parallel, expensive): for every new spike compute
    //     bestK and bestD2; record in tentativeBestK / tentativeBestD2.
    //     Also emit the .fet row that will ultimately be written.
    //   Pass 2 (serial, cheap):  apply the inflation guard — for every
    //     parent whose provisional acceptance count exceeds the cap, sort
    //     the candidate spikes by d² ascending and demote the tail to the
    //     unmatched bin.  This is essential to keep the parent's shadow
    //     cluster interpretable: a parent with 1868 reference spikes
    //     absorbing 108 000 new spikes is diagnostically useless.
    //
    // The guard activates iff inflationRatio > 0 AND inflationCap > 0.
    // Otherwise v1.0 behaviour is preserved exactly (no demotion).
    const int32_t shadowOffset   = maxCluId + 1;
    const int32_t unmatchedShadow= shadowOffset + maxCluId + 1;
    const double  d2Threshold    = chi2InvCDF(nFeatDim, chi2P);
    const bool    guardEnabled   = (inflationRatio > 0.0 && inflationCap > 0);

    vector<int32_t>         newCluIds(nNewSpikes, unmatchedShadow);
    vector<vector<int64_t>> newFetRows(nNewSpikes);
    vector<int64_t>         nAssignedPerParent((size_t)maxCluId + 1, 0);
    vector<int64_t>         nDemotedPerParent((size_t)maxCluId + 1, 0);
    int64_t                 nUnmatched = 0;

    // Per-spike tentative assignment (used by the guard pass).  bestK=-1
    // means "no eligible parent within threshold" — spike is already
    // destined for the unmatched bin and is not part of any demotion list.
    vector<int32_t> tentativeBestK(nNewSpikes, -1);
    vector<double>  tentativeBestD2(nNewSpikes, std::numeric_limits<double>::infinity());

#ifdef _OPENMP
    #pragma omp parallel
#endif
    {
        vector<int64_t> localAssigned((size_t)maxCluId + 1, 0);
        int64_t         localUnmatched = 0;
        vector<int64_t> feat;

#ifdef _OPENMP
        #pragma omp for schedule(dynamic, 1024)
#endif
        for (long long i = 0; i < (long long)nNewSpikes; ++i) {
            const int16_t *wav = newSpk16 + (size_t)i * wavSamples * nChanGroup;

            projectSpike(wav, wavSamples, nChanGroup, pca, extraFeat, feat);
            if ((int)feat.size() != nFeatDim) {
                // Internal consistency — should not trip given the
                // auto-detection above; fail loudly if it does.
                #ifdef _OPENMP
                #pragma omp critical
                #endif
                {
                    cerr << "error: projected feat dim " << feat.size()
                         << " != reference nFeatDim " << nFeatDim << endl;
                    std::exit(2);
                }
            }

            // Find nearest eligible parent by squared Mahalanobis distance.
            double   bestD2 = std::numeric_limits<double>::infinity();
            int      bestK  = -1;
            for (const auto &s : stats) {
                if (!s.eligible) continue;
                double d2 = 0.0;
                for (int d = 0; d < nFeatDim; ++d) {
                    const double z = ((double)feat[d] - s.mu[d]) / s.sig[d];
                    d2 += z * z;
                    if (d2 >= bestD2) break;   // early-out
                }
                if (d2 < bestD2) { bestD2 = d2; bestK = s.id; }
            }

            int32_t cluOut;
            if (bestK >= 0 && bestD2 < d2Threshold) {
                cluOut = shadowOffset + bestK;
                ++localAssigned[(size_t)bestK];
                tentativeBestK[(size_t)i]  = bestK;
                tentativeBestD2[(size_t)i] = bestD2;
            } else {
                cluOut = unmatchedShadow;
                ++localUnmatched;
                // tentativeBestK stays -1: this spike is already unmatched
                // and is not subject to the inflation guard.
            }
            newCluIds[(size_t)i] = cluOut;

            // Feat row to emit later: [feat..., timestamp]
            vector<int64_t> row(nDims);
            for (int d = 0; d < nFeatDim; ++d) row[d] = feat[d];
            row[nFeatDim] = newRes64[i];
            newFetRows[(size_t)i] = std::move(row);
        }

#ifdef _OPENMP
        #pragma omp critical
#endif
        {
            for (size_t k = 0; k < localAssigned.size(); ++k)
                nAssignedPerParent[k] += localAssigned[k];
            nUnmatched += localUnmatched;
        }
    }

    // ── inflation guard (serial) ─────────────────────────────────────────
    //
    // For each parent k with eligible cluster size |parent_k|, the cap is
    //     cap_k = max(inflationCap, inflationRatio × |parent_k|)
    // If nAssignedPerParent[k] > cap_k we demote the (assigned - cap_k)
    // spikes with the LARGEST d² back to the unmatched bin.  The
    // well-fitting spikes (small d²) stay in the shadow.
    //
    // Total cost: O(nAssignedPerParent[k] log nAssignedPerParent[k])
    // per over-absorbing parent, which is small relative to the main
    // assignment loop (nFeatDim × nEligible distance evaluations per
    // spike dominates).  Typically only 1-3 parents trip the cap in a
    // session, so the practical cost is negligible.
    if (guardEnabled && nNewSpikes > 0) {
        // Build per-parent candidate lists: (spikeIdx, d²).  Only parents
        // whose current count exceeds the cap need a list — skip the rest
        // to save allocation churn.
        vector<int64_t> capPerParent((size_t)maxCluId + 1, 0);
        for (const auto &s : stats) {
            if (!s.eligible) continue;
            const double  ratioCap = inflationRatio * (double)s.size;
            const int64_t cap      = (int64_t)std::max((double)inflationCap,
                                                        std::ceil(ratioCap));
            capPerParent[(size_t)s.id] = cap;
        }

        // Collect candidate spikes per over-capped parent.
        vector<vector<pair<double, size_t>>> candByParent((size_t)maxCluId + 1);
        for (int k = 0; k <= maxCluId; ++k) {
            if (nAssignedPerParent[(size_t)k] > capPerParent[(size_t)k])
                candByParent[(size_t)k].reserve(
                    (size_t)nAssignedPerParent[(size_t)k]);
        }
        for (size_t i = 0; i < nNewSpikes; ++i) {
            const int32_t k = tentativeBestK[i];
            if (k < 0) continue;  // unmatched spike, skip
            if (nAssignedPerParent[(size_t)k] <= capPerParent[(size_t)k])
                continue;         // parent within cap — no demotion needed
            candByParent[(size_t)k].emplace_back(tentativeBestD2[i], i);
        }

        // For each over-capped parent, sort by d² ascending and demote
        // the tail past the cap.
        for (int k = 0; k <= maxCluId; ++k) {
            auto &cand = candByParent[(size_t)k];
            if (cand.empty()) continue;
            const int64_t cap = capPerParent[(size_t)k];
            std::sort(cand.begin(), cand.end(),
                      [](const pair<double,size_t> &a,
                         const pair<double,size_t> &b){ return a.first < b.first; });
            for (size_t j = (size_t)cap; j < cand.size(); ++j) {
                const size_t spkIdx = cand[j].second;
                newCluIds[spkIdx]   = unmatchedShadow;
                ++nDemotedPerParent[(size_t)k];
                --nAssignedPerParent[(size_t)k];
                ++nUnmatched;
            }
        }
    }

    // ── merge by timestamp, emit combined files ──────────────────────────
    // Build an index array over combined order.  `origin[i]` < 0 means
    // original spike at (-origin[i] - 1); ≥ 0 means new spike at origin[i].
    struct TsRef { int64_t ts; int64_t idx; bool isNew; };
    vector<TsRef> combined;
    combined.reserve(nRefSpikes + nNewSpikes);
    // Original timestamps live in the last column of .fet.
    for (size_t i = 0; i < nRefSpikes; ++i) {
        const int64_t ts = fetData[i * (size_t)nDims + nFeatDim];
        combined.push_back({ts, (int64_t)i, false});
    }
    for (size_t i = 0; i < nNewSpikes; ++i)
        combined.push_back({newRes64[i], (int64_t)i, true});

    std::stable_sort(combined.begin(), combined.end(),
                     [](const TsRef &a, const TsRef &b){ return a.ts < b.ts; });

    // Determine combined nClusters = max cluster id + 1 across merged set.
    int32_t combinedMaxClu = maxCluId;
    for (size_t i = 0; i < nNewSpikes; ++i)
        if (newCluIds[i] > combinedMaxClu) combinedMaxClu = newCluIds[i];
    const int32_t combinedNClusters = combinedMaxClu + 1;

    // --- write .res ----------------------------------------------------
    // Pattern: write to <target>.tmp then atomically rename over the target.
    // On in-place merge this guarantees a crash between fopen and fclose
    // leaves the original file untouched.
    auto atomicRename = [](const std::string &tmp, const std::string &final_) {
        if (std::rename(tmp.c_str(), final_.c_str()) != 0) {
            cerr << "error: rename '" << tmp << "' → '" << final_ << "': "
                 << std::strerror(errno) << endl;
            std::exit(1);
        }
    };
    const std::string outResTmp = outRes + ".tmp";
    const std::string outCluTmp = outClu + ".tmp";
    const std::string outFetTmp = outFet + ".tmp";
    const std::string outSpkTmp = outSpk + ".tmp";
    {
        FILE *f = std::fopen(outResTmp.c_str(), "wb");
        if (!f) { cerr << "error: cannot write " << outResTmp << endl; return 1; }
        vector<int64_t> buf(combined.size());
        for (size_t i = 0; i < combined.size(); ++i) buf[i] = combined[i].ts;
        if (!buf.empty())
            std::fwrite(buf.data(), sizeof(int64_t), buf.size(), f);
        std::fclose(f);
    }
    // --- write .clu ----------------------------------------------------
    {
        FILE *f = std::fopen(outCluTmp.c_str(), "wb");
        if (!f) { cerr << "error: cannot write " << outCluTmp << endl; return 1; }
        std::fwrite(&combinedNClusters, sizeof(int32_t), 1, f);
        vector<int32_t> buf(combined.size());
        for (size_t i = 0; i < combined.size(); ++i)
            buf[i] = combined[i].isNew
                     ? newCluIds[(size_t)combined[i].idx]
                     : cluIds   [(size_t)combined[i].idx];
        if (!buf.empty())
            std::fwrite(buf.data(), sizeof(int32_t), buf.size(), f);
        std::fclose(f);
    }
    // --- write .fet ----------------------------------------------------
    {
        FILE *f = std::fopen(outFetTmp.c_str(), "wb");
        if (!f) { cerr << "error: cannot write " << outFetTmp << endl; return 1; }
        int32_t hdr = (int32_t)nDims;
        std::fwrite(&hdr, sizeof(int32_t), 1, f);
        for (const auto &c : combined) {
            if (c.isNew) {
                std::fwrite(newFetRows[(size_t)c.idx].data(),
                            sizeof(int64_t), (size_t)nDims, f);
            } else {
                const int64_t *src = fetData + (size_t)c.idx * (size_t)nDims;
                std::fwrite(src, sizeof(int64_t), (size_t)nDims, f);
            }
        }
        std::fclose(f);
    }
    // --- write .spk ----------------------------------------------------
    //
    // On in-place merge, the refSpkRaw buffer was loaded from the source
    // .spk BEFORE we open the tmp, so we can safely overwrite the target
    // after the rename.  (readFullFile slurped the entire file into a
    // std::vector.)
    {
        FILE *f = std::fopen(outSpkTmp.c_str(), "wb");
        if (!f) { cerr << "error: cannot write " << outSpkTmp << endl; return 1; }
        for (const auto &c : combined) {
            const uint8_t *src = c.isNew
                ? newSpkRaw.data() + (size_t)c.idx * spkRowBytes
                : refSpkRaw.data() + (size_t)c.idx * spkRowBytes;
            std::fwrite(src, 1, spkRowBytes, f);
        }
        std::fclose(f);
    }

    // All four .tmp files written successfully — commit via atomic rename.
    atomicRename(outResTmp, outRes);
    atomicRename(outCluTmp, outClu);
    atomicRename(outFetTmp, outFet);
    atomicRename(outSpkTmp, outSpk);

    // ── report ──────────────────────────────────────────────────────────
    int64_t totalDemoted = 0;
    for (size_t k = 0; k < nDemotedPerParent.size(); ++k)
        totalDemoted += nDemotedPerParent[k];

    cout << "process_shadowcluster: group " << grp
         << (inPlace ? "  [in-place merge]" : "  [--out stem differs from --ref]")
         << endl
         << "  reference spikes     : " << nRefSpikes << endl
         << "  new spikes           : " << nNewSpikes << endl
         << "  eligible parents     : " << nEligible << endl
         << "  chi2 threshold (d²)  : " << d2Threshold
         << " (q=" << chi2P << ", df=" << nFeatDim << ")" << endl
         << "  inflation guard      : "
         << (guardEnabled
             ? ("ratio=" + std::to_string(inflationRatio) +
                ", cap=" + std::to_string(inflationCap))
             : std::string("disabled")) << endl
         << "  shadow offset        : " << shadowOffset << endl
         << "  unmatched bin clu id : " << unmatchedShadow << endl;
    for (const auto &s : stats) {
        if (!s.eligible) continue;
        const int64_t nAssigned = nAssignedPerParent[(size_t)s.id];
        const int64_t nDemoted  = nDemotedPerParent[(size_t)s.id];
        if (nAssigned > 0 || nDemoted > 0) {
            cout << "    clu " << s.id
                 << "  (size=" << s.size << ")"
                 << "  shadow=" << (shadowOffset + s.id)
                 << "  +" << nAssigned << " new";
            if (nDemoted > 0)
                cout << "  (demoted " << nDemoted
                     << " to unmatched; cap="
                     << (int64_t)std::max((double)inflationCap,
                                          std::ceil(inflationRatio * (double)s.size))
                     << ")";
            cout << endl;
        }
    }
    cout << "  total demoted        : " << totalDemoted << endl;
    cout << "  unmatched            : " << nUnmatched << endl;
    cout << "  combined nClusters   : " << combinedNClusters << endl;

    return 0;
}
