foreach(_size IN ITEMS 16x16 22x22 32x32 48x48 64x64)
    file(GLOB _icons "${CMAKE_CURRENT_SOURCE_DIR}/${_size}/*.png")
    install(FILES ${_icons}
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}/mimetypes")
endforeach()

install(FILES nphys-data.xml
    DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages")
