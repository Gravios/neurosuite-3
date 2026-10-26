/***************************************************************************
    reservedclusters.h
    ------------------
    What an edit in Klusters may do to a reserved cluster.

    A fiber-kit chunk run (FK_CHUNKS) sorts only some chunks of a recording
    and puts every spike of each other chunk in that chunk's reserved
    cluster, kReservedClusterLo + chunk (types.h, isReservedClusterId).
    Klusters protects those clusters:

      - Nothing is added to one.  Data::prepareUndo, the commit point of
        every edit, refuses an edit that puts a spike into a reserved
        cluster it was not in (firstReservedGain below).
      - Only reclustering and selections in the feature space take spikes
        out of one: the Recluster menu, DipSplit, Watershed, and the lasso
        tools (new cluster, split, delete to artefact or noise, decollide).
        Every other edit leaves reserved clusters out of the clusters it
        works on (withoutReserved): group, auto-merge, delete clusters,
        the strips, purge, partition by time, the hierarchy's merges, the
        batch decollide.
      - Renumbering and sorting keep a reserved id as it is and leave the
        cluster out of the order.
      - The matrices (error, template, residual, drift, merge
        recommendations) leave reserved clusters out.

    Klusters shows a reserved id as R<chunk>, R0 .. R99 (label below,
    clusterLabel in clusterlabel.h): chunk 14's cluster 16777130 is R14.
    Files, logs and plugin arguments keep the number.

    Qt-free, so klusters_test_reservedclusters tests it directly.

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef RESERVEDCLUSTERS_H
#define RESERVEDCLUSTERS_H

#include "types.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace reservedclusters {

/// The chunk a reserved id holds (id - kReservedClusterLo: 0 .. 99), or -1 for
/// any other id.  It is the chunk number fiber-kit uses: FK_CHUNKS counts the
/// chunks from 0, and chunk k's spikes go to kReservedClusterLo + k.
constexpr int chunkOf(long long id)
{
    return (id >= kReservedClusterLo && id <= kReservedClusterHi)
               ? static_cast<int>(id - kReservedClusterLo) : -1;
}

/// How Klusters shows a cluster id: "R<chunk>" for a reserved id (R0 .. R99),
/// the number for any other.
inline std::string label(long long id)
{
    const int chunk = chunkOf(id);
    return chunk >= 0 ? "R" + std::to_string(chunk) : std::to_string(id);
}

/// The cluster id a typed label names: "R<chunk>" or "r<chunk>" (chunk 0 .. 99)
/// or a number, digits only; -1 for any other text.
inline long long idFromLabel(std::string_view text)
{
    const bool reserved = !text.empty() && (text.front() == 'R' || text.front() == 'r');
    if (reserved) text.remove_prefix(1);
    if (text.empty() || text.size() > 12) return -1;
    long long n = 0;
    for (const char ch : text) {
        if (ch < '0' || ch > '9') return -1;
        n = n * 10 + (ch - '0');
    }
    if (!reserved) return n;
    return n <= kReservedClusterHi - kReservedClusterLo ? kReservedClusterLo + n : -1;
}

/// @p ids without the reserved ones, in their order (QList<int>, std::vector, ...).
template <class Ids>
Ids withoutReserved(const Ids& ids)
{
    Ids out;
    for (const auto& id : ids)
        if (!isReservedClusterId(static_cast<dataType>(id))) out.push_back(id);
    return out;
}

/// The reserved ids of @p ids, in their order.
template <class Ids>
Ids reservedOf(const Ids& ids)
{
    Ids out;
    for (const auto& id : ids)
        if (isReservedClusterId(static_cast<dataType>(id))) out.push_back(id);
    return out;
}

static_assert(kReservedClusterHi - kReservedClusterLo < 255,
              "firstReservedGain keeps one reserved cluster per byte");

/// The reserved id that gains a spike when a labelling becomes another, or -1.
/// A labelling is a list of (feature row, cluster id) by 0-based position:
/// @p oldRow(i) / @p oldId(i) for i < @p nOld before the edit, @p newRow(i) /
/// @p newId(i) for i < @p nNew after it.  Rows are 1-based feature rows up to
/// @p nbRows.  The result is the first reserved id that the new labelling gives
/// a row the old one did not give it; a row outside 1..nbRows under a reserved
/// id counts as such a gain.  Taking spikes out of a reserved cluster is not a
/// gain.  @p scratch (one byte per row) is reused between calls.
template <class OldRow, class OldId, class NewRow, class NewId>
dataType firstReservedGain(std::size_t nOld, OldRow oldRow, OldId oldId,
                           std::size_t nNew, NewRow newRow, NewId newId,
                           dataType nbRows, std::vector<unsigned char>& scratch)
{
    scratch.assign(static_cast<std::size_t>(nbRows) + 1, 0);
    for (std::size_t i = 0; i < nOld; ++i) {
        const dataType id = oldId(i);
        if (!isReservedClusterId(id)) continue;
        const dataType r = oldRow(i);
        if (r >= 1 && r <= nbRows)
            scratch[static_cast<std::size_t>(r)] = static_cast<unsigned char>(id - kReservedClusterLo + 1);
    }
    for (std::size_t i = 0; i < nNew; ++i) {
        const dataType id = newId(i);
        if (!isReservedClusterId(id)) continue;
        const dataType r = newRow(i);
        if (r < 1 || r > nbRows
            || scratch[static_cast<std::size_t>(r)] != static_cast<unsigned char>(id - kReservedClusterLo + 1))
            return id;
    }
    return -1;
}

}  // namespace reservedclusters

#endif // RESERVEDCLUSTERS_H
