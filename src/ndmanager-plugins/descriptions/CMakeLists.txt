file(GLOB _xml "${CMAKE_CURRENT_SOURCE_DIR}/*.xml")
install(FILES ${_xml}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/ndmanager/descriptions")
