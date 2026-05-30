# Reference the canonical klustersshared progressbar source directly.  Not
# linking libklustersshared because process_extractspikes is a pure CLI tool
# with no Qt dependency, and libklustersshared is a Qt library — linking it
# would drag Qt into a command-line plugin.  progressbar.cpp (and its
# BlockProgress defrag-style stage display) is Qt-free, so we compile it in.
set(_klustersshared_dir
    "${CMAKE_SOURCE_DIR}/src/libklustersshared/src/klustersshared")

add_executable(process_extractspikes
    process_extractspikes.cpp
    "${_klustersshared_dir}/progressbar.cpp")
target_include_directories(process_extractspikes
    PRIVATE
        "${_klustersshared_dir}")
target_compile_features(process_extractspikes PRIVATE cxx_std_17)
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(process_extractspikes PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "process_extractspikes: OpenMP enabled")
else()
    message(STATUS "process_extractspikes: OpenMP not found, single-threaded")
endif()
install(TARGETS process_extractspikes DESTINATION "${CMAKE_INSTALL_BINDIR}")
add_manpage(process_extractspikes)
