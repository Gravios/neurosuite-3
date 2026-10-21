# =============================================================================
# neurosuite/spectral — multitaper spectral estimation (Neurosuite::spectral)
#
# Self-contained (no Qt) multitaper spectral-estimation library: DPSS/Slepian
# tapers, a radix-2 FFT, the multitaper PSD / spectrogram estimator, and
# cross-channel ("common") ZCA whitening.  NeuroScope's spectral view uses it;
# its Qt rendering layer stays in src/neuroscope/src/spectral.  It lives in
# libneurosuite-core so it is built and tested without Qt
# (core_test_spectral).  A static library of its own, not part of the
# neurosuite-core shared library, because it brings optional FFTW, CUDA,
# Accelerate (macOS) and OpenMP with it.  The code keeps its namespace, neuroscope::spectral.
#
# OpenMP parallelises the per-window spectrogram loop when available; the
# source guards every use behind _OPENMP, so a build without OpenMP is
# correct, just serial.
# =============================================================================
add_library(neurosuite-spectral STATIC
    dpss.cpp
    multitaper.cpp
    spectralfft.cpp
    decimate.cpp
    chunkcache.cpp
    spectralengine.cpp)

add_library(Neurosuite::spectral ALIAS neurosuite-spectral)

target_compile_features(neurosuite-spectral PUBLIC cxx_std_20)
set_target_properties(neurosuite-spectral PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Consumers write #include <neurosuite/spectral/spectralengine.h>.
target_include_directories(neurosuite-spectral
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>")

# The chunk cache runs its fill on a std::thread.
find_package(Threads REQUIRED)
target_link_libraries(neurosuite-spectral PUBLIC Threads::Threads)

# GPU backend source: the cuFFT translation unit when CUDA is enabled and the
# toolkit is present, otherwise a CPU stub that always declines so the engine
# uses its CPU path. CUDA is gated on the project having found nvcc (the root
# enables the CUDA language in project() only then).
set(_ns_gpu_cuda FALSE)
if(USE_CUDA AND CMAKE_CUDA_COMPILER)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        set(_ns_gpu_cuda TRUE)
    endif()
endif()
if(_ns_gpu_cuda)
    target_sources(neurosuite-spectral PRIVATE spectralgpu.cu)
    set_source_files_properties(spectralgpu.cu PROPERTIES LANGUAGE CUDA)
    target_link_libraries(neurosuite-spectral PUBLIC CUDA::cufft CUDA::cudart)
    target_compile_definitions(neurosuite-spectral PUBLIC HAVE_CUDA)
    message(STATUS "neurosuite-spectral: CUDA (cuFFT) GPU backend enabled")
else()
    target_sources(neurosuite-spectral PRIVATE spectralgpu_cpu.cpp)
    message(STATUS "neurosuite-spectral: GPU backend = CPU fallback (no CUDA)")
endif()

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
        target_link_libraries(neurosuite-spectral PUBLIC PkgConfig::FFTW)
        set(_ns_have_fftw TRUE)
    endif()
endif()
if(NOT _ns_have_fftw)
    find_package(FFTW3 QUIET CONFIG)
    if(FFTW3_FOUND)
        target_link_libraries(neurosuite-spectral PUBLIC FFTW3::fftw3)
        set(_ns_have_fftw TRUE)
    endif()
endif()
if(_ns_have_fftw)
    target_compile_definitions(neurosuite-spectral PUBLIC HAVE_FFTW)
    message(STATUS "neurosuite-spectral: FFTW backend enabled")
else()
    message(STATUS "neurosuite-spectral: FFTW not found — using in-tree radix-2 FFT")
endif()

# Apple Accelerate (macOS): vDSP's real DFT, a second SIMD provider beside
# FFTW. Without FFTW (the Homebrew build does not install it) it replaces the
# radix-2 fallback; with FFTW, FFTW stays the default and NS_SPECTRAL_FFT=vdsp
# selects vDSP at run time. Accelerate is part of macOS: no extra dependency.
if(APPLE)
    if(NOT DEFINED NS_USE_ACCELERATE)
        option(NS_USE_ACCELERATE "Use Apple's Accelerate framework (vDSP, BLAS) on macOS" ON)
    endif()
    if(NS_USE_ACCELERATE)
        find_library(NS_ACCELERATE_FRAMEWORK Accelerate)
        if(NS_ACCELERATE_FRAMEWORK)
            target_link_libraries(neurosuite-spectral PUBLIC "${NS_ACCELERATE_FRAMEWORK}")
            target_compile_definitions(neurosuite-spectral PUBLIC HAVE_VDSP)
            message(STATUS "neurosuite-spectral: Accelerate vDSP backend enabled")
        else()
            message(STATUS "neurosuite-spectral: Accelerate framework not found — no vDSP backend")
        endif()
    endif()
endif()

find_package(OpenMP QUIET COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
    target_link_libraries(neurosuite-spectral PUBLIC OpenMP::OpenMP_CXX)
    message(STATUS "neurosuite-spectral: OpenMP enabled")
else()
    message(STATUS "neurosuite-spectral: OpenMP not found — spectrogram runs serially")
endif()
