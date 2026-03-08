/***************************************************************************
 *   Copyright (C) 2003 by Lynn Hazan                                      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef PAIR_H
#define PAIR_H

#include <QString>
#include <utility>

// ---------------------------------------------------------------------------
// Pair — ordered (x, y) integer pair used as a correlation dictionary key.
//
// The underlying storage is std::pair<int,int>.  The only extra facility
// is pairKey(x, y) which produces the canonical QString key used in
// correlationDict and related maps.
// ---------------------------------------------------------------------------

using Pair = std::pair<int, int>;

/// Returns the canonical string key for a (x, y) cluster pair.
[[nodiscard]] inline QString pairKey(int x, int y)
{
    return QStringLiteral("%1-%2").arg(x).arg(y);
}

/// Convenience overload accepting a Pair directly.
[[nodiscard]] inline QString pairKey(Pair p)
{
    return pairKey(p.first, p.second);
}

#endif // PAIR_H
