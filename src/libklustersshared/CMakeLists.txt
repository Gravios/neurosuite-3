cmake_minimum_required(VERSION 3.22...3.31)
project(libklustersshared VERSION 2.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ---------------------------------------------------------------------------
# Qt automoc/uic/rcc
# ---------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
find_package(yaml-cpp REQUIRED)
# Inside the monorepo libneurosuite-core is configured first and its target
# exists; built on its own, this library finds the installed package.
if(NOT TARGET Neurosuite::core)
    find_package(LibNeurosuiteCore REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Install layout
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ---------------------------------------------------------------------------
# Linker hardening (GCC/Clang on non-Apple Linux)
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    add_link_options(
        -Wl,--fatal-warnings
        -Wl,--no-undefined)
endif()

# ---------------------------------------------------------------------------
# Symbol visibility — keeps the public API surface explicit
# ---------------------------------------------------------------------------
set(CMAKE_CXX_VISIBILITY_PRESET     hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# ---------------------------------------------------------------------------
# Package config files
# ---------------------------------------------------------------------------
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/LibKlustersSharedConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LibKlustersShared")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfigVersion.cmake"
    VERSION       "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

# ---------------------------------------------------------------------------
# Build the library
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Regression tests — built only with NS_BUILD_TESTS, run with ctest.
# (Only the self-contained tests are registered here; the legacy
# test_scrollview demo in test/ is left untouched and unbuilt.)
# ---------------------------------------------------------------------------
if(NS_BUILD_TESTS)
    add_executable(klustersshared_theme_test test/theme_test.cpp)
    target_link_libraries(klustersshared_theme_test PRIVATE Neurosuite::qt Qt6::Widgets)
    add_test(NAME klustersshared_theme_test COMMAND klustersshared_theme_test)
    set_tests_properties(klustersshared_theme_test PROPERTIES
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/themecfg")

    # ParameterYamlModifier round trip: NeuroScope's saves must not drop keys
    # other tools write (sdiffPairs, probeId, channelMapping, ...).
    add_executable(klustersshared_yamlmodifier_test test/yamlmodifier_test.cpp)
    target_link_libraries(klustersshared_yamlmodifier_test PRIVATE Neurosuite::qt yaml-cpp)
    add_test(NAME klustersshared_yamlmodifier_test COMMAND klustersshared_yamlmodifier_test)

    # ParameterYamlWriter over a base document: ndmanager's saves must carry the
    # keys and blocks it does not model (probeId, sourceRecording, ...).
    add_executable(klustersshared_yamlwriter_test test/yamlwriter_test.cpp)
    target_link_libraries(klustersshared_yamlwriter_test PRIVATE Neurosuite::qt yaml-cpp)
    add_test(NAME klustersshared_yamlwriter_test COMMAND klustersshared_yamlwriter_test)

    # Saving keeps the file's layout (yamlformat.cpp, Qt-free): comments,
    # inline lists, quoting; round trips over the repository's templates.
    add_executable(klustersshared_yamlformat_test
        test/yamlformat_test.cpp src/klustersshared/yamlformat.cpp)
    target_include_directories(klustersshared_yamlformat_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/klustersshared")
    target_compile_definitions(klustersshared_yamlformat_test PRIVATE KLUSTERSSHARED_STATICLIB)
    target_link_libraries(klustersshared_yamlformat_test PRIVATE Qt6::Core yaml-cpp)  # Qt: export-macro header only
    add_test(NAME klustersshared_yamlformat_test
        COMMAND klustersshared_yamlformat_test "${CMAKE_CURRENT_SOURCE_DIR}/../..")

    # ItemColors / ChannelColors: copies (construction and assignment) are deep.
    add_executable(klustersshared_colors_test test/colors_test.cpp)
    target_include_directories(klustersshared_colors_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/klustersshared")
    target_link_libraries(klustersshared_colors_test PRIVATE Neurosuite::qt Qt6::Gui)
    add_test(NAME klustersshared_colors_test COMMAND klustersshared_colors_test)

    # sdiffPairs grammar (sdiff_pairs.h, header-only, no Qt): trees, sets,
    # strict positions; malformed patterns are rejected, not reinterpreted.
    add_executable(klustersshared_sdiff_pairs_test test/sdiff_pairs_test.cpp)
    target_include_directories(klustersshared_sdiff_pairs_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/klustersshared")
    add_test(NAME klustersshared_sdiff_pairs_test COMMAND klustersshared_sdiff_pairs_test)

    # The per-clustering cluster-information file (unitsidecar.h, header-only,
    # no Qt) Klusters writes and ndManager records into the units table.
    add_executable(klustersshared_unitsidecar_test test/unitsidecar_test.cpp)
    target_include_directories(klustersshared_unitsidecar_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/klustersshared")
    add_test(NAME klustersshared_unitsidecar_test COMMAND klustersshared_unitsidecar_test)

    # XcorrDispatch under concurrent calls (no Qt): one probe, atomic demotion,
    # wide shifts on OpenMP.  Built with USE_CUDA against a fake CUDA back end
    # defined in the test, so it needs no GPU.
    find_package(Threads REQUIRED)
    add_executable(klustersshared_xcorr_dispatch_test
        test/xcorr_dispatch_test.cpp
        src/xcorr/realign_xcorr_dispatch.cpp
        src/xcorr/realign_xcorr_omp.cpp)
    target_compile_definitions(klustersshared_xcorr_dispatch_test PRIVATE USE_CUDA)
    target_include_directories(klustersshared_xcorr_dispatch_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/xcorr")
    target_link_libraries(klustersshared_xcorr_dispatch_test PRIVATE Threads::Threads)
    add_test(NAME klustersshared_xcorr_dispatch_test COMMAND klustersshared_xcorr_dispatch_test)

    # Realignment kernel edge cases (no Qt): undefined (zero-energy) xcorr,
    # tie-breaking, zero-energy spikes in the per-spike centroid aligner.
    add_executable(klustersshared_realign_kernels_test
        test/realign_kernels_test.cpp
        src/xcorr/realign_xcorr_omp.cpp
        src/xcorr/realign_center.cpp)
    target_include_directories(klustersshared_realign_kernels_test
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/xcorr")
    add_test(NAME klustersshared_realign_kernels_test COMMAND klustersshared_realign_kernels_test)
endif()

# ---------------------------------------------------------------------------
# Install cmake package files
# ---------------------------------------------------------------------------
install(EXPORT LibKlustersSharedTargets
    NAMESPACE   KlustersShared::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LibKlustersShared"
    FILE        LibKlustersSharedTargets.cmake)

# Build-tree export is driven from the top-level monorepo CMakeLists.txt
# (after add_subdirectory) where the export set is guaranteed to be fully
# populated.  See the ns_add_subdir(libklustersshared …) call there.

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LibKlustersSharedConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/LibKlustersShared"
    COMPONENT   Devel)

include(CPack)
