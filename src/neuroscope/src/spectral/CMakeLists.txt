# =============================================================================
# neuroscope spectral DSP core
#
# Self-contained (no Qt) multitaper spectral-estimation library: DPSS/Slepian
# tapers, a radix-2 FFT, the multitaper PSD / spectrogram estimator, and
# cross-channel ("common") ZCA whitening. Consumed by the neuroscope spectral
# view; kept Qt-free so it can be unit-tested in isolation.
#
# OpenMP parallelises the per-window spectrogram loop when available; the
# source guards every use behind _OPENMP, so a build without OpenMP is
# correct, just serial.
# =============================================================================
add_library(neuroscope-spectral STATIC
    dpss.cpp
    multitaper.cpp
    spectralfft.cpp
    spectralengine.cpp
    spectralgpu_cpu.cpp)

target_compile_features(neuroscope-spectral PUBLIC cxx_std_20)
set_target_properties(neuroscope-spectral PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(neuroscope-spectral
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# Optional FFTW backend (the MIT "Fastest Fourier Transform in the West").
# When present it provides SIMD real-to-complex transforms at arbitrary sizes;
# otherwise the in-tree radix-2 fallback is used (power-of-two sizes only).
# FFTW is GPL, compatible with neuroscope's GPL licence. Discovered via
# pkg-config (FFTW ships fftw3.pc) with the CMake package config as a fallback.
find_package(PkgConfig QUIET)
set(_ns_have_fftw FALSE)
if(PkgConfig_FOUND)
    pkg_check_modules(FFTW QUIET IMPORTED_TARGET fftw3)
    if(FFTW_FOUND)
        target_link_libraries(neuroscope-spectral PUBLIC PkgConfig::FFTW)
        set(_ns_have_fftw TRUE)
    endif()
endif()
if(NOT _ns_have_fftw)
    find_package(FFTW3 QUIET CONFIG)
    if(FFTW3_FOUND)
        target_link_libraries(neuroscope-spectral PUBLIC FFTW3::fftw3)
        set(_ns_have_fftw TRUE)
    endif()
endif()
if(_ns_have_fftw)
    target_compile_definitions(neuroscope-spectral PUBLIC HAVE_FFTW)
    message(STATUS "neuroscope-spectral: FFTW backend enabled")
else()
    message(STATUS "neuroscope-spectral: FFTW not found — using in-tree radix-2 FFT")
endif()

find_package(OpenMP QUIET COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
    target_link_libraries(neuroscope-spectral PUBLIC OpenMP::OpenMP_CXX)
    message(STATUS "neuroscope-spectral: OpenMP enabled")
else()
    message(STATUS "neuroscope-spectral: OpenMP not found — spectrogram runs serially")
endif()

# Numerical regression test (own main(), exits non-zero on failure).
if(NS_BUILD_TESTS)
    add_executable(neuroscope_spectral_selftest spectral_selftest.cpp)
    target_link_libraries(neuroscope_spectral_selftest PRIVATE neuroscope-spectral)
    add_test(NAME neuroscope_spectral_selftest
             COMMAND neuroscope_spectral_selftest)
endif()

# ---------------------------------------------------------------------------
# Qt rendering layer: SpectralImage -> QImage (colormap + dB scaling).
# Separated from the widget so the rendering maths is unit-tested directly.
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET COMPONENTS Gui)
if(Qt6_FOUND)
    add_library(neuroscope-spectral-render STATIC spectralrender.cpp)
    target_link_libraries(neuroscope-spectral-render
        PUBLIC neuroscope-spectral Qt6::Gui)
    target_include_directories(neuroscope-spectral-render
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

    if(NS_BUILD_TESTS)
        add_executable(neuroscope_spectral_render_test spectralrender_test.cpp)
        target_link_libraries(neuroscope_spectral_render_test
            PRIVATE neuroscope-spectral-render)
        add_test(NAME neuroscope_spectral_render_test
                 COMMAND neuroscope_spectral_render_test)
    endif()
endif()
