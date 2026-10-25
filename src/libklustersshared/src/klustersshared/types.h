/***************************************************************************
                          types.h  -  description
                             -------------------
    begin                : Mon Mar 1 2004
    copyright            : (C) 2004 by Lynn Hazan
    email                : lynn.hazan.myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef TYPES_H
#define TYPES_H

typedef long dataType;
typedef long long recordingUnitType;

/** Reserved cluster ids: the top 100 ids a float32 holds exactly
 *  (2^24-100 .. 2^24-1).  fiber-kit's chunk runs (FK_CHUNKS) write every
 *  spike of a chunk they did not compute to cluster kReservedClusterLo + chunk,
 *  so a partial sort still has one label per spike.  Cluster-id allocators
 *  (Data::highestClusterId) skip this block, so new clusters are always
 *  numbered below it.  Must match fiber_kit/fiber_chunks.py RESERVED_LO/HI
 *  (fiber-kit test_shared_contract checks it). */
constexpr dataType kReservedClusterLo = 16777116;   // 2^24 - 100
constexpr dataType kReservedClusterHi = 16777215;   // 2^24 - 1
constexpr bool isReservedClusterId(dataType id)
{
    return id >= kReservedClusterLo && id <= kReservedClusterHi;
}

#endif //TYPES_H
