/***************************************************************************
 * correlationgrid.h — choose the time interval between correlogram grid lines.
 *
 * The interval has to track the Duration: a step that reads well at 30 ms is
 * far too dense at 900 ms.  Qt-free and header-only so the choice can be
 * unit-tested against the durations the UI actually offers.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef CORRELATIONGRID_H
#define CORRELATIONGRID_H

#include <cmath>

/**
 * Time between grid lines / tick marks (ms) for a correlogram of full width
 * @p durationMs (the Duration box: the correlogram spans ±durationMs/2).
 *
 * Picks the "nice" step nearest to durationMs/8, from the 1-2-5 progression
 * floored at 5: 5, 10, 20, 50, 100, 200, 500, 1000...  Every candidate is a
 * round multiple of 5, and aiming at 8 divisions keeps the count readable
 * across the range (roughly 6-9 lines end to end).
 *
 * Returns 0 for a non-positive duration, i.e. "draw no grid".
 */
inline int cvGridStepMs(int durationMs)
{
    if (durationMs <= 0) return 0;

    const double target = durationMs / 8.0;
    int    best    = 5;
    double bestErr = std::fabs(target - 5.0);

    for (long decade = 1; decade <= 100000L; decade *= 10) {
        const int mantissas[3] = { 5, 10, 20 };   // -> 5,10,20 | 50,100,200 | ...
        for (int m : mantissas) {
            const long cand = static_cast<long>(m) * decade;
            if (cand > 1000000L) continue;
            const double err = std::fabs(target - static_cast<double>(cand));
            if (err < bestErr) { bestErr = err; best = static_cast<int>(cand); }
        }
    }
    return best;
}

#endif // CORRELATIONGRID_H
