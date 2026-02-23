project(libklustersshared-src)

include(CMakeFindDependencyMacro)

find_dependency(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

include_directories(${CMAKE_SOURCE_DIR}/src/gui/page)

set(libklustersshared_SRCS
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
)

set(libklustersshared_HEADERS
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
)

qt_add_resources(libklustersshared_SRCS
    gui/libklustersshared-icons.qrc
    gui/libklustersshared-cursors.qrc
)

add_library(klustersshared SHARED ${libklustersshared_SRCS})

target_link_libraries(klustersshared
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

target_include_directories(klustersshared
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/gui>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/shared>
        $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
)

set_target_properties(klustersshared PROPERTIES
    VERSION   ${LIBKLUSTERSSHARED_VERSION}
    SOVERSION ${LIBKLUSTERSSHARED_VERSION_MAJOR}
)

install(TARGETS klustersshared
    EXPORT LibKlustersSharedLibraryTargets
    RUNTIME DESTINATION ${BIN_INSTALL_DIR}
    LIBRARY DESTINATION ${LIB_INSTALL_DIR}
    ARCHIVE DESTINATION ${LIB_INSTALL_DIR} COMPONENT Devel
)
install(FILES ${libklustersshared_HEADERS} DESTINATION ${INCLUDE_INSTALL_DIR} COMPONENT Devel)
install(DIRECTORY images DESTINATION ${EXEC_INSTALL_PREFIX}/share/doc/)
