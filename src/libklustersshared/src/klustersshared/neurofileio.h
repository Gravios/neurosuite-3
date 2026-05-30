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

namespace neurofileio {

// ── .clu.N ────────────────────────────────────────────────────────────────
struct CluFile {
    int              nClusters = 0;  ///< value on the header line
    std::vector<int> ids;            ///< one cluster id per spike (header excluded)
    bool             ok = false;     ///< false on open/parse failure
};
CluFile readClu(const std::string& path);
bool    writeClu(const std::string& path, int nClusters,
                 const std::vector<int>& ids);

// ── .res.N ────────────────────────────────────────────────────────────────
std::vector<int64_t> readRes(const std::string& path, bool* ok = nullptr);
bool                 writeRes(const std::string& path,
                              const std::vector<int64_t>& times);

// ── .fet.N ────────────────────────────────────────────────────────────────
struct FetFile {
    int                           nFeatures = 0;
    std::vector<std::vector<int>> rows;   ///< nSpikes × nFeatures
    bool                          ok = false;
};
FetFile readFet(const std::string& path);

// ── .evt ──────────────────────────────────────────────────────────────────
struct EvtEntry {
    double      timeMs = 0.0;
    std::string label;
};
std::vector<EvtEntry> readEvt(const std::string& path, bool* ok = nullptr);
bool                  writeEvt(const std::string& path,
                               const std::vector<EvtEntry>& events);

// ── .dat / .lfp (interleaved int16) ─────────────────────────────────────────
// Number of samples in the file = fileSize / (nbChannels * 2). Returns -1 if
// the file cannot be opened or nbChannels <= 0.
int64_t datSampleCount(const std::string& path, int nbChannels);

// Read nSamples samples (each nbChannels int16) starting at sample startSample.
// `out` must hold at least nSamples*nbChannels int16. Returns the number of
// SAMPLES actually read (may be short at end-of-file), or -1 on open error.
int64_t readDatWindow(const std::string& path, int nbChannels,
                      int64_t startSample, int64_t nSamples, int16_t* out);

}  // namespace neurofileio
