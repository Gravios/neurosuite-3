# ---------------------------------------------------------------------------
# neuroscope executable
# ---------------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/../config-neuroscope.h.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/config-neuroscope.h")

set(_srcs
    main.cpp
    neuroscope.cpp
    neuroscopeview.cpp
    baseframe.cpp
    channelgroupview.cpp
    channeliconview.cpp
    channelpalette.cpp
    channelmimedata.cpp
    clustercolors.cpp
    clusterproperties.cpp
    clustersprovider.cpp
    configuration.cpp
    dataprovider.cpp
    eventsprovider.cpp
    itemgroupview.cpp
    itemiconview.cpp
    itempalette.cpp
    neuroscopedoc.cpp
    neuroscopeyamlreader.cpp
    sessionyamlwriter.cpp
    sessionyamlreader.cpp
    positionproperties.cpp
    positionsprovider.cpp
    positionview.cpp
    prefdefaults.cpp
    prefdialog.cpp
    prefgeneral.cpp
    properties.cpp
    propertiesdialog.cpp
    timer.cpp           # ODR-safe definition of tv0 (shared by all timer.h includers)
    tracesprovider.cpp
    traceview.cpp
    spectralview.cpp
    tracewidget.cpp
    imagecreator.cpp
    globaleventsprovider.cpp
    eventdata.cpp
    # AUTOUIC processes these automatically — list them so CMake tracks them
    clusterpropertieslayout.cpp
    positionpropertieslayout.cpp
    prefdefaultslayout.cpp
    prefgenerallayout.cpp
    propertieslayout.cpp)

qt_add_resources(_srcs neuroscope-icons.qrc)

# Spectral DSP core (Qt-free static library; used by the spectral view).
add_subdirectory(spectral)

# WIN32 suppresses the console window on Windows; no-op on Linux/macOS
add_executable(neuroscope $<$<PLATFORM_ID:Windows>:WIN32> ${_srcs})

target_include_directories(neuroscope PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")   # for config-neuroscope.h and AUTOUIC output

target_link_libraries(neuroscope PRIVATE
    KlustersShared::klustersshared
    neuroscope-spectral
    neuroscope-spectral-render
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS neuroscope
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

# Application icons
foreach(_size IN ITEMS 16 22 32 48 64)
    install(FILES "hi${_size}-app-neuroscope.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps"
        RENAME neuroscope.png)
    install(FILES "hi${_size}-nphys-nrs.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/mimetypes"
        RENAME application-nphys-nrs.png)
endforeach()

install(FILES neuroscope.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES neuroscope.xml
    DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages")
