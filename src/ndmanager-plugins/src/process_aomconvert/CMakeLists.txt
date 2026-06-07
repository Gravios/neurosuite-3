# process_aomconvert — AlphaOmega HDF5 v7.3 .mat → .dat + .yaml
#
# Requires: libhdf5 (C API only)
#   Ubuntu/Debian: apt install libhdf5-dev
#   macOS/Homebrew: brew install hdf5
#   Windows/vcpkg: vcpkg install hdf5

find_package(HDF5 QUIET COMPONENTS C)
if(NOT HDF5_FOUND)
    message(STATUS "process_aomconvert: HDF5 (C API) not found — skipping this plugin. "
                   "Install libhdf5-dev (apt) / hdf5 (brew) / hdf5 (vcpkg) to enable "
                   "AlphaOmega .mat → .dat/.yaml conversion.")
    return()
endif()

add_executable(process_aomconvert process_aomconvert.cpp)

target_compile_features(process_aomconvert PRIVATE cxx_std_20)

target_compile_options(process_aomconvert PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -O3 -ffast-math>)

target_include_directories(process_aomconvert PRIVATE
    ${HDF5_INCLUDE_DIRS})

target_link_libraries(process_aomconvert PRIVATE
    ${HDF5_C_LIBRARIES})

# Embed the HDF5 library directory as an RPATH so the installed binary can
# find libhdf5 at runtime without requiring ldconfig or LD_LIBRARY_PATH.
# This is safe for a workstation build: the soname is stable across minor
# HDF5 versions and the library won't move without a full reinstall.
set_target_properties(process_aomconvert PROPERTIES
    INSTALL_RPATH_USE_LINK_PATH TRUE
    BUILD_RPATH_USE_ORIGIN      TRUE)

# HDF5 definitions (e.g. _LARGEFILE_SOURCE, H5_USE_16_API) injected by FindHDF5
target_compile_definitions(process_aomconvert PRIVATE
    ${HDF5_DEFINITIONS})

install(TARGETS process_aomconvert
    DESTINATION "${CMAKE_INSTALL_BINDIR}")
