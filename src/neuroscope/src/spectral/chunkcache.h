#ifndef NEUROSCOPE_SPECTRAL_CHUNKCACHE_H
#define NEUROSCOPE_SPECTRAL_CHUNKCACHE_H

// Sliding-window cache of precomputed spectrogram chunks.
//
// The recording timeline is divided into fixed-size chunks. A background thread
// computes the chunks that fall within a movable window [centre-radius,
// centre+radius] (nearest the centre first); chunks outside the window are
// evicted, so memory stays bounded while scrolling stays instant. The actual
// per-chunk computation is delegated to a callback, so this cache is independent
// of the data source (the live view supplies traces+engine; tests supply a
// synthetic function). The class is Qt-free and self-contained.

#include "spectralengine.h"

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace neuroscope {
namespace spectral {

class SpectralChunkCache {
public:
    // Compute the spectrogram for chunk `index` under `params`, writing it into
    // `out`. Return false if the chunk can't be produced (e.g. out of range);
    // the cache will not retry it until the params change or it is evicted.
    using ComputeFn = std::function<bool(int index, const SpectralParams& params,
                                         SpectralImage& out)>;

    SpectralChunkCache();
    ~SpectralChunkCache();

    SpectralChunkCache(const SpectralChunkCache&) = delete;
    SpectralChunkCache& operator=(const SpectralChunkCache&) = delete;

    // Set the per-chunk compute callback. Call before start().
    void setCompute(ComputeFn fn);

    // Total number of chunks in the recording and the window half-width (chunks
    // kept on each side of the centre). radius < 0 is treated as 0.
    void configure(int numChunks, int radius);

    // Replace the estimator parameters; invalidates every cached chunk and any
    // in-flight result, and restarts the fill around the current centre.
    void setParams(const SpectralParams& params);

    // Move the window centre (clamped to [0, numChunks)). Evicts chunks that
    // fall outside the new window and wakes the worker to fill the rest.
    void setCenter(int chunkIndex);

    void start();   // launch the background worker (idempotent)
    void stop();    // stop and join the worker (idempotent)

    // Thread-safe read: copies the chunk's image into `out` and returns true if
    // that chunk is ready, otherwise returns false.
    bool tryGet(int chunkIndex, SpectralImage& out) const;

    bool isReady(int chunkIndex) const;
    int  readyCount() const;
    int  center() const;

private:
    void workerLoop();
    int  pickNextLocked() const;          // nearest in-window, not ready/attempted; -1 if none
    bool inWindowLocked(int idx) const;
    void evictOutsideWindowLocked();

    mutable std::mutex          mu_;
    std::condition_variable     cv_;
    std::thread                 worker_;
    bool                        running_       = false;
    bool                        stopRequested_ = false;

    ComputeFn                   compute_;
    SpectralParams              params_;
    long                        epoch_   = 0;   // bumped by setParams to drop stale work
    int                         numChunks_ = 0;
    int                         radius_  = 8;
    int                         center_  = 0;

    std::map<int, SpectralImage> ready_;        // chunkIndex -> computed image
    std::set<int>                attempted_;     // chunks whose compute returned false
};

} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_CHUNKCACHE_H
