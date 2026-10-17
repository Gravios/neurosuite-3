# =============================================================================
# neuroscope spectral rendering: SpectralImage -> QImage (colormap + dB
# scaling).  The Qt-free DSP it renders is Neurosuite::spectral
# (src/libneurosuite-core/src/neurosuite/spectral), tested there as
# core_test_spectral.  Separated from the widget so the rendering maths is
# unit-tested directly.
# =============================================================================
find_package(Qt6 QUIET COMPONENTS Gui)
if(Qt6_FOUND)
    add_library(neuroscope-spectral-render STATIC spectralrender.cpp)
    target_link_libraries(neuroscope-spectral-render
        PUBLIC Neurosuite::spectral Qt6::Gui)
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
