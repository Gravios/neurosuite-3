# Pipeline overview

The order in which the `ndm_*` steps run, from a raw recording to sorted,
curated and post-processed spikes. Each step reads and writes files next to
the session's `SESSION.yaml`; the command pages have the details and
[parameters](parameters.md) explains how steps are configured.

**Contents:** [Ways to run the steps](#ways-to-run-the-steps) ·
[1. Get a `.dat`](#1-get-a-dat) · [2. Set up the session](#2-set-up-the-session) ·
[3. Filter](#3-filter) · [4. Detect and extract spikes](#4-detect-and-extract-spikes) ·
[5. Features and clustering](#5-features-and-clustering) ·
[6. After curation](#6-after-curation) · [7. Drift](#7-drift) ·
[Re-running](#re-running)

```
 acquisition file (.plx/.pl2, .mat, .ncs, .smr, …)
        │  1  ndm_plx2dat · ndm_aom2dat · ndm_ncs2dat / ndm_smr2dat (+ resample, merge, concatenate)
        ▼
 SESSION.dat ─────────────────────────────┐
        │  2  ndm_setupgroups  (probes: → channel groups)      ndm_lfp → SESSION.lfp
        │     [ndm_extractemg → SESSION.emgclean]               ndm_slicebinary → excerpts
        │  3  ndm_hipass  or  ndm_bandpass
        ▼
 SESSION.fil
        │  4  ndm_detectspikes → .res.<m>.N     ndm_extractspikes → .spk.<m>.N
        │     [ndm_alignspikes]
        │  5  ndm_pca → .fet.<m>.N, .pca.<m>.N     ndm_klustakwik → .clu.<m>.N
        ▼
 Klusters — manual curation   ([ndm_autoklusta] learns and replays it)
        │  6  ndm_reextractspikes → ndm_subcluster_unmatched
        │     ndm_stripdat → ndm_redetectspikes
        │     ndm_decomposecollisions · ndm_localise · ndm_alignspikes_pca → ndm_refeaturize
        │  7  ndm_estimatedrift → ndm_driftcorrect / ndm_applydrift / ndm_drifttracker
        ▼
 re-sort (5) with the corrections, curate again
```

`<m>` is the step's `method` (`standard`, `sdiff`, `stderiv…`); every step of
one sort uses the same one. See [file naming](formats/naming.md).

---

## Ways to run the steps

- **By hand**, one step at a time: `ndm_hipass SESSION.yaml`. Every step
  skips (exit 10) when its output already exists, so a chain can be re-run
  after an interruption.
- **With a pipeline file**: ndmanager's Pipeline tab saves a graph of steps as
  `SESSION.ndm.<name>.pipeline`; [`ndm_start`](commands/ndm_start.md) runs it
  in graph order (`NDM_PIPELINE=<name>`, default `default`).
- **Batch, many days and acquisition systems**:
  [`ndm_start`](commands/ndm_start.md) with a template parameter file and
  `<base>-<suffix>/` data directories converts, merges and concatenates each
  day, then runs the selected steps per session.

## 1. Get a `.dat`

| System | Step | Writes |
|---|---|---|
| Plexon `.plx` / `.pl2` | [`ndm_plx2dat`](commands/ndm_plx2dat.md) | `.dat`, `.plx.evt`, `.px<stream>`, `.pxres/.pxclu/.pxspk.N`, header metadata in the YAML |
| AlphaOmega `.mat` | [`ndm_aom2dat`](commands/ndm_aom2dat.md) | `.dat` |
| Neuralynx, CED/Spike2, Amplipex | [format conversion](commands/format-conversion.md) | `.dat`, `.evt`, `.spots` / `.sts` per system |
| several systems or sessions | [channel manipulation](commands/channel-manipulation.md) — `ndm_resample`, `ndm_mergedat`, `ndm_extractchannels`, `ndm_reorderchannels`, `ndm_concatenate` | one `.dat` |
| video | [video](commands/video.md) — `ndm_extractleds`, `ndm_transcodevideo` | `.spots`, transcoded video |

## 2. Set up the session

- [`ndm_setupgroups`](commands/ndm_setupgroups.md) turns the `probes:` list
  into anatomical and spike groups with site positions
  ([`.probe` files](formats/probe.md)); [`ndm_spikegrouper`](commands/ndm_spikegrouper.md)
  can split groups by spike coincidence instead.
- [`ndm_lfp`](commands/signal-processing.md#ndm_lfp--create-lfp-file-dat--lfp) →
  `SESSION.lfp`; [`ndm_slicebinary`](commands/signal-processing.md) cuts
  channel subsets out of any binary.
- Optional: [`ndm_extractemg`](commands/ndm_extractemg.md) removes the EMG
  component (`SESSION.emgclean`) and writes EMG events; filter the cleaned
  signal with `inputExtension: emgclean`.

## 3. Filter

[`ndm_hipass`](commands/signal-processing.md) (zero-phase Butterworth by
default, or the median filter) **or** [`ndm_bandpass`](commands/signal-processing.md)
writes `SESSION.fil`. Only one of them: the second finds `.fil` and skips.

## 4. Detect and extract spikes

[Spike detection](commands/spike-detection.md):

1. `ndm_detectspikes` — threshold detection → `SESSION.res.<m>.N`
   (spike times are shared by all methods of a group);
2. `ndm_extractspikes` — waveforms at those times → `SESSION.spk.<m>.N`.

Optional clean-up: `ndm_spikecleaner`, `ndm_denoiseuniform`. Optional
alignment before PCA: [`ndm_alignspikes`](commands/alignment.md).

## 5. Features and clustering

- [`ndm_pca`](commands/pca.md) → `.fet.<m>.N` and the basis `.pca.<m>.N`;
  [`ndm_refeaturize`](commands/pca.md) re-projects onto an existing basis.
- [`ndm_klustakwik`](commands/ndm_klustakwik.md) → `.clu.<m>.N`. The external
  fiber-kit sorter can take its place as the `ndm_fiber-kit` plugin.
- Curate in Klusters. [`ndm_autoklusta`](commands/ndm_autoklusta.md) learns
  merge/split decisions from curated sessions and applies them to new ones.

## 6. After curation

| Goal | Steps |
|---|---|
| find spikes missed by detection | [`ndm_reextractspikes`](commands/ndm_reextractspikes.md) (masked second pass, shadow clusters) → [`ndm_subcluster_unmatched`](commands/ndm_subcluster_unmatched.md) |
| subtract sorted units and detect again | [`ndm_stripdat`](commands/ndm_stripdat.md) → `SESSION-spkclean.dat` → [`ndm_redetectspikes`](commands/ndm_redetectspikes.md) → re-run 5 |
| separate overlapping spikes | [`ndm_decomposecollisions`](commands/ndm_decomposecollisions.md) → `.col`, `.eap`, `.tcl` ([`process_initeap`](commands/process_initeap.md) seeds the template classes) |
| spike positions on the shank | [`ndm_localise`](commands/ndm_localise.md) → `.loc` |
| realign spikes within clusters | [`ndm_alignspikes_pca`](commands/alignment.md) → [`ndm_refeaturize`](commands/pca.md) |
| empirical KiloKlustaKwik priors | [`kk_build_prior.py`](commands/kk_build_prior.md), [`kk_resolve_prior.py`](commands/kk_resolve_prior.md) |

## 7. Drift

[`ndm_estimatedrift`](commands/ndm_estimatedrift.md) estimates probe drift
from the curated units (`SESSION.drift`, `SESSION.dat.drift.P`). Then:

- [`ndm_driftcorrect`](commands/ndm_driftcorrect.md) re-interpolates the
  waveforms to the reference position;
- [`ndm_applydrift`](commands/ndm_applydrift.md) writes drift-adaptive
  KiloKlustaKwik chunk boundaries (`.chunks.N`);
- [`ndm_drifttracker`](commands/ndm_drifttracker.md) merges clusters whose
  waveforms follow the same drift trajectory.

Iterate sort → curate → estimate → correct until the drift estimate settles
([drift correction workflow](../workflows/drift-correction.md)).

## Re-running

Steps skip when their output exists (exit 10) and when their input is
missing (exit 11); [`ndm_clean`](commands/ndm_clean.md) removes intermediate
files. To redo a step, delete or rename its output, or set its `overwrite`
parameter where it has one.

---

*Part of the [ndmanager-plugins](README.md) reference.*
