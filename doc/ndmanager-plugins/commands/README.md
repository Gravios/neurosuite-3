# Command reference

Every `ndm_*` step and helper, in pipeline order. Each page gives the synopsis,
inputs and outputs, what the step does, and its parameters
([how parameters work](../parameters.md)). The order the steps run in is on
the [pipeline overview](../pipeline.md).

## Running a pipeline

| Command | Purpose |
|---|---|
| [`ndm_start`](ndm_start.md) | Run a pipeline: a pipeline file from ndmanager, or batch over days and acquisition systems |

## 1. Conversion

| Command | Purpose |
|---|---|
| [`ndm_plx2dat`](ndm_plx2dat.md) | Plexon `.plx` / `.pl2` → `.dat`, `.evt`, extra streams, online spikes, header metadata |
| [`ndm_aom2dat`](ndm_aom2dat.md) | AlphaOmega `.mat` (HDF5 v7.3) → `.dat` |
| [`ndm_ncs2dat`](format-conversion.md#ndm_ncs2dat--neuralynx-ncs--dat) | Neuralynx `.ncs` → `.dat` |
| [`ndm_smr2dat`](format-conversion.md) | CED/Spike2 `.smr` → `.dat` |
| [`ndm_nev2evt`](format-conversion.md) | Neuralynx `.nev` → `.evt` |
| [`ndm_smr2evt`](format-conversion.md) | CED/Spike2 events → `.evt` |
| [`ndm_nvt2spots`](format-conversion.md) | Neuralynx `.nvt` tracking → `.spots` / `.sts` |
| [`ndm_smi2sts`](format-conversion.md) | Neuralynx `.smi` → `.sts` |
| [`ndm_tsp2sts`](format-conversion.md) | Amplipex `.tsp` / `.meta` → `.sts` |
| [`ndm_extractleds`](video.md) | LED tracking from video → `.spots` |
| [`ndm_transcodevideo`](video.md) | Transcode video |

## 2. Channels and session set-up

| Command | Purpose |
|---|---|
| [`ndm_resample`](channel-manipulation.md) | Resample `.dat` files to a common rate |
| [`ndm_mergedat`](channel-manipulation.md) | Merge `.dat` files of parallel systems channel-wise |
| [`ndm_extractchannels`](channel-manipulation.md) | Keep, scale or re-reference channels |
| [`ndm_reorderchannels`](channel-manipulation.md) | Reorder channels |
| [`ndm_recolorchannels`](channel-manipulation.md) | Channel display colours (XML parameter files) |
| [`ndm_concatenate`](channel-manipulation.md) | Concatenate a day's sessions |
| [`ndm_xml2yaml`](ndm_xml2yaml.md) | Convert a legacy `.xml` parameter file |
| [`ndm_setupgroups`](ndm_setupgroups.md) | Channel groups and site positions from the probe library |
| [`ndm_spikegrouper`](ndm_spikegrouper.md) | Split spike groups by spike coincidence |
| [`ndm_extractemg`](ndm_extractemg.md) | Remove the EMG component (FastICA); EMG events |

## 3. Filtering and continuous signals

| Command | Purpose |
|---|---|
| [`ndm_hipass`](signal-processing.md) | High-pass filter → `.fil` (Butterworth or median) |
| [`ndm_bandpass`](signal-processing.md) | Band-pass filter → `.fil` (instead of `ndm_hipass`) |
| [`ndm_lfp`](signal-processing.md) | Downsample → `.lfp` |
| [`ndm_slicebinary`](signal-processing.md) | Channel subset of any binary |

## 4. Spike detection

| Command | Purpose |
|---|---|
| [`ndm_detectspikes`](spike-detection.md) | Threshold detection → `.res` |
| [`ndm_extractspikes`](spike-detection.md) | Waveforms at the detected times → `.spk` |
| [`ndm_extractspikes_sdiff`, `ndm_extractspikes_stderiv`](spike-detection.md) | Deprecated aliases: detect + extract with that method |
| [`ndm_spikecleaner`](spike-detection.md) | Drop flat / railed waveforms |
| [`ndm_denoiseuniform`](spike-detection.md) | Drop uniform-noise events |
| [`ndm_alignspikes`](alignment.md) | Align snippets to their peak before PCA |

## 5. Features and clustering

| Command | Purpose |
|---|---|
| [`ndm_pca`](pca.md) | PCA features → `.fet`, basis `.pca` |
| [`ndm_refeaturize`](pca.md) | Re-project onto an existing basis |
| [`ndm_pca_multichannel`](pca.md) | Geometry-aware multi-channel PCA (command-line options) |
| [`ndm_pca_stderiv`](pca.md) | Deprecated alias for `ndm_pca --method stderiv` |
| [`ndm_klustakwik`](ndm_klustakwik.md) | KiloKlustaKwik per spike group → `.clu` |
| [`ndm_autoklusta`](ndm_autoklusta.md) | Learn manual curation and apply it |
| [`kk_build_prior.py`](kk_build_prior.md) | Train an empirical KiloKlustaKwik prior from curation logs |
| [`kk_resolve_prior.py`](kk_resolve_prior.md) | Find the prior for a session and group |

## 6. After curation

| Command | Purpose |
|---|---|
| [`ndm_reextractspikes`](ndm_reextractspikes.md) | Masked second-pass detection, shadow clustering |
| [`ndm_subcluster_unmatched`](ndm_subcluster_unmatched.md) | Re-cluster the unmatched bin |
| [`ndm_stripdat`](ndm_stripdat.md) | Subtract sorted units → `-spkclean.dat` |
| [`ndm_redetectspikes`](ndm_redetectspikes.md) | Detect again on the stripped recording |
| [`ndm_alignspikes_pca`](alignment.md) | Per-cluster realignment |
| [`ndm_decomposecollisions`](ndm_decomposecollisions.md) | Decompose collisions → `.col`, `.eap`, `.tcl` |
| [`process_initeap`](process_initeap.md) | Seed the template-class layer (`.eap`, `.tcl`) |
| [`ndm_localise`](ndm_localise.md) | Per-spike source positions → `.loc` |

## 7. Drift

| Command | Purpose |
|---|---|
| [`ndm_estimatedrift`](ndm_estimatedrift.md) | Probe drift from curated units → `.drift` |
| [`ndm_driftcorrect`](ndm_driftcorrect.md) | Waveforms re-interpolated to the reference position |
| [`ndm_applydrift`](ndm_applydrift.md) | Drift-adaptive KiloKlustaKwik chunks → `.chunks` |
| [`ndm_drifttracker`](ndm_drifttracker.md) | Merge clusters along drift trajectories |

## Housekeeping and internals

| Command | Purpose |
|---|---|
| [`ndm_clean`](ndm_clean.md) | Remove intermediate files |
| [`ndm_functions`, `ndm_custody`](ndm_functions.md) | Shared bash helpers sourced by every script; file-resolution policy |

---

*Part of the [ndmanager-plugins](../README.md) reference.*
