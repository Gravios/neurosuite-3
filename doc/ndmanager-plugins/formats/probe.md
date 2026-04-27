# `.probe` — probe configuration (YAML)

Read by `ndm_setupgroups`, `ndm_estimatedrift`, `ndm_localise`. Stored
in the probe library at `${CMAKE_INSTALL_DATADIR}/neurosuite/probes/`.
Entries in the session YAML `probes:` section reference files by path
relative to the library root (e.g. `neuronexus/Buzsaki64.probe`).


---

*Part of the [ndmanager-plugins](../README.md) file-format reference.*
