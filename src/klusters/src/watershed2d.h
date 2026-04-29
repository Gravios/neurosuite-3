#ifndef KLUSTERS_WATERSHED2D_H
#define KLUSTERS_WATERSHED2D_H

// ---------------------------------------------------------------------------
// 2D density watershed for klusters.
//
// Build a histogram on a configurable square grid (default 256×256) over
// the active scatter view's two feature dimensions; smooth with a small
// Gaussian; find local maxima (peaks); region-grow each peak using a
// priority queue to assign every grid cell to one basin (the "rainfall"
// formulation); then map each input point back to the basin its cell
// belongs to.
//
// Public API:
//   - Watershed2D::Config — algorithm tuning
//   - Watershed2D::Result — basin labels per input point + diagnostic
//     fields for status display
//   - Watershed2D::run    — performs the segmentation
//
// The algorithm operates on (x, y) coordinates already projected to the
// scatter-view's two chosen dimensions.  No knowledge of clusters or
// of klusters' Data layer.  Caller maps the resulting basin labels back
// to feature-row indices and feeds them to
// Data::createNewClustersFromLabeling.
//
// Performance: O(N) for histogram + smoothing + label assignment, plus
// the priority-queue flood from peaks; for typical N≤500k spikes and
// 256×256 grid this completes in tens of milliseconds — fast enough to
// run on the GUI thread without a worker.
// ---------------------------------------------------------------------------

#include <vector>
#include <cstdint>

namespace Watershed2D {

struct Config {
    int    gridSize        = 256;     ///< sqrt of histogram bins (square grid)
    double smoothSigma     = 0;       ///< gaussian sigma in cells; 0 = auto
    double minPeakHeight   = 0;       ///< drop peaks below this absolute density; 0 = auto (5% of grid max)
    int    minBasinSize    = 0;       ///< merge basins smaller than this; 0 = auto from N
    bool   useLocalMaxima  = true;    ///< treat 8-connected local maxima as seeds (vs. h-maxima)
    bool   keepGrid        = false;   ///< populate Result::smoothedGrid + cellLabels (for UI preview)
};

struct Result {
    /// basin label per input point, 1-indexed.  0 = unassigned (point
    /// fell outside any basin, e.g. on the histogram floor of zero
    /// density after smoothing).
    std::vector<int>  pointLabels;

    int               numBasins   = 0;     ///< distinct labels >= 1 in pointLabels
    int               numPeaks    = 0;     ///< pre-merge peak count
    bool              ok          = false; ///< false if input degenerate (all points same coord, etc.)
    double            xMin = 0, xMax = 0;
    double            yMin = 0, yMax = 0;

    /// Echoed-back effective parameters (after auto-tune, if requested)
    /// so callers can show them in a UI and use them as initial slider
    /// positions for follow-up runs.
    double            effSigma       = 0;
    double            effPeakHeight  = 0;
    int               effMinBasinSize = 0;

    /// 256x256 (or whatever gridSize) row-major float densities AFTER
    /// smoothing.  Useful for rendering a preview heatmap in the UI.
    /// Empty if not requested via Config::keepGrid.
    std::vector<float> smoothedGrid;
    int                gridSize  = 0;
    /// Per-cell basin label (matching smoothedGrid, row-major). 0 = unassigned.
    /// Empty if not requested via Config::keepGrid.
    std::vector<int>   cellLabels;
};

/// Run watershed on the given (x, y) point cloud.
/// @param xs  input x coordinates, length N
/// @param ys  input y coordinates, length N
/// @param cfg algorithm parameters (defaults are sensible for spike-feature data)
Result run(const std::vector<double>& xs,
           const std::vector<double>& ys,
           const Config& cfg = Config());

}  // namespace Watershed2D

#endif  // KLUSTERS_WATERSHED2D_H
