/***************************************************************************
                          timer.h  -  description
                             -------------------
    begin                : lun sep 22 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                : lynn.hazan@myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#pragma once

// A process-wide stopwatch for the providers' load-time debug messages:
// RestartTimer() starts it, Timer() returns the seconds since.
//
// std::chrono::steady_clock rather than gettimeofday(): portable (MSVC has
// no <sys/time.h>), and monotonic, so a wall-clock change cannot make a
// measurement negative.  The start point is defined once, in timer.cpp, so
// every translation unit shares it (an earlier header-only version gave each
// TU its own copy).

#include <chrono>

// Defined in timer.cpp — one definition shared by all translation units.
extern std::chrono::steady_clock::time_point timerStart;

inline void RestartTimer()
{
    timerStart = std::chrono::steady_clock::now();
}

inline float Timer()
{
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - timerStart).count();
}
