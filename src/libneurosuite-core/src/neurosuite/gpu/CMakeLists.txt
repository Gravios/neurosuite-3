# =============================================================================
# neurosuite/gpu — the Apple GPU (Metal) device context (Neurosuite::metal)
#
# macOS with USE_METAL only (cmake/MetalBackend.cmake; design in
# doc/design/metal-backend.md).  A static library of its own, beside the
# neurosuite-core shared library, because it brings metal-cpp and the Metal
# framework with it.  It also holds metal-cpp's one implementation unit, so
# every program with a Metal backend links it, and only it defines that.
# Users get NS_HAVE_METAL and write #include <neurosuite/gpu/metalcontext.h>.
# =============================================================================
get_filename_component(_ns_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
include("${_ns_repo_root}/cmake/MetalBackend.cmake")
ns_metal_setup()
if(NOT NS_METAL_FOUND)
    return()
endif()

add_library(neurosuite-metal STATIC metalcontext.cpp)
add_library(Neurosuite::metal ALIAS neurosuite-metal)
target_compile_features(neurosuite-metal PUBLIC cxx_std_20)
target_include_directories(neurosuite-metal
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>")
target_link_libraries(neurosuite-metal PUBLIC ns_metalcpp)
target_compile_definitions(neurosuite-metal PUBLIC NS_HAVE_METAL)
