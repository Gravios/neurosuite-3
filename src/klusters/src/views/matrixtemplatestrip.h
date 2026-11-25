/***************************************************************************
 * matrixtemplatestrip.h — the marked-node template region shared by the four
 * curation matrices (§11.5).
 *
 * The error, template, residual and drift matrices share no render base
 * (matrixgrid.h explains why), so when the marked-node template columns were
 * added (0621–0626) every view grew its own copy of the SAME plumbing: the
 * template-column storage, the per-cell time-overlap shade (the spike-time
 * window loop), the strip-room reservation, and the click hit-test.  That is the
 * duplication claude/template-curation-audit.md calls out (D2/D3/D5); this struct
 * is recommendation 1 — the shared pieces in ONE place.
 *
 * A view owns one MatrixTemplateStrip.  It keeps for itself only the parts that
 * genuinely differ: the per-cell VALUE (xcorr / residual / drift-xcorr, or grey
 * for the error matrix), the colour ramp, and the mean-size `hasData` predicate it
 * feeds to computeShade.  Everything else — cols(), stripCells(), shade(t,j),
 * hitTest() — comes from here, so a change like 0626 (the tri-state shade) is now
 * a one-place edit.
 *
 * Qt-light on purpose (QColor/QPainter come in only through matrixtemplatecols.h):
 * computeShade needs KlustersDoc, so it is declared here and defined in the .cpp,
 * keeping the heavy document/data includes out of every view that includes this.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MATRIXTEMPLATESTRIP_H
#define MATRIXTEMPLATESTRIP_H

#include <vector>
#include <functional>
#include <QList>
#include <QPointF>

#include "matrixtemplatecols.h"   // MatrixTemplateCol, MatrixStripShade, matrixStripHitTest, gap

class KlustersDoc;

/** Owns the marked-node template columns and their per-cell time-overlap shade
 *  for one curation matrix, plus the region geometry.  See the file header. */
struct MatrixTemplateStrip {
    /** Replace the template columns (invalidates the shade until the next
     *  computeShade).  The view calls this from its setTemplateColumns. */
    void setColumns(const std::vector<MatrixTemplateCol>& c) { cols_ = c; shade_.clear(); }

    const std::vector<MatrixTemplateCol>& cols() const { return cols_; }
    bool empty() const { return cols_.empty(); }
    int  size()  const { return static_cast<int>(cols_.size()); }   // number of template columns M

    /** Cells to reserve past the N-cluster block so the region fits on screen
     *  (a gap + M template cells), 0 when there is no region.  Each view adds this
     *  to its cluster count in updateWindow/recomputeCellWidth. */
    int stripCells() const
        { return cols_.empty() ? 0 : (kTemplateStripGapCells + static_cast<int>(cols_.size())); }

    /** The shade for template column t against cluster j (MatrixStripGrey until
     *  computeShade has run, or for an out-of-range index). */
    MatrixStripShade shade(int t, int j) const {
        if (t < 0 || t >= static_cast<int>(shade_.size()) ||
            j < 0 || j >= static_cast<int>(shade_[static_cast<std::size_t>(t)].size()))
            return MatrixStripGrey;
        return static_cast<MatrixStripShade>(shade_[static_cast<std::size_t>(t)][static_cast<std::size_t>(j)]);
    }

    /** Recompute the per-cell shade from each cluster's spike-time RANGE against
     *  each node's [a,b] window: Grey where `hasData(t,j)` is false (no comparable
     *  mean), Dim where the cluster has spikes but none inside the window (the
     *  value is still shown, at half alpha), Value where it overlaps.  This is the
     *  spike-time loop that used to be copied into all three waveform matrices.
     *  Defined in the .cpp so this header stays free of the document/data headers. */
    void computeShade(KlustersDoc& doc, const QList<int>& clusterList,
                      const std::function<bool(int t, int j)>& hasData);

    /** Resolve a widget-space click (px,py) against the region drawn at (oriF,eff)
     *  for an N-cluster matrix.  `ids` is the cluster id at each matrix row/column
     *  in VISUAL order — the plain cluster list for the three fixed-order matrices,
     *  the display-ordered list for the reorderable error matrix — so the hit names
     *  the cluster the user sees.  ok=false when the click is outside the region. */
    MatrixStripHit hitTest(double px, double py, const QPointF& oriF, double eff,
                           const QList<int>& ids) const
        { return matrixStripHitTest(px, py, oriF, eff, ids, cols_); }

private:
    std::vector<MatrixTemplateCol>          cols_;    // the marked-node template columns (empty = no region)
    std::vector<std::vector<unsigned char>> shade_;   // [tpl][cluster] MatrixStripShade; empty until computeShade
};

#endif // MATRIXTEMPLATESTRIP_H
