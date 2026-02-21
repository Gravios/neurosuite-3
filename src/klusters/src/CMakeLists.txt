project(klusters-src)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(klusters_SRCS
    autosavethread.cpp
    baseframe.cpp
    channellist.cpp
    clusterPalette.cpp
    clusterinformationdialog.cpp
    clustersprovider.cpp
    clusterview.cpp
    configuration.cpp
    correlationthread.cpp
    correlationview.cpp
    data.cpp
    dataprovider.cpp
    errormatrixthread.cpp
    errormatrixview.cpp
    eventsprovider.cpp
    groupingassistant.cpp
    groupingassistant_gpu_dispatch.cpp
    spikerealigndialog.cpp
    klusters.cpp
    klustersdoc.cpp
    klustersview.cpp
    klustersxmlreader.cpp
    main.cpp
    minmaxthread.cpp
    pair.cpp
    parameterxmlmodifier.cpp
    prefclusterview.cpp
    prefdialog.cpp
    prefgeneral.cpp
    prefwaveformview.cpp
    processlinemaker.cpp
    processwidget.cpp
    spinbox.cpp
    savethread.cpp
    sortabletable.cpp
    tags.cpp
    tracesprovider.cpp
    traceview.cpp
    tracewidget.cpp
    viewwidget.cpp
    waveformthread.cpp
    waveformview.cpp
    prefgenerallayout.cpp
    prefwaveformviewlayout.cpp
    prefclusterviewlayout.cpp
)

# GPU backend source files — each compiled by its own toolchain.
if(USE_CUDA)
    list(APPEND klusters_SRCS groupingassistant_cuda.cu)
endif()
if(USE_HIP)
    list(APPEND klusters_SRCS groupingassistant_hip.hip)
endif()
if(USE_SYCL)
    list(APPEND klusters_SRCS groupingassistant_sycl.cpp)
endif()

qt_add_resources(klusters_SRCS klusters-cursors.qrc klusters-icons.qrc)

set(klusters_UI prefwaveformviewlayout.ui prefgenerallayout.ui prefclusterviewlayout.ui)
qt_wrap_ui(klusters_SRCS ${klusters_UI})

add_executable(klusters WIN32 ${klusters_SRCS})

# ---------------------------------------------------------------------------
# OpenMP
# ---------------------------------------------------------------------------
if(OpenMP_CXX_FOUND)
    target_link_libraries(klusters PRIVATE OpenMP::OpenMP_CXX)
    message(STATUS "OpenMP: enabled (${OpenMP_CXX_VERSION})")
else()
    message(STATUS "OpenMP: not found — single-threaded CPU fallback")
endif()

# ---------------------------------------------------------------------------
# CUDA backend
# ---------------------------------------------------------------------------
if(USE_CUDA)
    target_compile_definitions(klusters PRIVATE USE_CUDA)
    set_target_properties(klusters PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        # Architectures:
        #   75  = Turing   (RTX 2000)
        #   86  = Ampere   (RTX 3000)
        #   89  = Ada      (RTX 4000)
        #   120 = Blackwell (RTX 5000, e.g. RTX 5070 Ti)
        CUDA_ARCHITECTURES "75;86;89;120"
    )
    target_link_libraries(klusters PRIVATE CUDA::cudart)
endif()

# ---------------------------------------------------------------------------
# HIP backend (AMD ROCm)
# ---------------------------------------------------------------------------
if(USE_HIP)
    target_compile_definitions(klusters PRIVATE USE_HIP)
    set_target_properties(klusters PROPERTIES
        # GFX targets:
        #   gfx1100 = RDNA 3 (RX 7900)
        #   gfx1030 = RDNA 2 (RX 6000)
        #   gfx90a  = CDNA 2 (MI210/250)
        #   gfx908  = CDNA 1 (MI100)
        HIP_ARCHITECTURES "gfx1100;gfx1030;gfx90a;gfx908"
    )
    # hipcc needs to know the target when compiling .hip files.
    set_source_files_properties(groupingassistant_hip.hip
        PROPERTIES LANGUAGE HIP)
    target_link_libraries(klusters PRIVATE hip::device)
endif()

# ---------------------------------------------------------------------------
# SYCL backend (Intel oneAPI / Arc)
# ---------------------------------------------------------------------------
if(USE_SYCL)
    target_compile_definitions(klusters PRIVATE USE_SYCL)
    # -fsycl compiles SYCL kernels; -fsycl-targets selects the backend.
    # intel_gpu_tgllp  = Tiger Lake / Xe (Arc A-series starts here)
    # intel_gpu_acm-g10 = Arc Alchemist
    # spir64           = generic SPIR-V (fallback, covers all Intel GPUs)
    set(SYCL_FLAGS "-fsycl" "-fsycl-targets=spir64,spir64_gen")
    target_compile_options(klusters PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${SYCL_FLAGS}>)
    target_link_options(klusters PRIVATE ${SYCL_FLAGS})
    # If IntelDPCPP package found, use its imported targets for runtime libs.
    if(IntelDPCPP_FOUND)
        target_link_libraries(klusters PRIVATE IntelDPCPP::SYCL_CXX)
    endif()
endif()

# ---------------------------------------------------------------------------
# Common Qt + shared library links
# ---------------------------------------------------------------------------
target_link_libraries(klusters
    PRIVATE
    ${LIBKLUSTERSSHARED_LIBRARY}
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Xml
    Qt6::PrintSupport
)

target_include_directories(klusters PRIVATE
    ${LIBKLUSTERSSHARED_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/../
)

# ---------------------------------------------------------------------------
# Summarise what was enabled
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== klusters GPU acceleration summary ===")
message(STATUS "  CUDA  : ${USE_CUDA}")
message(STATUS "  HIP   : ${USE_HIP}")
message(STATUS "  SYCL  : ${USE_SYCL}")
message(STATUS "  OpenMP: ${OpenMP_CXX_FOUND}")
message(STATUS "=========================================")
message(STATUS "")

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS klusters DESTINATION bin)
install(FILES klusters.png DESTINATION share/icons/)
install(FILES hi16-app-klusters.png DESTINATION share/icons/hicolor/16x16/apps/ RENAME klusters.png)
install(FILES hi22-app-klusters.png DESTINATION share/icons/hicolor/22x22/apps/ RENAME klusters.png)
install(FILES hi32-app-klusters.png DESTINATION share/icons/hicolor/32x32/apps/ RENAME klusters.png)
install(FILES hi48-app-klusters.png DESTINATION share/icons/hicolor/48x48/apps/ RENAME klusters.png)
install(FILES hi64-app-klusters.png DESTINATION share/icons/hicolor/64x64/apps/ RENAME klusters.png)
install(FILES klusters.desktop DESTINATION share/applications/)
