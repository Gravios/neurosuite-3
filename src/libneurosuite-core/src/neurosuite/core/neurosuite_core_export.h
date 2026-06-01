#ifndef NEUROSUITE_CORE_EXPORT_H
#define NEUROSUITE_CORE_EXPORT_H

/* Visibility macros for libneurosuite-core.
 *
 * Unlike libklustersshared's export header, this carries NO Qt dependency —
 * it uses plain compiler visibility attributes so the core library (and the
 * `extern "C"` facade that will sit on top of it for the Python ndm_autoklusta
 * FFI) can be built and linked without pulling in Qt.
 *
 * NEUROSUITE_CORE_BUILD_LIB is defined (PRIVATE) only while building the
 * library itself; consumers leave it undefined and get the import side.
 */

#ifdef NEUROSUITE_CORE_STATIC
#  define NEUROSUITE_CORE_EXPORT
#else
#  if defined(_WIN32) || defined(__CYGWIN__)
#    ifdef NEUROSUITE_CORE_BUILD_LIB
#      define NEUROSUITE_CORE_EXPORT __declspec(dllexport)
#    else
#      define NEUROSUITE_CORE_EXPORT __declspec(dllimport)
#    endif
#  else
#    define NEUROSUITE_CORE_EXPORT __attribute__((visibility("default")))
#  endif
#endif

#endif /* NEUROSUITE_CORE_EXPORT_H */
