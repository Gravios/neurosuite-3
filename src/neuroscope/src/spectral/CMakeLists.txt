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
    multitaper.cpp)

target_compile_features(neuroscope-spectral PUBLIC cxx_std_20)
set_target_properties(neuroscope-spectral PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(neuroscope-spectral
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

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
