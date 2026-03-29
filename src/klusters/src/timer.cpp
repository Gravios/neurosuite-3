/***************************************************************************
 * timer.cpp — ODR-safe definition of the shared timer state.
 *
 * timer.h was previously a header-only file that defined `static timeval tv0`
 * directly.  Because timer.h is included by multiple translation units, each
 * TU got its own private copy of tv0, making RestartTimer/Timer pairs in
 * different files measure unrelated clocks.
 *
 * This file provides exactly one definition of tv0; timer.h now declares it
 * `extern` so all TUs share the same object.
 ***************************************************************************/

#include "timer.h"

// One and only definition — shared by all TUs that include timer.h.
struct timeval tv0 = {};
