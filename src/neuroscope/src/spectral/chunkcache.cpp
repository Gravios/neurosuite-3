#include "chunkcache.h"

#include <algorithm>
#include <cmath>

namespace neuroscope {
namespace spectral {

SpectralChunkCache::SpectralChunkCache() = default;

SpectralChunkCache::~SpectralChunkCache()
{
    stop();
}

void SpectralChunkCache::setCompute(ComputeFn fn)
{
    std::lock_guard<std::mutex> lk(mu_);
    compute_ = std::move(fn);
}

void SpectralChunkCache::configure(int numChunks, int radius)
{
    {
        std::lock_guard<std::mutex> lk(mu_);
        numChunks_ = std::max(0, numChunks);
        radius_    = std::max(0, radius);
        center_    = std::clamp(center_, 0, numChunks_ > 0 ? numChunks_ - 1 : 0);
        evictOutsideWindowLocked();
    }
    cv_.notify_all();
}

void SpectralChunkCache::setParams(const SpectralParams& params)
{
    {
        std::lock_guard<std::mutex> lk(mu_);
        params_ = params;
        ++epoch_;            // any in-flight result for the old epoch is dropped
        ready_.clear();
        attempted_.clear();
    }
    cv_.notify_all();
}

void SpectralChunkCache::setCenter(int chunkIndex)
{
    {
        std::lock_guard<std::mutex> lk(mu_);
        const int clamped = std::clamp(chunkIndex, 0, numChunks_ > 0 ? numChunks_ - 1 : 0);
        if (clamped == center_ && !ready_.empty()) {
            // No move; still make sure the worker is awake for any gaps.
        }
        center_ = clamped;
        evictOutsideWindowLocked();
    }
    cv_.notify_all();
}

void SpectralChunkCache::start()
{
    std::lock_guard<std::mutex> lk(mu_);
    if (running_) return;
    running_       = true;
    stopRequested_ = false;
    worker_ = std::thread([this] { workerLoop(); });
}

void SpectralChunkCache::stop()
{
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!running_) return;
        stopRequested_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
    std::lock_guard<std::mutex> lk(mu_);
    running_ = false;
}

bool SpectralChunkCache::inWindowLocked(int idx) const
{
    if (idx < 0 || idx >= numChunks_) return false;
    return idx >= center_ - radius_ && idx <= center_ + radius_;
}

void SpectralChunkCache::evictOutsideWindowLocked()
{
    for (auto it = ready_.begin(); it != ready_.end(); ) {
        if (!inWindowLocked(it->first)) it = ready_.erase(it);
        else ++it;
    }
    for (auto it = attempted_.begin(); it != attempted_.end(); ) {
        if (!inWindowLocked(*it)) it = attempted_.erase(it);
        else ++it;
    }
}

int SpectralChunkCache::pickNextLocked() const
{
    // Nearest-to-centre chunk in the window that is neither ready nor a failed
    // attempt. Expand outward from the centre so the visible region fills first.
    for (int d = 0; d <= radius_; ++d) {
        for (int s = 0; s <= (d == 0 ? 0 : 1); ++s) {
            const int idx = center_ + (s == 0 ? -d : d);
            if (!inWindowLocked(idx)) continue;
            if (ready_.count(idx) || attempted_.count(idx)) continue;
            return idx;
        }
    }
    return -1;
}

void SpectralChunkCache::workerLoop()
{
    std::unique_lock<std::mutex> lk(mu_);
    while (!stopRequested_) {
        const int idx = pickNextLocked();
        if (idx < 0) {
            cv_.wait(lk);
            continue;
        }
        const SpectralParams p = params_;
        const long epoch = epoch_;
        ComputeFn fn = compute_;
        lk.unlock();

        SpectralImage img;
        const bool ok = fn ? fn(idx, p, img) : false;

        lk.lock();
        if (epoch != epoch_) continue;          // params changed under us: drop
        if (!inWindowLocked(idx)) continue;      // scrolled away: drop
        if (ok) ready_[idx] = std::move(img);
        else    attempted_.insert(idx);          // don't spin on an uncomputable chunk
    }
}

bool SpectralChunkCache::tryGet(int chunkIndex, SpectralImage& out) const
{
    std::lock_guard<std::mutex> lk(mu_);
    auto it = ready_.find(chunkIndex);
    if (it == ready_.end()) return false;
    out = it->second;
    return true;
}

bool SpectralChunkCache::isReady(int chunkIndex) const
{
    std::lock_guard<std::mutex> lk(mu_);
    return ready_.count(chunkIndex) != 0;
}

int SpectralChunkCache::readyCount() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return static_cast<int>(ready_.size());
}

int SpectralChunkCache::center() const
{
    std::lock_guard<std::mutex> lk(mu_);
    return center_;
}

} // namespace spectral
} // namespace neuroscope
