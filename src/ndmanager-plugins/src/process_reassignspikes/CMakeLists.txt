cmake_minimum_required(VERSION 3.16)

# Compile the canonical Qt-free progressbar.cpp (BlockProgress defrag-style
# stage display) as shared source -- not linking the Qt libklustersshared,
# matching process_pca / process_extractspikes.
set(_klustersshared_dir
    "${CMAKE_SOURCE_DIR}/src/libklustersshared/src/klustersshared")

add_executable(process_reassignspikes
    process_reassignspikes.cpp
    "${_klustersshared_dir}/progressbar.cpp"
)
target_include_directories(process_reassignspikes
    PRIVATE "${_klustersshared_dir}")
target_compile_features(process_reassignspikes PRIVATE cxx_std_17)

# YAML parameter file (geometry, sampling rate), as in process_drifttracker.
find_package(yaml-cpp REQUIRED)
target_link_libraries(process_reassignspikes PRIVATE yaml-cpp)

install(TARGETS process_reassignspikes
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

add_manpage(process_reassignspikes)
