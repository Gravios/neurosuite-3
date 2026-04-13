cmake_minimum_required(VERSION 3.16)

add_executable(process_pca_stderiv
    process_pca_stderiv.cpp
)
target_compile_features(process_pca_stderiv PRIVATE cxx_std_17)

install(TARGETS process_pca_stderiv
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

add_manpage(process_pca_stderiv)
