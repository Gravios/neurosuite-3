# ---------------------------------------------------------------------------
# klustersshared shared library
# ---------------------------------------------------------------------------
#
# Note: the `xcorr/` subdirectory is NOT part of this Qt6/yaml-cpp target.
# It contains the shared normalised-xcorr kernel relocated from
# src/shared/xcorr/ in patch66 (canonical home for what was previously
# duplicated between src/shared/xcorr and src/klusters/src/realign_xcorr_*).
# It's a pure-C++ kernel with no Qt/yaml deps; consumers (klustakwik,
# klustakwikExp, klusters) include its sources directly into their own
# targets so they control the USE_CUDA/USE_HIP/USE_SYCL macros that the
# dispatcher conditionally compiles against.  See each consumer's
# CMakeLists for the include-and-build wiring (_xcorr_dir variable).
# ---------------------------------------------------------------------------
set(_srcs
    gui/qrecentfileaction.cpp
    gui/qcolorbutton.cpp
    gui/page/qpagedialog.cpp
    gui/page/qpagemodel.cpp
    gui/page/qpageview.cpp
    gui/page/qpageview_p.cpp
    gui/page/qpagewidget.cpp
    gui/page/qpagewidgetmodel.cpp
    gui/page/qextenddialog.cpp
    gui/page/klusterseparator.cpp
    gui/dockarea.cpp
    gui/qextendtabwidget.cpp
    gui/qhelpviewer.cpp
    gui/klusterrubberband.cpp
    klustersshared/channelcolors.cpp
    klustersshared/itemcolors.cpp
    klustersshared/zoomwindow.cpp
    klustersshared/utilities.cpp
    klustersshared/progressbar.cpp
    klustersshared/parameteryamlreader.cpp
    klustersshared/parameteryamlmodifier.cpp
    klustersshared/parameteryamlwriter.cpp
    klustersshared/parameteryamlreader_probes.cpp
    klustersshared/neurofileio.cpp)

set(_hdrs
    libklustersshared_export.h
    gui/qrecentfileaction.h
    gui/qcolorbutton.h
    gui/page/qpagedialog.h
    gui/page/qextenddialog.h
    gui/page/qpagewidget.h
    gui/page/qpagemodel.h
    gui/page/qpageview.h
    gui/page/qpagewidgetmodel.h
    gui/dockarea.h
    gui/qextendtabwidget.h
    gui/qhelpviewer.h
    gui/klusterrubberband.h
    klustersshared/channelcolors.h
    klustersshared/itemcolors.h
    klustersshared/types.h
    klustersshared/array.h
    klustersshared/utilities.h
    klustersshared/zoomwindow.h
    klustersshared/customtypes.h
    klustersshared/progressbar.h
    klustersshared/parameteryamlreader.h
    klustersshared/parameteryamlmodifier.h
    klustersshared/parameteryamlwriter.h
    klustersshared/parameteryamlreader_probes.h
    klustersshared/generalinformation.h
    klustersshared/fileinformation.h
    klustersshared/programinformation.h
    klustersshared/neuroscopevideoinfo.h
    klustersshared/channelcolorentry.h
    klustersshared/neurofileio.h)

qt_add_resources(_srcs
    gui/libklustersshared-icons.qrc
    gui/libklustersshared-cursors.qrc)

add_library(klustersshared SHARED ${_srcs})
add_library(KlustersShared::klustersshared ALIAS klustersshared)

# Export macro so klustersshared_export.h emits the right declspec/visibility
target_compile_definitions(klustersshared
    PRIVATE KLUSTERSSHARED_BUILD_KLUSTERSSHARED_LIB)

# Include paths.
# The source directory is laid out so that headers live under
#   src/klustersshared/<header>.h
# which means #include <klustersshared/header.h> resolves directly at both
# build time (CMAKE_CURRENT_SOURCE_DIR = .../libklustersshared/src/) and
# install time (${CMAKE_INSTALL_INCLUDEDIR}/klustersshared/).
# No symlinks or generator-expression tricks are required.
target_include_directories(klustersshared
    PUBLIC
        # Build-tree consumers: the src/ parent is exposed so that
        # #include <klustersshared/header.h> resolves to src/klustersshared/header.h.
        # The klustersshared/ subdir is also exposed so that headers *inside*
        # that subdir can include their siblings as <sibling.h> (no prefix),
        # mirroring the flat install layout where both the parent and the
        # klustersshared/ subdir are on the include path.
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/klustersshared>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gui>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gui/page>
        # Install-tree consumers get both the parent (for <klustersshared/X.h>)
        # and the subdir (for <X.h> bare includes).
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/klustersshared>)

target_link_libraries(klustersshared
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        yaml-cpp)

set_target_properties(klustersshared PROPERTIES
    VERSION   "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}")

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS klustersshared
    EXPORT  LibKlustersSharedTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    COMPONENT Devel)

install(FILES ${_hdrs}
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/klustersshared"
    COMPONENT   Devel)

# Images are embedded in the .so via the .qrc resources; nothing to install.
