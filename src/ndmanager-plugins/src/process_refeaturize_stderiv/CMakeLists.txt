cmake_minimum_required(VERSION 3.16)

add_executable(process_refeaturize_stderiv
    process_refeaturize_stderiv.cpp
)
target_compile_features(process_refeaturize_stderiv PRIVATE cxx_std_17)

install(TARGETS process_refeaturize_stderiv
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
add_manpage(process_refeaturize_stderiv)
