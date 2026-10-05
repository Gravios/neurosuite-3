# =============================================================================
# libneurosuite-core unit tests (regression suite)
#
# Self-contained programs (own main(), assert-based).  Built only when
# NS_BUILD_TESTS=ON; run with `ctest` from the build directory.
# =============================================================================
add_executable(core_test_custody_conformance custody_conformance_test.cpp)
target_compile_features(core_test_custody_conformance PRIVATE cxx_std_17)
target_include_directories(core_test_custody_conformance
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME core_test_custody_conformance
         COMMAND core_test_custody_conformance
                 "${CMAKE_CURRENT_SOURCE_DIR}/custody_vectors.tsv")

# .spk int16 waveform I/O + stage-tagged path composition round-trip.  Links the
# library (unlike the header-only custody test) because readSpk/writeSpk/stagePath
# live in neurofileio.cpp.
add_executable(core_test_neurofileio_spk neurofileio_spk_test.cpp)
target_compile_features(core_test_neurofileio_spk PRIVATE cxx_std_17)
target_include_directories(core_test_neurofileio_spk
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(core_test_neurofileio_spk PRIVATE Neurosuite::core)
add_test(NAME core_test_neurofileio_spk COMMAND core_test_neurofileio_spk)

# Two-component collision-apply engine (decollide.hpp) — header-only, so no link
# (mirrors the custody conformance test).  The noise-free linear case is exact.
add_executable(core_test_decollide decollide_test.cpp)
target_compile_features(core_test_decollide PRIVATE cxx_std_17)
target_include_directories(core_test_decollide
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME core_test_decollide COMMAND core_test_decollide)

# Template-class CRUD engine (templateclass.hpp) over the .eap matrix + .tcl
# registry.  Header-only, but links the library for the writeEap/writeTcl +
# readEap/readTcl persistence round-trip.
add_executable(core_test_templateclass templateclass_test.cpp)
target_compile_features(core_test_templateclass PRIVATE cxx_std_17)
target_include_directories(core_test_templateclass
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(core_test_templateclass PRIVATE Neurosuite::core)
add_test(NAME core_test_templateclass COMMAND core_test_templateclass)

# Decomp -> .eap bridge (decollide_eap.hpp): maps collision constituents onto the
# membership matrix via .tcl provenance.  Header-only, links the library for the
# growEap/writeEap/writeTcl + readEap/readTcl persistence round-trip.
add_executable(core_test_decollide_eap decollide_eap_test.cpp)
target_compile_features(core_test_decollide_eap PRIVATE cxx_std_17)
target_include_directories(core_test_decollide_eap
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(core_test_decollide_eap PRIVATE Neurosuite::core)
add_test(NAME core_test_decollide_eap COMMAND core_test_decollide_eap)

# Native fiber-template port (template_generate.hpp): the LINKED per-unit/class
# median series — drift chunks + energy bins, numpy-matching (median, quantile
# edges, subsample stride, res-frame roll, rint->int16).  Header-only (the pure
# core uses only neurofileio's inline structs), so no link.
add_executable(core_test_template_generate template_generate_test.cpp)
target_compile_features(core_test_template_generate PRIVATE cxx_std_17)
target_include_directories(core_test_template_generate
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME core_test_template_generate COMMAND core_test_template_generate)

# .wti template index (readWti/writeWti): the version-2 schema bump — the trailing
# `parent` lineage column + the `collision` link kind — and v1/v2 back-compat.
# Links the library (the readers/writers live in neurofileio.cpp).
add_executable(core_test_wti wti_test.cpp)
target_compile_features(core_test_wti PRIVATE cxx_std_17)
target_include_directories(core_test_wti
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(core_test_wti PRIVATE Neurosuite::core)
add_test(NAME core_test_wti COMMAND core_test_wti)

# Running mean/std summaries (running_stats.hpp): the lineage node payload math —
# meanStdOfRecords + the EXACT weighted combine combineMeanStd.  Headline invariant:
# combine(A,B) == summary(A∪B), which is why a node keeps no spikes.  Header-only.
add_executable(core_test_running_stats running_stats_test.cpp)
target_compile_features(core_test_running_stats PRIVATE cxx_std_17)
target_include_directories(core_test_running_stats
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME core_test_running_stats COMMAND core_test_running_stats)

# .wtl manual template-linkage sidecar (readWtl/writeWtl + the wtl* view-model
# helpers): the per-node curator lineage (v2 running mean/std/count).  Links the
# library (the readers/writers live in neurofileio.cpp), like the .spk test.
add_executable(core_test_wtl wtl_test.cpp)
target_compile_features(core_test_wtl PRIVATE cxx_std_17)
target_include_directories(core_test_wtl
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(core_test_wtl PRIVATE Neurosuite::core)
add_test(NAME core_test_wtl COMMAND core_test_wtl)

# Manual-lineage renderer (template_generate.hpp renderLineage): a .wtl forest ->
# .wti rows + per-variant .wtf, one median per node over its own spike set (no
# binning), with the kind->link mapping, parent row indices and empty placeholders.
# Header-only (uses only the inline median primitives + neurofileio structs), no link.
add_executable(core_test_template_lineage template_lineage_test.cpp)
target_compile_features(core_test_template_lineage PRIVATE cxx_std_17)
target_include_directories(core_test_template_lineage
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME core_test_template_lineage COMMAND core_test_template_lineage)

# On-disk manual-lineage writer (template_generate.hpp renderLineageToFiles):
# persist a .wtl forest + render it to .wti (v2) + per-variant .wtf on a synthetic
# session.  Links the library (the readers/writers live in neurofileio.cpp).
add_executable(core_test_template_lineage_io template_lineage_io_test.cpp)
target_compile_features(core_test_template_lineage_io PRIVATE cxx_std_17)
target_include_directories(core_test_template_lineage_io
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(core_test_template_lineage_io PRIVATE Neurosuite::core)
add_test(NAME core_test_template_lineage_io COMMAND core_test_template_lineage_io)

# Session-wide drift partition + forest re-grain (drift_partition.hpp): the tiling
# of the session into disjoint time regions shared by all classes, split/delete,
# and the re-bin of pooled drift spikes onto a new grain (leaf re-parenting,
# placeholders).  Header-only (neurofileio structs only), so no link.
add_executable(core_test_drift_partition drift_partition_test.cpp)
target_compile_features(core_test_drift_partition PRIVATE cxx_std_17)
target_include_directories(core_test_drift_partition
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME core_test_drift_partition COMMAND core_test_drift_partition)

# Temporally-restricted projection scope (projectionscope.hpp): cluster->class
# resolution, the union of a class's .wti drift windows, interval merge + the
# in-scope test.  Header-only (builds the structs directly), so no link.
add_executable(core_test_projectionscope projectionscope_test.cpp)
target_compile_features(core_test_projectionscope PRIVATE cxx_std_17)
target_include_directories(core_test_projectionscope
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME core_test_projectionscope COMMAND core_test_projectionscope)

# PCAE transform-method enum conformance.  Pins core::Method's integer values and
# its spatialOrder/hasTemporalDiff/methodTag helpers against the shared table; the
# fiber-kit Python mirror runs an identical copy so the cross-repo enum cannot drift.
add_executable(core_test_pca_method_conformance pca_method_conformance_test.cpp)
target_compile_features(core_test_pca_method_conformance PRIVATE cxx_std_17)
target_include_directories(core_test_pca_method_conformance
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME core_test_pca_method_conformance
         COMMAND core_test_pca_method_conformance
                 "${CMAKE_CURRENT_SOURCE_DIR}/pca_method_vectors.tsv")

# Run the Python mirror (ndm_resolve_io) against the SAME vectors, so the C++
# and Python implementations are kept in lock-step by `ctest`.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME custody_conformance_python
             COMMAND ${Python3_EXECUTABLE}
                     "${CMAKE_SOURCE_DIR}/src/ndmanager-plugins/test/test_custody_conformance.py")
endif()

# Run the bash mirror (ndm_custody) against the SAME vectors, completing the
# three-language lock-step (C++ / Python / bash) enforced by `ctest`.
find_program(BASH_PROGRAM bash)
if(BASH_PROGRAM)
    add_test(NAME custody_conformance_bash
             COMMAND ${BASH_PROGRAM}
                     "${CMAKE_SOURCE_DIR}/src/ndmanager-plugins/test/test_custody_conformance.sh"
                     "${CMAKE_CURRENT_SOURCE_DIR}/custody_vectors.tsv")
endif()
