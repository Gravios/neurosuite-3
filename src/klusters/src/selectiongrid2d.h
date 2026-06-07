/***************************************************************************
 *                          selectiongrid2d.h                              *
 *                                                                         *
 *   Uniform 2D spatial grid for accelerating polygon spike selection.     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/
#ifndef SELECTIONGRID2D_H
#define SELECTIONGRID2D_H

#include "types.h" // dataType (== long); Qt-free

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

/**
 * SelectionGrid2D — broad-phase index for "which spikes fall inside this
 * polygon" in a 2D feature projection.
 *
 * Klusters identifies the spikes a lasso/polygon selects by scanning every
 * spike of the edited clusters and testing point-in-region — O(spikes in those
 * clusters).  For a high-firing-rate cluster that scan dominates the *other*
 * half of reassignment latency (the commit half is addressed by
 * SpikeAssignment).
 *
 * This grid buckets points by cell; a query tests only the points in cells that
 * overlap the polygon's bounding box, then applies the exact point-in-polygon
 * predicate (even-odd ray casting) to those candidates.  Because any point
 * inside the polygon lies within the polygon's bbox, and a point's cell is
 * computed by the same mapping used to derive the queried cell range, the
 * pruning never drops a true positive (no false negatives).
 *
 * Build is O(N); rebuild only when the displayed dimension pair changes, so the
 * cost amortises over many lassos.  Qt-free and self-contained for unit testing.
 *
 * NOTE ON PREDICATE PARITY: the narrow phase here is float ray casting.  When
 * wired into Klusters the narrow-phase predicate must match whatever the editing
 * path uses (QRegion::contains on integer pixel coordinates); the grid only
 * changes *which* points are tested, not the predicate.  Keep the predicate in
 * one place so the grid and the non-accelerated path agree exactly.
 */
class SelectionGrid2D
{
public:
    using Spike = dataType;

    SelectionGrid2D() = default;

    /** Build over points (xs[i], ys[i]) belonging to spike ids[i].  The three
     *  arrays are parallel and equal length; ids lets the grid cover a subset
     *  (e.g. only the clusters being edited).  @p targetPerCell tunes grid
     *  resolution (average points per occupied cell). */
    void build(const std::vector<float> &xs,
               const std::vector<float> &ys,
               const std::vector<Spike> &ids,
               int targetPerCell = 8)
    {
        xs_ = xs;
        ys_ = ys;
        ids_ = ids;
        const std::size_t n = xs_.size();
        cells_.clear();

        if (n == 0) {
            nCols_ = nRows_ = 0;
            return;
        }

        minX_ = maxX_ = xs_[0];
        minY_ = maxY_ = ys_[0];
        for (std::size_t i = 1; i < n; ++i) {
            minX_ = std::min(minX_, xs_[i]);
            maxX_ = std::max(maxX_, xs_[i]);
            minY_ = std::min(minY_, ys_[i]);
            maxY_ = std::max(maxY_, ys_[i]);
        }

        // Aim for ~targetPerCell points per cell, square-ish grid.
        const double cellsWanted =
            std::max(1.0, double(n) / std::max(1, targetPerCell));
        int side = std::max(1, int(std::ceil(std::sqrt(cellsWanted))));
        nCols_ = side;
        nRows_ = side;

        // Guard degenerate extents (all points share an x or y).
        spanX_ = double(maxX_) - double(minX_);
        spanY_ = double(maxY_) - double(minY_);
        if (spanX_ <= 0.0) { nCols_ = 1; spanX_ = 1.0; }
        if (spanY_ <= 0.0) { nRows_ = 1; spanY_ = 1.0; }

        cells_.assign(std::size_t(nCols_) * nRows_, {});
        for (std::size_t i = 0; i < n; ++i)
            cells_[cellIndex(colOf(xs_[i]), rowOf(ys_[i]))].push_back(
                std::uint32_t(i));
    }

    std::size_t size() const { return xs_.size(); }
    int cols() const { return nCols_; }
    int rows() const { return nRows_; }

    /** Spike ids whose (x,y) is inside the closed polygon (polyX[k],polyY[k]).
     *  The polygon is implicitly closed (last vertex back to first). */
    std::vector<Spike> query(const std::vector<float> &polyX,
                             const std::vector<float> &polyY) const
    {
        std::vector<Spike> out;
        if (xs_.empty() || polyX.size() < 3 || polyX.size() != polyY.size())
            return out;

        float pxMin = polyX[0], pxMax = polyX[0], pyMin = polyY[0], pyMax = polyY[0];
        for (std::size_t k = 1; k < polyX.size(); ++k) {
            pxMin = std::min(pxMin, polyX[k]); pxMax = std::max(pxMax, polyX[k]);
            pyMin = std::min(pyMin, polyY[k]); pyMax = std::max(pyMax, polyY[k]);
        }

        const int c0 = colOf(pxMin), c1 = colOf(pxMax);
        const int r0 = rowOf(pyMin), r1 = rowOf(pyMax);

        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
                for (std::uint32_t idx : cells_[cellIndex(c, r)])
                    if (pointInPolygon(xs_[idx], ys_[idx], polyX, polyY))
                        out.push_back(ids_[idx]);
        return out;
    }

    /** Exact even-odd ray-cast point-in-polygon test (shared narrow phase). */
    static bool pointInPolygon(float x, float y,
                               const std::vector<float> &px,
                               const std::vector<float> &py)
    {
        bool inside = false;
        const std::size_t n = px.size();
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            const bool crosses = ((py[i] > y) != (py[j] > y));
            if (crosses) {
                const float xCross =
                    (px[j] - px[i]) * (y - py[i]) / (py[j] - py[i]) + px[i];
                if (x < xCross)
                    inside = !inside;
            }
        }
        return inside;
    }

private:
    int colOf(float x) const
    {
        int c = int((double(x) - double(minX_)) / spanX_ * nCols_);
        return std::clamp(c, 0, nCols_ - 1);
    }
    int rowOf(float y) const
    {
        int r = int((double(y) - double(minY_)) / spanY_ * nRows_);
        return std::clamp(r, 0, nRows_ - 1);
    }
    std::size_t cellIndex(int c, int r) const
    {
        return std::size_t(r) * nCols_ + c;
    }

    std::vector<float> xs_, ys_;
    std::vector<Spike> ids_;
    std::vector<std::vector<std::uint32_t>> cells_;
    int nCols_ = 0, nRows_ = 0;
    float minX_ = 0, maxX_ = 0, minY_ = 0, maxY_ = 0;
    double spanX_ = 1.0, spanY_ = 1.0;
};

#endif // SELECTIONGRID2D_H
