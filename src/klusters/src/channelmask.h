/***************************************************************************
 * channelmask.h — restrict channel-major waveforms to a channel subset.
 *
 * Shared by the waveform-based matrix threads (template, residual, drift) so
 * they all interpret KlustersDoc::selectedChannels() the same way.  Qt-free and
 * header-only so the maths can be unit-tested in isolation.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef CHANNELMASK_H
#define CHANNELMASK_H

#include <algorithm>
#include <vector>

/**
 * Normalise a raw channel selection into a usable keep-list.
 *
 * Returns an EMPTY vector to mean "no restriction — use every channel", which
 * is both the no-selection case and the degenerate cases (out-of-range indices
 * only, or every channel selected: restricting to all of them is the same as
 * not restricting).  Otherwise returns the in-range indices, sorted and
 * de-duplicated.
 */
inline std::vector<int> cmResolveMask(const std::vector<int>& selection, int nChan)
{
    std::vector<int> keep;
    if (nChan <= 0) return keep;
    for (int c : selection)
        if (c >= 0 && c < nChan
            && std::find(keep.begin(), keep.end(), c) == keep.end())
            keep.push_back(c);
    std::sort(keep.begin(), keep.end());
    if (static_cast<int>(keep.size()) == nChan) keep.clear();   // all == none
    return keep;
}

/**
 * Compact a channel-major waveform [ch * nSamp + sm] down to the channels in
 * @p keep, preserving their order.  out has keep.size() * nSamp points.
 *
 * The channels are COPIED OUT rather than zeroed in place.  Zeroing looks
 * equivalent but is not: the shape metrics correlate over small temporal lags,
 * and at a non-zero lag a zeroed channel of A still multiplies against a live
 * channel of B, so the masked channels keep contributing to B's norm and the
 * correlation comes out wrong.  Compacting removes them from the maths.
 */
inline void cmCompactChannels(const std::vector<float>& full, int nChan, int nSamp,
                              const std::vector<int>& keep, std::vector<float>& out)
{
    if (keep.empty() || nSamp <= 0) { out = full; return; }
    out.assign(keep.size() * static_cast<size_t>(nSamp), 0.0f);
    for (size_t k = 0; k < keep.size(); ++k) {
        const int c = keep[k];
        if (c < 0 || c >= nChan) continue;
        const size_t src = static_cast<size_t>(c) * nSamp;
        const size_t dst = k * static_cast<size_t>(nSamp);
        std::copy(full.begin() + src, full.begin() + src + nSamp, out.begin() + dst);
    }
}

/**
 * Compact a per-channel scalar array (e.g. the drift matrix's site depths) to
 * the same subset, so it stays aligned with cmCompactChannels' output.
 */
inline void cmCompactPerChannel(const std::vector<float>& full,
                                const std::vector<int>& keep,
                                std::vector<float>& out)
{
    if (keep.empty()) { out = full; return; }
    out.clear();
    out.reserve(keep.size());
    for (int c : keep)
        if (c >= 0 && c < static_cast<int>(full.size()))
            out.push_back(full[static_cast<size_t>(c)]);
}

#endif // CHANNELMASK_H
