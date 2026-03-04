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
# which maps to /usr/local/share/neurosuite/probes/neuronexus/ by default.
#
# The runtime search order (lowest to highest priority) is:
#   /usr/share/neurosuite/probes/
#   /usr/local/share/neurosuite/probes/
#   ~/.local/share/neurosuite/probes/
#   $NEUROSUITE_PROBE_PATH
#   session_dir/probes/
#
file(GLOB _probes "${CMAKE_CURRENT_SOURCE_DIR}/probes/neuronexus/*.probe")
install(FILES ${_probes}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/neurosuite/probes/neuronexus")
