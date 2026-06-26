# ---------------------------------------------------------------------------
# libneurosuite-core — Qt-free shared library
# ---------------------------------------------------------------------------
#
# Holds the program-agnostic NeuroSuite primitives that have no Qt (or yaml-cpp)
# dependency, so they can be linked by CLI tools, the C++ ndm plugins, and —
# via a future extern "C" facade — the Python ndm_autoklusta worker, without
# dragging Qt into those consumers.
#
# Currently: neurofileio (canonical .clu/.res/.fet/.evt readers/writers), which
# was relocated out of libklustersshared.  The shared xcorr kernel under
# ../libklustersshared/src/xcorr is intentionally NOT compiled here — consumers
# source-include it so they control the USE_CUDA/HIP/SYCL macros (see the note
# in libklustersshared/src/CMakeLists.txt).  It can migrate here later if a
# linked (rather than source-included) build is ever wanted.
# ---------------------------------------------------------------------------

set(_core_srcs
    neurosuite/core/neurofileio.cpp)

set(_core_hdrs
    neurosuite/core/neurosuite_core_export.h
    neurosuite/core/neurofileio.h
    neurosuite/core/custody.hpp
    neurosuite/core/pca_projection.hpp)

add_library(neurosuite-core SHARED ${_core_srcs})
add_library(Neurosuite::core ALIAS neurosuite-core)

# Emit the export side of the visibility macro while building the lib.
target_compile_definitions(neurosuite-core
    PRIVATE NEUROSUITE_CORE_BUILD_LIB)

# Headers live under src/neurosuite/core/<header>.h so consumers write
#   #include <neurosuite/core/neurofileio.h>
# at both build and install time.
target_include_directories(neurosuite-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# No Qt, no yaml-cpp: pure C++/STL.
set_target_properties(neurosuite-core PROPERTIES
    VERSION   "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}")

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS neurosuite-core
    EXPORT  LibNeurosuiteCoreTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    COMPONENT Devel)

install(FILES ${_core_hdrs}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/neurosuite/core"
    COMPONENT   Devel)
