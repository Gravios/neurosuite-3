# process_aomconvert — AlphaOmega HDF5 v7.3 .mat → .dat + .yaml
#
# Requires: libhdf5 (C API only)
#   Ubuntu/Debian: apt install libhdf5-dev
#   macOS/Homebrew: brew install hdf5
#   Windows/vcpkg: vcpkg install hdf5

find_package(HDF5 REQUIRED COMPONENTS C)

add_executable(process_aomconvert process_aomconvert.cpp)

target_compile_features(process_aomconvert PRIVATE cxx_std_20)

target_compile_options(process_aomconvert PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -O3 -march=native -ffast-math>)

target_include_directories(process_aomconvert PRIVATE
    ${HDF5_INCLUDE_DIRS})

target_link_libraries(process_aomconvert PRIVATE
    ${HDF5_C_LIBRARIES})

# HDF5 definitions (e.g. _LARGEFILE_SOURCE, H5_USE_16_API) injected by FindHDF5
target_compile_definitions(process_aomconvert PRIVATE
    ${HDF5_DEFINITIONS})

install(TARGETS process_aomconvert
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
