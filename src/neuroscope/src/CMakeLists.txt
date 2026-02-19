configure_file(${CMAKE_SOURCE_DIR}/config-neuroscope.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-neuroscope.h)
project(neuroscope-src)

set(neuroscope_SRCS
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
    neuroscopexmlreader.cpp
    positionproperties.cpp
    positionsprovider.cpp
    positionview.cpp
    prefdefaults.cpp
    prefdialog.cpp
    prefgeneral.cpp
    properties.cpp
    propertiesdialog.cpp
    tags.cpp
    tracesprovider.cpp
    traceview.cpp
    tracewidget.cpp
    sessionxmlwriter.cpp
    parameterxmlcreator.cpp
    parameterxmlmodifier.cpp
    imagecreator.cpp
    globaleventsprovider.cpp
    eventdata.cpp
    clusterpropertieslayout.cpp
    positionpropertieslayout.cpp
    prefdefaultslayout.cpp
    prefgenerallayout.cpp
    propertieslayout.cpp
)

set(neuroscope_UI
    propertieslayout.ui
    prefdefaultslayout.ui
    positionpropertieslayout.ui
    prefgenerallayout.ui
    clusterpropertieslayout.ui
)

qt_add_resources(neuroscope_SRCS neuroscope-icons.qrc)
qt_wrap_ui(neuroscope_SRCS ${neuroscope_UI})

add_executable(neuroscope WIN32 ${neuroscope_SRCS})

target_link_libraries(neuroscope
    PRIVATE
        ${LIBKLUSTERSSHARED_LIBRARY}
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Xml
        Qt6::PrintSupport
)

install(TARGETS neuroscope DESTINATION bin)
install(FILES hi16-app-neuroscope.png DESTINATION share/icons/hicolor/16x16/apps/ RENAME neuroscope.png)
install(FILES hi22-app-neuroscope.png DESTINATION share/icons/hicolor/22x22/apps/ RENAME neuroscope.png)
install(FILES hi32-app-neuroscope.png DESTINATION share/icons/hicolor/32x32/apps/ RENAME neuroscope.png)
install(FILES hi48-app-neuroscope.png DESTINATION share/icons/hicolor/48x48/apps/ RENAME neuroscope.png)
install(FILES hi64-app-neuroscope.png DESTINATION share/icons/hicolor/64x64/apps/ RENAME neuroscope.png)

install(FILES hi16-nphys-nrs.png DESTINATION share/icons/hicolor/16x16/mimetypes/ RENAME application-nphys-nrs.png)
install(FILES hi22-nphys-nrs.png DESTINATION share/icons/hicolor/22x22/mimetypes/ RENAME application-nphys-nrs.png)
install(FILES hi32-nphys-nrs.png DESTINATION share/icons/hicolor/32x32/mimetypes/ RENAME application-nphys-nrs.png)
install(FILES hi48-nphys-nrs.png DESTINATION share/icons/hicolor/48x48/mimetypes/ RENAME application-nphys-nrs.png)
install(FILES hi64-nphys-nrs.png DESTINATION share/icons/hicolor/64x64/mimetypes/ RENAME application-nphys-nrs.png)

install(FILES neuroscope.desktop DESTINATION share/applications/)
install(FILES neuroscope.xml DESTINATION share/mime/packages/)
