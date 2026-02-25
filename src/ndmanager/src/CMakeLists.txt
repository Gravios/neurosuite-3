# ---------------------------------------------------------------------------
# ndmanager executable
# ---------------------------------------------------------------------------
set(_srcs
    acquisitionsystempage.cpp
    anatomypage.cpp
    channelcolors.cpp
    channelcolorspage.cpp
    channeloffsetspage.cpp
    clusterspage.cpp
    descriptionwriter.cpp
    fileinformation.cpp
    filepage.cpp
    filespage.cpp
    generalinfopage.cpp
    generalinformation.cpp
    helper.cpp
    launcherpage.cpp
    lfppage.cpp
    main.cpp
    managerview.cpp
    miscellaneouspage.cpp
    ndmanager.cpp
    ndmanagerdoc.cpp
    neuroscopevideoinfo.cpp
    neuroscopevideopage.cpp
    parameterpage.cpp
    parameterview.cpp
    programspage.cpp
    programpage.cpp
    queryinputdialog.cpp
    queryoutputdialog.cpp
    spikepage.cpp
    tags.cpp
    unitlistpage.cpp
    unittable.cpp
    videopage.cpp
    xmlreader.cpp
    ndmanageryamlreader.cpp
    xmlwriter.cpp
    safetablewidget.cpp
    ndmanagerutils.cpp
    colortable.cpp
    parametertree.cpp
    # AUTOUIC processes .ui files automatically when listed as sources
    acquisitionsystemlayout.cpp
    anatomylayout.cpp
    channelcolorslayout.cpp
    channeloffsetslayout.cpp
    clusterslayout.cpp
    filelayout.cpp
    generalinfolayout.cpp
    launcherlayout.cpp
    lfplayout.cpp
    neuroscopemisclayout.cpp
    neuroscopevideolayout.cpp
    parameterlayout.cpp
    queryinputdialogbase.cpp
    spikelayout.cpp
    unitlistlayout.cpp
    videolayout.cpp
    ndmanager-icons.qrc
    ndmanager-html.qrc)

add_executable(ndmanager $<$<PLATFORM_ID:Windows>:WIN32> ${_srcs})

target_include_directories(ndmanager PRIVATE
    # KlustersShared headers first so moved types (GeneralInformation etc)
    # are found in libklustersshared rather than stale local copies.
    "$<TARGET_PROPERTY:KlustersShared::klustersshared,INTERFACE_INCLUDE_DIRECTORIES>"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/..")   # for config-ndmanager.h

target_link_libraries(ndmanager PRIVATE
    KlustersShared::klustersshared
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Xml)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS ndmanager
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(FILES ndManagerDefault.xml
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications/ndmanager")

# Application and mimetype icons
foreach(_size IN ITEMS 16 22 32 48 64)
    install(FILES "hi${_size}-app-ndmanager.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps"
        RENAME ndmanager.png)
    install(FILES "hi${_size}-nphys-xml.png"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/mimetypes"
        RENAME application-nphys-xml.png)
endforeach()

install(FILES ndmanager.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES ndmanager.xml
    DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages")

file(GLOB _templates "${CMAKE_CURRENT_SOURCE_DIR}/Template*.xml")
install(FILES ${_templates}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/ndmanager/templates")
