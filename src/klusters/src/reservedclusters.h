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

    Qt-free, so klusters_test_reservedclusters tests it directly.

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef RESERVEDCLUSTERS_H
#define RESERVEDCLUSTERS_H

#include "types.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace reservedclusters {

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
