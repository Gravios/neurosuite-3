foreach(_size IN ITEMS 16x16 22x22 32x32 48x48 64x64)
    file(GLOB _icons "${CMAKE_CURRENT_SOURCE_DIR}/${_size}/*.png")
    install(FILES ${_icons}
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}/mimetypes")
endforeach()

install(FILES nphys-data.xml
    DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages")

# ---------------------------------------------------------------------------
# Probe library
# ---------------------------------------------------------------------------
# .probe files are YAML-format electrode configuration files read at runtime
# by ndmanager and ndm_estimatedrift.  They are installed to:
#   ${CMAKE_INSTALL_DATADIR}/neurosuite/probes/<vendor>/
# which maps to /usr/local/share/neurosuite/probes/<vendor>/ by default.
#
# A relative probeFile is looked up in this order, first match wins (see
# doc/ndmanager-plugins/commands/ndm_setupgroups.md; ndmanager's Probes tab
# uses the same order):
#   session_dir/, session_dir/probes/, probeLibrary (ndm_setupgroups),
#   probeLibraryPath (session YAML), $NEUROSUITE_PROBE_PATH,
#   $NS3_PROBE_LIBRARY_PATH (legacy), then neurosuite/probes/ under
#   $XDG_DATA_HOME and each $XDG_DATA_DIRS entry.

file(GLOB _probes_neuronexus "${CMAKE_CURRENT_SOURCE_DIR}/probes/neuronexus/*.probe")
install(FILES ${_probes_neuronexus}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/neurosuite/probes/neuronexus")

file(GLOB _probes_general "${CMAKE_CURRENT_SOURCE_DIR}/probes/general/*.probe")
install(FILES ${_probes_general}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/neurosuite/probes/general")
