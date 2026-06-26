// pca_projection.hpp — PCA basis load + projection energy, the primitive shared
// by every spike-realignment path (klusters realignSpikes, the
// process_alignspikes_pca plugin, and — as a device mirror — pca_refine_cuda).
//
// Header-only and DEPENDENCY-FREE (std only; no Qt, no libneurosuite-core
// linkage), like custody.hpp, so the Qt apps and the standalone ndmanager-plugins
// binaries that do not link the core library can use it by #include alone.  Kept
// C++17-clean because the plugin target compiles as cxx_std_17.
//
// This is the variance-neutral PCA-projection energy only.  The *policy* that
// consumes it — per-spike argmax (klusters --pca-refine) vs global-mean argmax
// (plugin Stage 2) vs RMS-centroid recenter — stays at the call site; see
// docs/pca-align-refine-behavioral-diff.md.
//
//   .pca / .pcaD file format — PCAE (the single canonical format):
//     header [int32]: magic = 0x50434145 ("PCAE"), version,
//       nChannels, data2use, nComponents, recShift, isCentered,
//       [v2+: method, nInputChannels]
//     then, BLOCK-WISE:
//       nCh × (data2use × double)           : per-channel means, ALWAYS present
//       nCh × (data2use × nComp × double)   : per-channel eigenvectors, col-major
//                                             (ev[k*data2use+u])
//     The means block is written unconditionally; `isCentered` governs only
//     whether the means are SUBTRACTED at projection time, not their presence.
//     Block-wise (all means, then all eigenvectors) is canonical: it is what
//     process_pca writes and what klusters / shadowcluster / KK read.

#ifndef NEUROSUITE_PCA_PROJECTION_HPP
#define NEUROSUITE_PCA_PROJECTION_HPP

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace neurosuite {
namespace core {

// Spatial-derivative order (the extractor's SdiffOrder, kept here so callers
// need not include the plugin header).
enum class SdiffOrder : int32_t { None = 0, First = 1, Laplacian = 2, AllPairs = 3 };

// The transform a basis was trained against — a single explicit method rather
// than a (transform × order) cross-product, so an illegal combination cannot be
// encoded.  Stored in the PCAE header; every consumer applies exactly this.
//   Standard : raw waveform, no transform.
//   Sdiff*   : spatial derivative only (no temporal first-difference).
//   Stderiv* : spatial derivative + temporal first-difference (the clustering space).
// (Spatial order NONE is meaningful only as Standard, so it is not enumerated for
//  Sdiff/Stderiv; pure-temporal "Stderiv-none" is not a named pipeline method.)
enum class Method : int32_t {
    Standard         = 0,
    SdiffFirst       = 1,
    SdiffLaplacian   = 2,
    SdiffAllPairs    = 3,
    StderivFirst     = 4,
    StderivLaplacian = 5,
    StderivAllPairs  = 6,   // canonical (the YAML default)
};

inline bool methodValid(int32_t v) { return v >= 0 && v <= 6; }

// Spatial-derivative order a method applies.
inline SdiffOrder spatialOrder(Method m) {
    switch (m) {
        case Method::SdiffFirst:     case Method::StderivFirst:     return SdiffOrder::First;
        case Method::SdiffLaplacian: case Method::StderivLaplacian: return SdiffOrder::Laplacian;
        case Method::SdiffAllPairs:  case Method::StderivAllPairs:  return SdiffOrder::AllPairs;
        case Method::Standard:       default:                       return SdiffOrder::None;
    }
}

// Whether a method applies the temporal first-difference (stderiv vs sdiff/raw).
inline bool hasTemporalDiff(Method m) {
    return m == Method::StderivFirst || m == Method::StderivLaplacian
        || m == Method::StderivAllPairs;
}

// Chain-of-custody method tag for the filename / YAML cross-check.
inline const char* methodTag(Method m) {
    switch (m) {
        case Method::SdiffFirst: case Method::SdiffLaplacian: case Method::SdiffAllPairs:
            return "sdiff";
        case Method::StderivFirst: case Method::StderivLaplacian: case Method::StderivAllPairs:
            return "stderiv";
        case Method::Standard: default:
            return "standard";
    }
}

struct PcaBasis {
    int    nCh = 0, data2use = 0, nComp = 0, recShift = 0;
    bool   centered = false;
    Method method = Method::Standard;  // transform the basis was trained against
    int    nInputChannels = 0;         // raw channels the transform consumes
                                       // (0 ⇒ == nCh, i.e. no channel drop)
    std::vector<std::vector<double>> means;  // [ch][data2use]
    std::vector<std::vector<double>> evec;   // [ch][data2use × nComp], col-major
    bool valid() const { return nCh > 0 && data2use > 0 && nComp > 0; }
    // Raw channels consumed by the transform: the stderiv all-pairs build drops
    // the trailing linearly-dependent channel, so nInputChannels = nCh + 1 there;
    // 0 means "no drop" (nInputChannels == nCh).
    int inputChannels() const { return nInputChannels > 0 ? nInputChannels : nCh; }
};

inline constexpr int32_t kPcaeMagic   = 0x50434145;  // "PCAE"
inline constexpr int32_t kPcaeVersion = 2;           // v2 adds method + nInputChannels

// Load a PCAE .pca / .pcaD basis.  Returns false on open / bad-magic /
// unsupported-version / bad-method / short-read.  Block-wise body: ALL
// per-channel means first (always present), then ALL per-channel eigenvectors.
// v1 files (no method) load as Standard with nInputChannels == nCh.
inline bool loadPca(const std::string& path, PcaBasis& pca) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    int32_t h[2];
    if (std::fread(h, 4, 2, f) != 2)        { std::fclose(f); return false; }
    if (h[0] != kPcaeMagic)                 { std::fclose(f); return false; }  // not PCAE
    const int32_t version = h[1];
    if (version != 1 && version != 2)       { std::fclose(f); return false; }  // unsupported
    // Common 5 fields: nChannels, data2use, nComponents, recShift, isCentered.
    int32_t core5[5];
    if (std::fread(core5, 4, 5, f) != 5)    { std::fclose(f); return false; }
    pca.nCh      = core5[0];
    pca.data2use = core5[1];
    pca.nComp    = core5[2];
    pca.recShift = core5[3];
    pca.centered = (core5[4] != 0);
    if (version == 2) {
        int32_t ext[2];                     // method, nInputChannels
        if (std::fread(ext, 4, 2, f) != 2)  { std::fclose(f); return false; }
        if (!methodValid(ext[0]))           { std::fclose(f); return false; }
        pca.method         = static_cast<Method>(ext[0]);
        pca.nInputChannels = ext[1];
    } else {                                // v1: no transform descriptor
        pca.method         = Method::Standard;
        pca.nInputChannels = 0;             // ⇒ == nCh
    }
    if (!pca.valid()) { std::fclose(f); return false; }
    const size_t d2u  = static_cast<size_t>(pca.data2use);
    const size_t evSz = d2u * static_cast<size_t>(pca.nComp);
    pca.means.assign(pca.nCh, std::vector<double>(d2u, 0.0));
    pca.evec.assign(pca.nCh, std::vector<double>(evSz, 0.0));
    for (int ch = 0; ch < pca.nCh; ++ch)
        if (std::fread(pca.means[ch].data(), 8, d2u, f) != d2u)
        { std::fclose(f); return false; }
    for (int ch = 0; ch < pca.nCh; ++ch)
        if (std::fread(pca.evec[ch].data(), 8, evSz, f) != evSz)
        { std::fclose(f); return false; }
    std::fclose(f);
    return true;
}

// Write a PCAE v2 .pca / .pcaD basis (the single canonical writer).  Layout is
// the exact inverse of loadPca; means are always emitted regardless of
// `centered`.  Returns false on open / short-write.
inline bool writePca(const std::string& path, const PcaBasis& pca) {
    if (!pca.valid()) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const int32_t hdr[9] = {
        kPcaeMagic, kPcaeVersion,
        pca.nCh, pca.data2use, pca.nComp, pca.recShift, pca.centered ? 1 : 0,
        static_cast<int32_t>(pca.method), pca.inputChannels()
    };
    bool ok = (std::fwrite(hdr, 4, 9, f) == 9);
    const size_t d2u  = static_cast<size_t>(pca.data2use);
    const size_t evSz = d2u * static_cast<size_t>(pca.nComp);
    for (int ch = 0; ch < pca.nCh && ok; ++ch)
        ok = (std::fwrite(pca.means[ch].data(), 8, d2u, f) == d2u);
    for (int ch = 0; ch < pca.nCh && ok; ++ch)
        ok = (std::fwrite(pca.evec[ch].data(), 8, evSz, f) == evSz);
    std::fclose(f);
    return ok;
}
// PCA-projection energy of a channel-major waveform (wf[ch*nSamp + t]) against
// the basis:  E = Σ_ch Σ_k ( Σ_u ev[k·data2use+u]·(x[recShift+u] − μ[u]) )².
// Uses min(pca.nCh, nChan) channels so a stderiv basis trained on nChan−1
// channels still projects.  The sIdx bounds guard is a no-op whenever the
// header invariant recShift + data2use ≤ nSamp holds; it is kept so a stray
// shifted window can never read out of bounds.
inline double pcaProjectionEnergy(const int16_t* wfChannelMajor,
                                  int nChan, int nSamp,
                                  const PcaBasis& pca) {
    const int chForPca = std::min(pca.nCh, nChan);
    double energy = 0.0;
    for (int ch = 0; ch < chForPca; ++ch) {
        const std::vector<double>& mu = pca.means[ch];
        const std::vector<double>& ev = pca.evec[ch];
        for (int k = 0; k < pca.nComp; ++k) {
            double score = 0.0;
            for (int u = 0; u < pca.data2use; ++u) {
                const int sIdx = pca.recShift + u;
                if (sIdx < 0 || sIdx >= nSamp) continue;
                double x = static_cast<double>(wfChannelMajor[ch * nSamp + sIdx]);
                if (pca.centered) x -= mu[u];
                score += ev[static_cast<size_t>(k * pca.data2use + u)] * x;
            }
            energy += score * score;
        }
    }
    return energy;
}

} // namespace core
} // namespace neurosuite

#endif // NEUROSUITE_PCA_PROJECTION_HPP
