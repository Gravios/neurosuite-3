/***************************************************************************
 * timer.cpp — the one definition of the shared stopwatch start (timer.h).
 *
 * Defined here, not in the header, so every translation unit that includes
 * timer.h shares one start point (C++ [basic.def.odr]).
 ***************************************************************************/

#include "timer.h"

std::chrono::steady_clock::time_point timerStart;
