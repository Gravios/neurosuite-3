# File formats

The files the `ndm_*` steps read and write. Per-group spike files follow one
naming scheme, `SESSION.<type>.<method>.N` — start with
[file naming](naming.md).

## Continuous signals

| File | Description |
|---|---|
| [`.dat` / `.fil` / `.lfp`](dat.md) | Interleaved int16 signals (wide-band, filtered, LFP; also `-spkclean.dat`, `.emgclean`, slices) |
| [`.px<stream>`](pxstream.md) | Extra Plexon continuous streams (`.pxai`, `.pxfp`, …) |
| [EMG files](emg.md) | `-emgclean.dat`, `.emg.<probe>`, EMG events and metadata from `ndm_extractemg` |

## Spikes and clusters

| File | Description |
|---|---|
| [Naming convention](naming.md) | `<base>.<type>.<method>.<group>` and how readers resolve it |
| [`.res.<method>.N`](res.md) | Spike times (shared by all methods of a group) |
| [`.spk.<method>.N`](spk.md) | Spike waveforms |
| [`.fet.<method>.N`](fet.md) | Feature vectors |
| [`.pca.<method>.N`](pca.md) | PCA basis |
| [`.clu.<method>.N`](clu.md) | Cluster assignments |
| [`.clc.<method>.N`](clc.md) | Child (atom) clusters of a hierarchical clustering |
| [`.clp.<method>.N`](clp.md) | Atom → fiber map |
| [`.cli.<method>.N`](cli.md) | Cluster information (structure, type, quality, notes) of one clustering |
| [`.pxres` / `.pxclu` / `.pxspk.N`](pxspikes.md) | Plexon's online spikes |

## Post-sorting

| File | Description |
|---|---|
| [`.col.<method>.N`](col.md) | Collision decomposition results |
| [`.eap.N`](eap.md) | Template-class membership and offsets |
| [`.tcl.N`](tcl.md) | Template-class registry |
| [`.wti.N`](wti.md) | Template-waveform index |
| [`.wtf.<method>.N`](wtf.md) | Template-waveform stack |
| [`.wtl.N`](wtl.md) | Manual template-lineage forest |
| [`.loc.N`](loc.md) | Per-spike source locations |
| [`.drift`](drift.md) | Probe drift trajectories (and `.dat.drift.P`) |
| [`.chunks.N`](chunks.md) | Drift-adaptive KiloKlustaKwik chunk boundaries |

## Session, probes, events

| File | Description |
|---|---|
| [`.probe`](probe.md) | Probe geometry and channel binding |
| [`.evt`](evt.md) | Timestamped events (text) |
| [`.pos`](pos.md) | Video tracking positions |

The session parameter file `SESSION.yaml` is described in
[parameters](../parameters.md) and `templates/template.yaml`.

---

*Part of the [ndmanager-plugins](../README.md) reference.*
