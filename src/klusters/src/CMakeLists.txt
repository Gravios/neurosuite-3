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
    views/baseframe.cpp
    views/bufferedview.cpp
    # input binding core moved to libklustersshared (src/libklustersshared/src/input)
    # in the neuroscope input-overhaul S0 — klusters now links it via
    # KlustersShared::klustersshared.  See claude/neuroscope-input-plan.md.
    views/channellist.cpp
    views/clusterpalette.cpp
    dialogs/clusterinformationdialog.cpp
    data/clustersprovider.cpp
    views/clusterview.cpp
    prefs/configuration.cpp
    prefs/configuration_prefs.cpp     # Configuration::prefTable(): every preference of the settings file, one row each
    prefs/prefbinder.cpp              # PrefBinder: page controls loaded / applied / reset from the preference table
    jobs/correlationthread.cpp
    views/correlationview.cpp
    document/curationlogger.cpp
    data/data.cpp
    data/spkreader.cpp               # shared positioned-read .spk access (worker-pool step 1)
    data/spkmemory.cpp               # every .spk of the open group held in memory (SpkMemoryCache)
    jobs/klustersjobpool.cpp         # shared worker pool for compute jobs (worker-pool step 2)
    data/dataprovider.cpp
    jobs/errormatrixthread.cpp
    views/errormatrixview.cpp
    jobs/templatematrixthread.cpp
    views/templatematrixview.cpp
    views/templateview.cpp
    views/templatepalette.cpp        # left-column template-class palette (§11.5)
    data/templateclassstore.cpp     # EAP template-class state model (.eap/.tcl over neurosuite-core)
    data/templatelineagestore.cpp   # manual template-lineage model (.wtl over neurosuite-core)
    document/filecommit.cpp             # all-or-nothing commit of the saved files; parity stamps
    document/hierarchyfiles.cpp         # .clu/.clc/.clp triple on disk: nesting scan, promotion, staged writes
    jobs/residualmatrixthread.cpp
    views/matrixviewbase.cpp          # MatrixViewBase: grid geometry, pan / zoom, follow and id labels of the four matrices
    views/residualmatrixview.cpp
    jobs/pairxcorrthread.cpp
    compute/driftgeometry.cpp
    jobs/driftmatrixthread.cpp
    jobs/driftshiftthread.cpp
    views/driftmatrixview.cpp
    views/matrixtemplatestrip.cpp    # shared marked-node template region for the 4 curation matrices (§11.5)
    views/rangeslider.cpp
    views/mergerecommendview.cpp
    jobs/mergerecommendthread.cpp
    data/eventsprovider.cpp
    compute/groupingassistant.cpp
    compute/groupingassistant_gpu_dispatch.cpp
    compute/pca_refine_gpu_dispatch.cpp
    compute/corecut_gpu_dispatch.cpp
    ${_xcorr_dir}/realign_xcorr_omp.cpp
    ${_xcorr_dir}/realign_xcorr_dispatch.cpp
    ${_xcorr_dir}/realign_center.cpp
    ${_xcorr_dir}/realign_pass.cpp
    jobs/realignworker.cpp
    jobs/serialjobqueue.h   # header-only QObject — listed so AUTOMOC generates its moc
    app/klusters.cpp
    app/klusters_recluster.cpp      # KlustersApp reclustering / process execution (split from klusters.cpp)
    app/klusters_state.cpp          # KlustersApp action-state machine slotStateChanged (split from klusters.cpp)
    app/klusters_realign.cpp        # KlustersApp spike-realignment UI / batch driver (split from klusters.cpp)
    app/klusters_hierarchy.cpp      # KlustersApp hierarchical (.clc child) view + child-palette slots (split from klusters.cpp)
    app/klusters_dipdrift.cpp       # KlustersApp dip-split / probe-drift / timestamp-nudge (split from klusters.cpp)
    app/klusters_featurespace.cpp   # KlustersApp feature-space switcher (Parameters toolbar combo)
    app/klusters_collisions.cpp     # KlustersApp Mark Collisions (scan the selection, confirm, mark)
    app/klusters_selftest.cpp       # headless self-test driver (NS3_KLUSTERS_SELFTEST=<spec.json>)
    app/klusters_radialmenu.cpp     # KlustersApp radial context menu (Right-click; proof of concept)
    app/radialmenu.cpp              # hierarchical radial / marking menu widget over a QMenu tree
    app/radiallayout.cpp            # the radial menu's layout as data (JSON in the settings)
    app/actionid.cpp                # stable ids for menu actions (registry id, else menu path)
    app/klusters_actionids.cpp      # KlustersApp::actionCatalog() / actionsById() / applyActionIcons()
    app/klusters_commands.cpp       # KlustersApp::commandTable() / addCommand(): the menu commands, described once
    app/actionicons.cpp             # the one table of action icons (keyed by action id)
    prefs/prefradialmenu.cpp          # Preferences ▸ Radial menu: the wheel editor
    app/klusters_timesort.cpp       # KlustersApp Sort Spikes by Time (confirm, rewrite, reopen)
    app/klusters_sort.cpp           # KlustersApp cluster sorting / reordering + sort progress bar (split from klusters.cpp)
    document/klustersdoc.cpp
    document/klustersdoc_recluster.cpp   # KlustersDoc reclustering subsystem (split from klustersdoc.cpp)
    document/klustersdoc_subsession.cpp  # KlustersDoc recluster sub-session (a fiber-kit plan on the selection)
    document/subsessionfiles.cpp         # its file operations (QtCore only; klusters_test_subsessionfiles)
    data/realignio.cpp               # realign_io: the rows of one cluster read from .spk / .res / .fet (no document)
    document/undohistory.cpp             # UndoHistory: the entries, the steps and the depth of the one undo history
    document/klustersdoc_undo.cpp        # KlustersDoc undo/redo subsystem (split from klustersdoc.cpp)
    document/klustersdoc_hierarchy.cpp   # KlustersDoc hierarchical (.clc child) clustering (split from klustersdoc.cpp)
    document/klustersdoc_edit.cpp        # KlustersDoc cluster-editing operations (split from klustersdoc.cpp)
    document/klustersdoc_renumber.cpp    # KlustersDoc cluster renumber/reorder (split from klustersdoc.cpp)
    document/klustersdoc_io.cpp          # KlustersDoc document lifecycle / file I/O (split from klustersdoc.cpp)
    document/klustersdoc_realign.cpp     # KlustersDoc spike realignment / nudge (split from klustersdoc.cpp)
    document/klustersdoc_featurespace.cpp # KlustersDoc feature-space switching (load another method's .fet/.spk)
    document/klustersdoc_timesort.cpp    # KlustersDoc full time sort (one row permutation over .res/.spk/.fet/.clu/.clc)
    document/klustersdoc_dipsplit.cpp    # KlustersDoc bimodality / KNN splitting (split from klustersdoc.cpp)
    document/klustersdoc_strip.cpp       # KlustersDoc template strip (kernel-weighted residual)
    document/klustersdoc_spkc.cpp        # KlustersDoc collision rows with the .eap partner subtracted
    document/klustersdoc_collisionscan.cpp # KlustersDoc collisions no sorter named: scan and mark
    document/klustersdoc_residuals.cpp   # KlustersDoc the partner's spike of a collision row (.spkr)
    document/klustersdoc_watershed.cpp   # KlustersDoc 2D density-watershed split (split from klustersdoc.cpp)
    views/klustersview.cpp
    document/klustersyamlreader.cpp
    app/main.cpp
    jobs/minmaxthread.cpp
    data/pair.cpp
    prefs/prefclusterview.cpp
    compute/automerge.cpp
    prefs/prefdialog.cpp
    # prefinput.cpp / buttonchordedit.cpp / wheelchordedit.cpp relocated to libklustersshared
    # (input/) in neuroscope input-overhaul S6 — now shared with neuroscope.
    prefs/prefdisplay.cpp
    prefs/prefsession.cpp
    prefs/prefreclustering.cpp
    prefs/prefrealignment.cpp
    prefs/prefsplitting.cpp
    prefs/prefmerging.cpp
    prefs/prefclusterorder.cpp
    prefs/prefmatrices.cpp
    prefs/prefwaveformview.cpp
    jobs/processlinemaker.cpp
    jobs/processwidget.cpp
    app/pluginregistry.cpp
    dialogs/plugindialog.cpp
    prefs/pluginsettings.cpp          # per-plugin parameter defaults (Preferences > Plugins)
    dialogs/pluginparamform.cpp         # typed parameter form generated from a descriptor
    prefs/prefplugins.cpp             # Preferences > Plugins pages
    views/spinbox.cpp
    jobs/savethread.cpp
    data/sortabletable.cpp
    document/tags.cpp
    data/tracesprovider.cpp
    views/traceview.cpp
    views/tracewidget.cpp
    views/viewwidget.cpp
    jobs/waveformthread.cpp
    views/waveformview.cpp
    prefs/prefdisplaylayout.cpp
    prefs/prefsessionlayout.cpp
    prefs/prefreclusteringlayout.cpp
    prefs/prefrealignmentlayout.cpp
    prefs/prefsplittinglayout.cpp
    prefs/prefmerginglayout.cpp
    prefs/prefclusterorderlayout.cpp
    prefs/prefmatriceslayout.cpp
    prefs/prefwaveformviewlayout.cpp
    prefs/prefclusterviewlayout.cpp
)

# GPU backend source files — each compiled by its own toolchain.
if(USE_CUDA)
    list(APPEND klusters_SRCS compute/groupingassistant_cuda.cu)
    list(APPEND klusters_SRCS compute/pca_refine_cuda.cu)
    list(APPEND klusters_SRCS compute/corecut_cuda.cu)
    list(APPEND klusters_SRCS ${_xcorr_dir}/realign_xcorr_cuda.cu)
endif()
if(USE_HIP)
    list(APPEND klusters_SRCS compute/groupingassistant_hip.hip)
    list(APPEND klusters_SRCS ${_xcorr_dir}/realign_xcorr_hip.hip)
endif()
if(USE_SYCL)
    list(APPEND klusters_SRCS compute/groupingassistant_sycl.cpp)
    list(APPEND klusters_SRCS ${_xcorr_dir}/realign_xcorr_sycl.cpp)
endif()

qt_add_resources(klusters_SRCS klusters-cursors.qrc klusters-icons.qrc klusters-keymaps.qrc klusters-appicon.qrc)

set(klusters_UI prefs/prefwaveformviewlayout.ui prefs/prefdisplaylayout.ui prefs/prefsessionlayout.ui prefs/prefreclusteringlayout.ui prefs/prefrealignmentlayout.ui prefs/prefsplittinglayout.ui prefs/prefmerginglayout.ui prefs/prefclusterorderlayout.ui prefs/prefmatriceslayout.ui prefs/prefclusterviewlayout.ui)
qt_wrap_ui(klusters_SRCS ${klusters_UI})

add_executable(klusters WIN32 ${klusters_SRCS})

# The executable's icon (Explorer, Start menu, shortcuts).
if(WIN32)
    target_sources(klusters PRIVATE klusters.rc)
endif()

# ---------------------------------------------------------------------------
# Apple GPU (Metal) backend — macOS with USE_METAL (doc/design/metal-backend.md).
# Kernel sources are embedded and compiled at run time; no Metal device at run
# time: the dispatchers use the CPU.
# ---------------------------------------------------------------------------
if(TARGET Neurosuite::metal)
    target_sources(klusters PRIVATE ${_xcorr_dir}/realign_xcorr_metal.cpp compute/groupingassistant_metal.cpp)
    ns_metal_embed(klusters realign_xcorr_kernels ${_xcorr_dir}/realign_xcorr.metal)
    ns_metal_embed(klusters groupingassistant_kernels compute/groupingassistant.metal)
    target_compile_definitions(klusters PRIVATE USE_METAL)
    target_link_libraries(klusters PRIVATE Neurosuite::metal)
    message(STATUS "klusters: Metal backend (realign cross-correlation, FP32 error matrix)")
endif()

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
    # Architectures come from CMAKE_CUDA_ARCHITECTURES, which the top-level
    # CMakeLists.txt sets before project(): "native" when nvidia-smi sees a GPU,
    # otherwise the fallback list (cmake/GpuBackends.cmake), or whatever was
    # passed with -DCMAKE_CUDA_ARCHITECTURES=.  No per-target list here: one
    # overrode all of those and asked for sm_120, which CUDA before 12.8 (Ubuntu
    # 26.04's packaged 12.4) refuses.
    set_target_properties(klusters PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
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
    set_source_files_properties(compute/groupingassistant_hip.hip
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
    if(WIN32)
        # Windows: -fsycl only on the two SYCL sources (both Qt-free).  With
        # -fsycl every source also gets a device pass, and on Windows that
        # pass re-reads Qt's headers, where Qt's __FUNCSIG__-based type-name
        # parsing (qmetatype.h typenameHelper, for qvariant_cast<QIcon> etc.)
        # is not a constant expression -- 11 errors per Qt widget source with
        # icx 2025.1 and Qt 6.8.3.  The link below still takes -fsycl, and
        # with Ninja it runs through icx, which bundles the device code.
        set_source_files_properties(
            compute/groupingassistant_sycl.cpp
            "${_xcorr_dir}/realign_xcorr_sycl.cpp"
            PROPERTIES COMPILE_OPTIONS "${SYCL_FLAGS}")
    else()
        target_compile_options(klusters PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:${SYCL_FLAGS}>)
    endif()
    target_link_options(klusters PRIVATE ${SYCL_FLAGS})
    # Link SYCL runtime if the CMake package exports an imported target.
    # Intel renamed the package from IntelDPCPP to IntelSYCL; try both.
    # If neither exports the target, the -fsycl linker flag (set above via
    # target_link_options) is sufficient — the icpx driver pulls in the runtime.
    if(TARGET IntelSYCL::SYCL_CXX)
        target_link_libraries(klusters PRIVATE IntelSYCL::SYCL_CXX)
    elseif(TARGET IntelDPCPP::SYCL_CXX)
        target_link_libraries(klusters PRIVATE IntelDPCPP::SYCL_CXX)
    elseif(WIN32)
        # Windows skips Intel's CMake packages (klusters/CMakeLists.txt), and
        # the link may not go through the icx driver, so name the SYCL runtime
        # import library itself: sycl8.lib in oneAPI 2025, sycl7.lib in 2024.
        get_filename_component(_ns_oneapi_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_library(NS_SYCL_RUNTIME_LIB NAMES sycl8 sycl7 sycl
            HINTS "$ENV{CMPLR_ROOT}/lib" "${_ns_oneapi_bin}/../lib")
        if(NS_SYCL_RUNTIME_LIB)
            target_link_libraries(klusters PRIVATE "${NS_SYCL_RUNTIME_LIB}")
            message(STATUS "SYCL: runtime library ${NS_SYCL_RUNTIME_LIB}")
        else()
            message(WARNING "SYCL: the SYCL runtime library (sycl8.lib) was not found; "
                            "configure in the oneAPI command prompt so CMPLR_ROOT is set")
        endif()
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
    Neurosuite::core                # neurofileio res/clu/spk/fet IO (decollide stage writer)
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::PrintSupport
)

# The sources are in one subdirectory per layer and include one another by bare
# file name ("data.h"): every subdirectory is on the include path.
set(klusters_SOURCE_DIRS app document data views jobs compute dialogs prefs)
foreach(_d IN LISTS klusters_SOURCE_DIRS)
    target_include_directories(klusters PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/${_d}")
endforeach()

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
install(FILES hi128-app-klusters.png DESTINATION share/icons/hicolor/128x128/apps/ RENAME klusters.png)
install(FILES hi256-app-klusters.png DESTINATION share/icons/hicolor/256x256/apps/ RENAME klusters.png)
install(FILES app-klusters.svg DESTINATION share/icons/hicolor/scalable/apps/ RENAME klusters.svg)
install(FILES klusters.desktop DESTINATION share/applications/)

# ---------------------------------------------------------------------------
# Standalone binary converter: text res/fet/clu → binary
# ---------------------------------------------------------------------------
add_executable(klusters_convert_binary compute/convert_to_binary.cpp)
# header-only neurosuite/core/fileops.hpp (replaceFile for the .bak copies)
target_include_directories(klusters_convert_binary PRIVATE "${CMAKE_SOURCE_DIR}/src/libneurosuite-core/src")
install(TARGETS klusters_convert_binary DESTINATION bin)
