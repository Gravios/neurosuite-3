/***************************************************************************
 * matrixdisplayorder.h -- the display permutation shared by the four curation
 * matrices (error / template / residual / drift).
 *
 * A matrix view keeps its rows in the order its thread computed them
 * (clusterList[i] is the cluster of 0-based matrix row i).  A SORT under the
 * child scope must not renumber atoms, so it rearranges the DISPLAY instead:
 * the document holds the sorted atom ids (KlustersDoc::scopeSortOrderList) and
 * every matrix derives, from those ids and its own clusterList, a permutation
 * "display position -> matrix row".  The four matrices therefore read in the
 * same order whatever each one's thread produced, and the permutation is
 * re-derived after every recompute while the scope and its held order last.
 *
 * Header-only, QtCore only; the views compose one and route every row/column
 * index through it (toMatrix for drawing and hit tests, toDisplay for marking
 * a pair, displayList for helpers that take the cluster list positionally).
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MATRIXDISPLAYORDER_H
#define MATRIXDISPLAYORDER_H

#include <QList>
#include <QSet>

#include <vector>

struct MatrixDisplayOrder {
    /** display position -> 0-based matrix row; empty = identity. */
    QList<int> perm;

    bool active() const { return !perm.isEmpty(); }
    void clear() { perm.clear(); }

    /** 0-based display position -> 0-based matrix row (identity when unset or
     *  out of range, so a stale permutation can never index past the matrix). */
    int toMatrix(int d) const {
        return (d >= 0 && d < perm.size()) ? perm[d] : d;
    }
    /** 0-based matrix row -> 0-based display position (identity when unset). */
    int toDisplay(int m) const {
        if (perm.isEmpty()) return m;
        const int d = perm.indexOf(m);
        return d >= 0 ? d : m;
    }
    /** The cluster list in display order (for helpers that read it positionally). */
    QList<int> displayList(const QList<int>& clusterList) const {
        if (perm.size() != clusterList.size()) return clusterList;
        QList<int> out;
        out.reserve(clusterList.size());
        for (int m : perm) out.append(clusterList[m]);
        return out;
    }

    /** Install an explicit permutation over n rows; anything that is not a
     *  permutation of 0..n-1 clears to identity (completeness is correctness:
     *  a partial map would hide rows). */
    bool set(const QList<int>& order, int n) {
        if (order.size() != n) { perm.clear(); return false; }
        std::vector<bool> seen(static_cast<size_t>(n), false);
        for (int v : order) {
            if (v < 0 || v >= n || seen[static_cast<size_t>(v)]) { perm.clear(); return false; }
            seen[static_cast<size_t>(v)] = true;
        }
        perm = order;
        return true;
    }

    /** The permutation a HELD id order means for this clusterList: the
     *  specials (ids 0/1) keep the front in their own order, the held ids
     *  follow in the held order (ids absent from the rows simply do not
     *  place -- an atom that died since the sort), and rows the order does
     *  not name append in natural order, so the result is always complete.
     *  An empty held order yields the identity (an empty perm). */
    static QList<int> derive(const QList<int>& clusterList, const QList<int>& held) {
        QList<int> out;
        if (held.isEmpty()) return out;
        out.reserve(clusterList.size());
        QSet<int> placed;
        for (int i = 0; i < clusterList.size(); ++i)
            if (clusterList[i] <= 1) { out.append(i); placed.insert(i); }
        for (int cid : held) {
            const int i = clusterList.indexOf(cid);
            if (i >= 0 && !placed.contains(i)) { out.append(i); placed.insert(i); }
        }
        for (int i = 0; i < clusterList.size(); ++i)
            if (!placed.contains(i)) out.append(i);
        return out;
    }

    /** derive() + set(): the one call a view makes after a recompute or when
     *  the held order changes.  Returns whether a non-identity order is on. */
    bool apply(const QList<int>& clusterList, const QList<int>& held) {
        const QList<int> p = derive(clusterList, held);
        if (p.isEmpty()) { perm.clear(); return false; }
        set(p, clusterList.size());
        return active();
    }
};

#endif // MATRIXDISPLAYORDER_H
