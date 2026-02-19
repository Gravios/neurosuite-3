project(klusters-src)

set(klusters_SRCS
    autosavethread.cpp
    baseframe.cpp
    channellist.cpp
    clusterPalette.cpp
    clusterinformationdialog.cpp
    clustersprovider.cpp
    clusterview.cpp
    configuration.cpp
    correlationthread.cpp
    correlationview.cpp
    data.cpp
    dataprovider.cpp
    errormatrixthread.cpp
    errormatrixview.cpp
    eventsprovider.cpp
    groupingassistant.cpp
    klusters.cpp
    klustersdoc.cpp
    klustersview.cpp
    klustersxmlreader.cpp
    main.cpp
    minmaxthread.cpp
    pair.cpp
    parameterxmlmodifier.cpp
    prefclusterview.cpp
    prefdialog.cpp
    prefgeneral.cpp
    prefwaveformview.cpp
    processlinemaker.cpp
    processwidget.cpp
    spinbox.cpp
    savethread.cpp
    sortabletable.cpp
    tags.cpp
    tracesprovider.cpp
    traceview.cpp
    tracewidget.cpp
    viewwidget.cpp
    waveformthread.cpp
    waveformview.cpp
    prefgenerallayout.cpp
    prefwaveformviewlayout.cpp
    prefclusterviewlayout.cpp
)

qt_add_resources(klusters_SRCS klusters-cursors.qrc klusters-icons.qrc)

set(klusters_UI prefwaveformviewlayout.ui prefgenerallayout.ui prefclusterviewlayout.ui)
qt_wrap_ui(klusters_SRCS ${klusters_UI})

add_executable(klusters WIN32 ${klusters_SRCS})

target_link_libraries(klusters
    PRIVATE
    ${LIBKLUSTERSSHARED_LIBRARY}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Xml
        Qt6::PrintSupport
)

target_include_directories(klusters PRIVATE ${LIBKLUSTERSSHARED_INCLUDE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/../)

install(TARGETS klusters DESTINATION bin)
install(FILES klusters.png DESTINATION share/icons/)
install(FILES hi16-app-klusters.png DESTINATION share/icons/hicolor/16x16/apps/ RENAME klusters.png)
install(FILES hi22-app-klusters.png DESTINATION share/icons/hicolor/22x22/apps/ RENAME klusters.png)
install(FILES hi32-app-klusters.png DESTINATION share/icons/hicolor/32x32/apps/ RENAME klusters.png)
install(FILES hi48-app-klusters.png DESTINATION share/icons/hicolor/48x48/apps/ RENAME klusters.png)
install(FILES hi64-app-klusters.png DESTINATION share/icons/hicolor/64x64/apps/ RENAME klusters.png)
install(FILES klusters.desktop DESTINATION share/applications/)
