/***************************************************************************
 * driftgeometry.h
 *
 * Resolve per-channel probe depth (y, µm) for a spike group, reading the same
 * probe geometry the rest of the toolchain uses: the session YAML `probes:`
 * section (via klustersshared/readProbesSection) plus each referenced `.probe`
 * file's `sites.geometry` list.  Used by the drift matrix to shift cluster
 * mean waveforms along the depth axis.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef DRIFTGEOMETRY_H
#define DRIFTGEOMETRY_H

#include <QList>
#include <QString>
#include <vector>

/**
 * @brief Per-channel site depth (µm) for one spike group.
 *
 * @param yamlPath       Path to the session `.yaml` (e.g. KlustersDoc::url()).
 * @param groupChannels  The group's global ADC channel indices, in the group's
 *                       channel order; the result is returned in that same order.
 * @param err            If non-null, set to a human-readable message on failure.
 * @return               Depth (y, µm) per channel, or an empty vector if the
 *                       probes section / .probe geometry could not be resolved.
 *
 * A channel c belongs to the probe whose half-open ADC range (channelOffset up
 * to but excluding channelOffset + nSites) contains it; its site index within
 * that probe's flat geometry list is (c - channelOffset).  The y coordinate of
 * that site is the depth (each shank's sites run y = 0 at the head to the tip,
 * so within a single-shank spike group y is the local depth axis drift moves
 * along).
 */
std::vector<float> loadGroupChannelDepths(const QString& yamlPath,
                                          const QList<int>& groupChannels,
                                          QString* err = nullptr);

#endif // DRIFTGEOMETRY_H
