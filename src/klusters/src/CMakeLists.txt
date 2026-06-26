project(klusters-src)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Shared xcorr kernel sources (live in src/libklustersshared/src/xcorr/ —
# no Qt dependency).  Relocated from src/shared/xcorr/ in patch66 to
# consolidate with libklustersshared and eliminate the duplicate that
# previously lived in src/klusters/src/realign_xcorr_*.
# ---------------------------------------------------------------------------
set(_xcorr_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../libklustersshared/src/xcorr")

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
    curationlogger.cpp
    data.cpp
    dataprovider.cpp
    dipsplit.cpp
    errormatrixthread.cpp
    errormatrixview.cpp
    templatematrixthread.cpp
    templatematrixview.cpp
    residualmatrixthread.cpp
    residualmatrixview.cpp
    pairxcorrthread.cpp
    eventsprovider.cpp
    groupingassistant.cpp
    groupingassistant_gpu_dispatch.cpp
    reorder_similarity_gpu_dispatch.cpp
    pca_refine_gpu_dispatch.cpp
    ${_xcorr_dir}/realign_xcorr_omp.cpp
    ${_xcorr_dir}/realign_xcorr_dispatch.cpp
    ${_xcorr_dir}/realign_center.cpp
    realignworker.cpp
    serialjobqueue.h   # header-only QObject — listed so AUTOMOC generates its moc
    realignreviewdialog.cpp
    spikerealigndialog.cpp
    klusters.cpp
    klustersdoc.cpp
    klustersdoc_recluster.cpp   # KlustersDoc reclustering subsystem (split from klustersdoc.cpp)
    klustersdoc_undo.cpp        # KlustersDoc undo/redo subsystem (split from klustersdoc.cpp)
    klustersdoc_hierarchy.cpp   # KlustersDoc hierarchical (.clc child) clustering (split from klustersdoc.cpp)
    klustersdoc_edit.cpp        # KlustersDoc cluster-editing operations (split from klustersdoc.cpp)
    klustersdoc_renumber.cpp    # KlustersDoc cluster renumber/reorder (split from klustersdoc.cpp)
    klustersview.cpp
    klustersyamlreader.cpp
    main.cpp
    minmaxthread.cpp
    pair.cpp
    prefclusterview.cpp
    autoMerge.cpp
    prefdialog.cpp
    prefdisplay.cpp
    prefsession.cpp
    prefreclustering.cpp
    prefrefinement.cpp
    prefautomerge.cpp
    prefwaveformview.cpp
    processlinemaker.cpp
    processwidget.cpp
    pluginregistry.cpp
    plugindialog.cpp
    spinbox.cpp
    savethread.cpp
    sortabletable.cpp
    tags.cpp
    timer.cpp        # ODR-safe definition of tv0 (shared by all timer.h includers)
    tracesprovider.cpp
    traceview.cpp
    tracewidget.cpp
    viewwidget.cpp
    waveformthread.cpp
    waveformview.cpp
    watershed2d.cpp
    prefdisplaylayout.cpp
    prefsessionlayout.cpp
    prefreclusteringlayout.cpp
    prefrefinementlayout.cpp
    prefautomergelayout.cpp
    prefwaveformviewlayout.cpp
    prefclusterviewlayout.cpp
)

# GPU backend source files — each compiled by its own toolchain.
if(USE_CUDA)
    list(APPEND klusters_SRCS groupingassistant_cuda.cu)
    list(APPEND klusters_SRCS reorder_similarity_cuda.cu)
    list(APPEND klusters_SRCS pca_refine_cuda.cu)
    list(APPEND klusters_SRCS ${_xcorr_dir}/realign_xcorr_cuda.cu)
endif()
if(USE_HIP)
    list(APPEND klusters_SRCS groupingassistant_hip.hip)
    list(APPEND klusters_SRCS ${_xcorr_dir}/realign_xcorr_hip.hip)
endif()
if(USE_SYCL)
    list(APPEND klusters_SRCS groupingassistant_sycl.cpp)
    list(APPEND klusters_SRCS ${_xcorr_dir}/realign_xcorr_sycl.cpp)
endif()

qt_add_resources(klusters_SRCS klusters-cursors.qrc klusters-icons.qrc)

set(klusters_UI prefwaveformviewlayout.ui prefdisplaylayout.ui prefsessionlayout.ui prefreclusteringlayout.ui prefrefinementlayout.ui prefautomergelayout.ui prefclusterviewlayout.ui)
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
    set_source_files_properties(${_xcorr_dir}/realign_xcorr_hip.hip
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
    # spir64 = JIT at runtime, works on any Intel GPU/CPU without needing
    # ocloc or a specific device name.  For AOT add:
    #   -fsycl-targets=spir64_gen -Xs "-device xe_hpg"  (or your GPU codename)
    set(SYCL_FLAGS "-fsycl" "-fsycl-targets=spir64")
    target_compile_options(klusters PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:${SYCL_FLAGS}>)
    target_link_options(klusters PRIVATE ${SYCL_FLAGS})
    # Link SYCL runtime if the CMake package exports an imported target.
    # Intel renamed the package from IntelDPCPP to IntelSYCL; try both.
    # If neither exports the target, the -fsycl linker flag (set above via
    # target_link_options) is sufficient — the icpx driver pulls in the runtime.
    if(TARGET IntelSYCL::SYCL_CXX)
        target_link_libraries(klusters PRIVATE IntelSYCL::SYCL_CXX)
    elseif(TARGET IntelDPCPP::SYCL_CXX)
        target_link_libraries(klusters PRIVATE IntelDPCPP::SYCL_CXX)
    else()
        message(STATUS "SYCL: no imported CMake target found — relying on -fsycl linker flag")
    endif()
endif()

# ---------------------------------------------------------------------------
# Common Qt + shared library links
# ---------------------------------------------------------------------------
target_link_libraries(klusters
    PRIVATE
    KlustersShared::klustersshared
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport
)

target_include_directories(klusters PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/../"   # for config-klusters.h and AUTOUIC output
    "${_xcorr_dir}"                     # for realign_xcorr.h from shared location
    "${CMAKE_SOURCE_DIR}/src/libneurosuite-core/src"  # header-only neurosuite::core (pca_projection.hpp)
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

# ---------------------------------------------------------------------------
# Standalone binary converter: text res/fet/clu → binary
# ---------------------------------------------------------------------------
add_executable(klusters_convert_binary convert_to_binary.cpp)
install(TARGETS klusters_convert_binary DESTINATION bin)
