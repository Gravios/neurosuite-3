# ---------------------------------------------------------------------------
# klustersshared shared library
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
    shared/channelcolors.cpp
    shared/itemcolors.cpp
    shared/zoomwindow.cpp
    shared/utilities.cpp
    shared/progressbar.cpp
    shared/parameteryamlreader.cpp
    shared/parameteryamlmodifier.cpp)

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
    shared/channelcolors.h
    shared/itemcolors.h
    shared/types.h
    shared/array.h
    shared/utilities.h
    shared/zoomwindow.h
    shared/customtypes.h
    shared/progressbar.h
    shared/parameteryamlreader.h
    shared/parameteryamlmodifier.h)

qt_add_resources(_srcs
    gui/libklustersshared-icons.qrc
    gui/libklustersshared-cursors.qrc)

add_library(klustersshared SHARED ${_srcs})
add_library(KlustersShared::klustersshared ALIAS klustersshared)

# Export macro so klustersshared_export.h emits the right declspec/visibility
target_compile_definitions(klustersshared
    PRIVATE KLUSTERSSHARED_BUILD_KLUSTERSSHARED_LIB)

# Include paths: build-tree consumers get the source dirs; install-tree
# consumers get the single flattened installed include prefix.
target_include_directories(klustersshared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gui>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/gui/page>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/shared>
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
