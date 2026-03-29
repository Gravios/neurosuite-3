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

// ---- ODR fix (2025-03) -----------------------------------------------
// The original timer.h defined `static struct timeval tv0` directly in the
// header.  Because timer.h is included by 5 separate translation units in
// neuroscope, each TU got its own private copy of tv0 (C++ [basic.def.odr]).
// That meant RestartTimer() in one file had no effect on Timer() in another —
// measurements were silently wrong.
//
// Fix: tv0 is now declared `extern` here and *defined* exactly once in
// timer.cpp.  All TUs that include this header share the same object.
// struct timezone is POSIX-obsolescent; gettimeofday tz arg is now nullptr.
// -----------------------------------------------------------------------

#include <sys/time.h>

// Defined in timer.cpp — one definition shared by all translation units.
extern struct timeval tv0;

inline void RestartTimer()
{
    gettimeofday(&tv0, nullptr);
}

inline float Timer()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    const float msec  = static_cast<int>(tv.tv_usec  / 1000) / 1000.0f;
    const float msec0 = static_cast<int>(tv0.tv_usec / 1000) / 1000.0f;
    return (tv.tv_sec + msec) - (tv0.tv_sec + msec0);
}
