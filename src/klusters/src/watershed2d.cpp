#include "watershed2d.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>
#include <unordered_map>

namespace Watershed2D {

namespace {

inline int idx(int x, int y, int W) { return y * W + x; }

/// Separable 1D Gaussian convolution along x, then y, into `dst`.
/// Operates on float density grids of shape [size × size].  Reflects at
/// boundaries.  `sigma` is the Gaussian standard deviation in cells.
/// Kernel half-window is set to ceil(3*sigma) so >99% of the mass is
/// captured.
void gaussianBlur(std::vector<float>& grid, int size, double sigma)
{
    if (sigma < 0.5) return;

    const int  radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    const int  k = 2 * radius + 1;
    std::vector<float> kernel(k);
    {
        double sum = 0.0;
        for (int i = 0; i < k; ++i) {
            const double x = i - radius;
            kernel[i] = static_cast<float>(std::exp(-x * x / (2 * sigma * sigma)));
            sum += kernel[i];
        }
        const float inv = static_cast<float>(1.0 / sum);
        for (float& w : kernel) w *= inv;
    }

    std::vector<float> tmp(grid.size(), 0.0f);

    // Horizontal pass.
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float acc = 0.0f;
            for (int i = 0; i < k; ++i) {
                int xi = x + i - radius;
                if      (xi < 0)     xi = -xi;
                else if (xi >= size) xi = 2 * size - xi - 1;
                acc += kernel[i] * grid[idx(xi, y, size)];
            }
            tmp[idx(x, y, size)] = acc;
        }
    }

    // Vertical pass.
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float acc = 0.0f;
            for (int i = 0; i < k; ++i) {
                int yi = y + i - radius;
                if      (yi < 0)     yi = -yi;
                else if (yi >= size) yi = 2 * size - yi - 1;
                acc += kernel[i] * tmp[idx(x, yi, size)];
            }
            grid[idx(x, y, size)] = acc;
        }
    }
}

}  // namespace

Result run(const std::vector<double>& xs,
           const std::vector<double>& ys,
           const Config& cfg)
{
    Result R;
    const int N = static_cast<int>(xs.size());
    if (N == 0 || xs.size() != ys.size()) return R;

    R.pointLabels.assign(N, 0);

    // ── Bounding box on input ────────────────────────────────────────────
    double xMin = xs[0], xMax = xs[0], yMin = ys[0], yMax = ys[0];
    for (int i = 1; i < N; ++i) {
        if (xs[i] < xMin) xMin = xs[i]; else if (xs[i] > xMax) xMax = xs[i];
        if (ys[i] < yMin) yMin = ys[i]; else if (ys[i] > yMax) yMax = ys[i];
    }
    R.xMin = xMin; R.xMax = xMax; R.yMin = yMin; R.yMax = yMax;
    if (xMax <= xMin || yMax <= yMin) return R;     // degenerate

    const int W = std::max(8, cfg.gridSize);
    const double sx = (W - 1) / (xMax - xMin);
    const double sy = (W - 1) / (yMax - yMin);

    // ── Histogram ────────────────────────────────────────────────────────
    std::vector<float> grid(static_cast<size_t>(W) * W, 0.0f);
    std::vector<int>   cellOf(N, 0);
    for (int i = 0; i < N; ++i) {
        int gx = static_cast<int>((xs[i] - xMin) * sx);
        int gy = static_cast<int>((ys[i] - yMin) * sy);
        if (gx < 0) gx = 0; else if (gx >= W) gx = W - 1;
        if (gy < 0) gy = 0; else if (gy >= W) gy = W - 1;
        cellOf[i] = idx(gx, gy, W);
        grid[cellOf[i]] += 1.0f;
    }

    // ── Smooth ───────────────────────────────────────────────────────────
    // Auto-tune smoothing if sigma <= 0: pick radius proportional to
    // grid size (heuristic ~W/32) so the kernel covers a region large
    // enough to merge Poisson-noise micro-peaks within a real density
    // basin without smearing distinct basins together.  This is the
    // single most important parameter for getting reasonable basin
    // counts on real spike-feature distributions.
    double sigma = cfg.smoothSigma;
    if (sigma <= 0.0) sigma = std::max(1.5, double(W) / 32.0);
    R.effSigma = sigma;
    gaussianBlur(grid, W, sigma);

    // Compute global max of smoothed grid for auto-tuning of peak height.
    float gridMax = 0.0f;
    for (float v : grid) if (v > gridMax) gridMax = v;

    // Auto-tune: if minPeakHeight is the sentinel 0, set it to a small
    // fraction of the grid max.  This ensures the threshold scales with
    // the data's density distribution rather than a fixed value that
    // might be either useless (too small) or impossible (too big).
    float effPeakHeight = static_cast<float>(cfg.minPeakHeight);
    if (effPeakHeight <= 0.0f)
        effPeakHeight = 0.05f * gridMax;     // default: 5% of grid max
    R.effPeakHeight = effPeakHeight;

    // ── Peak detection: 8-connected local maxima above effPeakHeight ────
    // Strict-or-tied: a cell is a peak iff (a) no neighbour is strictly
    // greater AND (b) at least one neighbour is strictly less (excludes
    // perfectly flat plateaus, which the histogram on integer counts
    // can produce even after Gaussian smoothing for empty regions).
    std::vector<int> labels(grid.size(), 0);
    int nextLabel = 1;
    std::vector<std::pair<int,int>> peakCoords;     // (x, y)
    for (int y = 0; y < W; ++y) {
        for (int x = 0; x < W; ++x) {
            const float v = grid[idx(x, y, W)];
            if (v < effPeakHeight) continue;
            bool isMax       = true;
            bool hasStrictLt = false;
            for (int dy = -1; dy <= 1 && isMax; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= W || ny >= W) continue;
                    const float nv = grid[idx(nx, ny, W)];
                    if (nv > v)   { isMax = false; break; }
                    if (nv < v)   { hasStrictLt = true; }
                }
            }
            if (isMax && hasStrictLt) {
                labels[idx(x, y, W)] = nextLabel++;
                peakCoords.push_back({x, y});
            }
        }
    }
    R.numPeaks = static_cast<int>(peakCoords.size());

    if (peakCoords.empty()) return R;     // no basins

    // ── Region-grow with priority queue ──────────────────────────────────
    // Highest-density unlabeled cells get assigned first.  When a cell
    // has multiple labeled neighbours with different labels, it becomes
    // the watershed boundary (left at 0).  Otherwise it inherits its
    // labeled neighbour's label.
    struct Cell { float density; int x, y; };
    struct CellLess {
        bool operator()(const Cell& a, const Cell& b) const {
            return a.density < b.density;     // max-heap
        }
    };
    std::priority_queue<Cell, std::vector<Cell>, CellLess> pq;

    for (auto pc : peakCoords) {
        const int x = pc.first, y = pc.second;
        // Push the peak's labeled-neighbour candidates.
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= W || ny >= W) continue;
                if (labels[idx(nx, ny, W)] != 0) continue;
                Cell c{ grid[idx(nx, ny, W)], nx, ny };
                pq.push(c);
            }
        }
    }

    while (!pq.empty()) {
        Cell c = pq.top(); pq.pop();
        const int cellIdx = idx(c.x, c.y, W);
        if (labels[cellIdx] != 0) continue;

        // Inspect 8-neighbours: collect the set of distinct labels
        // already assigned among them.
        int neighborLabel = 0;
        bool conflict = false;
        for (int dy = -1; dy <= 1 && !conflict; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = c.x + dx, ny = c.y + dy;
                if (nx < 0 || ny < 0 || nx >= W || ny >= W) continue;
                const int lab = labels[idx(nx, ny, W)];
                if (lab == 0) continue;
                if (neighborLabel == 0) {
                    neighborLabel = lab;
                } else if (lab != neighborLabel) {
                    conflict = true;
                    break;
                }
            }
        }

        if (conflict) {
            // Watershed line — keep at 0.
            continue;
        }

        if (neighborLabel == 0) continue;   // (shouldn't happen if seeded correctly)

        labels[cellIdx] = neighborLabel;

        // Push unlabeled 4-neighbours into the queue.
        const int dxs[4] = { -1,  1,  0,  0 };
        const int dys[4] = {  0,  0, -1,  1 };
        for (int n = 0; n < 4; ++n) {
            const int nx = c.x + dxs[n], ny = c.y + dys[n];
            if (nx < 0 || ny < 0 || nx >= W || ny >= W) continue;
            if (labels[idx(nx, ny, W)] != 0) continue;
            Cell next{ grid[idx(nx, ny, W)], nx, ny };
            pq.push(next);
        }
    }

    // ── Map points back to basins ────────────────────────────────────────
    for (int i = 0; i < N; ++i) {
        R.pointLabels[i] = labels[cellOf[i]];
    }

    // ── Drop tiny basins (assign their points to label 0) ────────────────
    // Auto-tune: 0 sentinel means "1% of total points, floor 20".
    int effMinBasinSize = cfg.minBasinSize;
    if (effMinBasinSize <= 0)
        effMinBasinSize = std::max(20, N / 100);
    R.effMinBasinSize = effMinBasinSize;

    {
        std::unordered_map<int, int> basinSize;
        for (int lab : R.pointLabels)
            if (lab > 0) ++basinSize[lab];

        std::unordered_map<int, bool> tooSmall;
        for (auto& kv : basinSize)
            if (kv.second < effMinBasinSize) tooSmall[kv.first] = true;

        if (!tooSmall.empty()) {
            for (int& lab : R.pointLabels)
                if (lab > 0 && tooSmall.count(lab)) lab = 0;
        }
    }

    // ── Renumber kept basins to 1..k contiguously ────────────────────────
    std::unordered_map<int, int> remap;
    int nextRemap = 1;
    for (int lab : R.pointLabels)
        if (lab > 0 && !remap.count(lab)) remap[lab] = nextRemap++;
    if (!remap.empty()) {
        for (int& lab : R.pointLabels)
            if (lab > 0) lab = remap[lab];
    }
    R.numBasins = static_cast<int>(remap.size());
    R.ok        = (R.numBasins > 0);

    // ── Optionally retain grid + cell labels for UI preview ─────────────
    // If keepGrid was requested we ALSO renumber cellLabels via the same
    // remap so the preview heatmap colours match the per-point labels.
    if (cfg.keepGrid) {
        R.gridSize     = W;
        R.smoothedGrid = std::move(grid);
        if (!remap.empty()) {
            for (int& cl : labels)
                if (cl > 0) {
                    auto it = remap.find(cl);
                    cl = (it == remap.end()) ? 0 : it->second;
                }
        }
        R.cellLabels   = std::move(labels);
    }

    return R;
}

}  // namespace Watershed2D
