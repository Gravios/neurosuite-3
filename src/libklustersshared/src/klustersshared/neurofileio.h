/***************************************************************************
 * neurofileio.h
 *
 * Centralised low-level readers/writers for the NeuroSuite on-disk formats
 * (.clu, .res, .fet, .evt, and the interleaved int16 .dat/.lfp signal files).
 *
 * Historically each application re-implemented these parsers: neuroscope's
 * providers, klusters' Data loader, and KlustaKwik's C-style readers each
 * carried their own copy, which is how format drift between the clusterer and
 * the viewers creeps in.  This module is the single source of truth.
 *
 * It is intentionally dependency-light — standard C++ only, no Qt — so every
 * consumer can use it: Qt callers bridge a QString path with toStdString(),
 * KlustaKwik uses std::string directly.  The functions are pure (a path in, a
 * value out); stateful concerns (time-window queries, display buffers, signals)
 * stay in each application's provider/loader on top of these primitives.
 *
 * Format notes (NeuroSuite conventions):
 *   .clu.N  first line is the cluster count, then one cluster id per spike.
 *   .res.N  one sample-index timestamp per spike, no header.
 *   .fet.N  first line is the feature count, then one row of ints per spike.
 *   .evt    one "<time_ms> <label>" per line; time is a float in milliseconds.
 *   .dat    interleaved int16, nbChannels values per sample, no header.
 ***************************************************************************/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "libklustersshared_export.h"

namespace neurofileio {

// ── .clu.N ────────────────────────────────────────────────────────────────
struct CluFile {
    int              nClusters = 0;  ///< value on the header line
    std::vector<int> ids;            ///< one cluster id per spike (header excluded)
    bool             ok = false;     ///< false on open/parse failure
};
KLUSTERSSHARED_EXPORT CluFile readClu(const std::string& path);
KLUSTERSSHARED_EXPORT bool    writeClu(const std::string& path, int nClusters,
                 const std::vector<int>& ids);

// Binary .clu: int32_t cluster-count header, then nSpikes × int32_t ids.
// nSpikes must be known up front (from the matching .res — see below).
KLUSTERSSHARED_EXPORT CluFile readCluBinary(const std::string& path, int64_t nSpikes);

// ── .res.N ────────────────────────────────────────────────────────────────
KLUSTERSSHARED_EXPORT std::vector<int64_t> readRes(const std::string& path, bool* ok = nullptr);
KLUSTERSSHARED_EXPORT bool                 writeRes(const std::string& path,
                              const std::vector<int64_t>& times);

// Binary .res: nSpikes × int64_t timestamps, no header (nSpikes = size/8).
KLUSTERSSHARED_EXPORT std::vector<int64_t> readResBinary(const std::string& path, bool* ok = nullptr);

// ── matched .clu + .res pair (auto-detecting binary vs text) ────────────────
// NeuroSuite cluster data is a .clu/.res pair read together. Some tools write a
// binary variant for fast loading of large datasets; this detects which and
// returns the unified result so no consumer re-implements the detection.
//
// Detection (matches NeuroScope): probe the .res file — binary iff its size is
// a non-zero multiple of 8 AND its first byte is not an ASCII digit; text
// otherwise. The .clu is then read in the same format.
KLUSTERSSHARED_EXPORT bool isBinaryClusterRes(const std::string& resPath);

struct ClusterResData {
    int                  nClusters = 0;
    std::vector<int>     ids;     ///< one cluster id per spike (.clu body)
    std::vector<int64_t> times;   ///< one timestamp per spike (.res)
    bool                 binary = false;  ///< detected format
    bool                 ok = false;      ///< false on open/parse/size mismatch
};
KLUSTERSSHARED_EXPORT ClusterResData readClusterRes(const std::string& cluPath,
                              const std::string& resPath);

// ── .fet.N ────────────────────────────────────────────────────────────────
struct FetFile {
    int                           nFeatures = 0;
    std::vector<std::vector<int>> rows;   ///< nSpikes × nFeatures
    bool                          ok = false;
};
KLUSTERSSHARED_EXPORT FetFile readFet(const std::string& path);

// Binary .fet: int32_t feature-count header, then nSpikes × nFeatures int64
// values, row-major. This is the layout written by the process_pca plugin and
// read by BOTH klusters (Data::loadFeatures) and KlustaKwik (KK::LoadData), so
// it is genuine cross-app duplication. nSpikes is derived from the file size.
struct FetBinaryFile {
    int                  nFeatures = 0;
    int64_t              nSpikes   = 0;
    std::vector<int64_t> values;    ///< row-major, nSpikes × nFeatures
    bool                 ok = false;
};
KLUSTERSSHARED_EXPORT FetBinaryFile readFetBinary(const std::string& path);

// ── .evt ──────────────────────────────────────────────────────────────────
struct EvtEntry {
    double      timeMs = 0.0;
    std::string label;
};
KLUSTERSSHARED_EXPORT std::vector<EvtEntry> readEvt(const std::string& path, bool* ok = nullptr);
KLUSTERSSHARED_EXPORT bool                  writeEvt(const std::string& path,
                               const std::vector<EvtEntry>& events);

// ── .dat / .lfp (interleaved int16) ─────────────────────────────────────────
// Number of samples in the file = fileSize / (nbChannels * 2). Returns -1 if
// the file cannot be opened or nbChannels <= 0.
KLUSTERSSHARED_EXPORT int64_t datSampleCount(const std::string& path, int nbChannels);

// Read nSamples samples (each nbChannels int16) starting at sample startSample.
// `out` must hold at least nSamples*nbChannels int16. Returns the number of
// SAMPLES actually read (may be short at end-of-file), or -1 on open error.
KLUSTERSSHARED_EXPORT int64_t readDatWindow(const std::string& path, int nbChannels,
                      int64_t startSample, int64_t nSamples, int16_t* out);

}  // namespace neurofileio
