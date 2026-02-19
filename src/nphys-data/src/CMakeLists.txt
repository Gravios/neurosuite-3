project(nphys-data-src)

foreach(size 16x16 22x22 32x32 48x48 64x64)
    file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/${size}/*.png")
    install(FILES ${files} DESTINATION share/icons/hicolor/${size}/mimetypes/)
endforeach()

install(FILES nphys-data.xml DESTINATION share/mime/packages/)
