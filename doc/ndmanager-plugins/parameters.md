# Parameters — how the `ndm_*` steps are configured

Every step reads its settings from the session parameter file,
`SESSION.yaml`. This page covers what all steps share: where parameters go,
how a step finds them, and the conventions behind `method`,
`inputExtension` and the KiloKlustaKwik settings. Each command page lists
its own parameters.

**Contents:** [Where parameters live](#where-parameters-live) ·
[How a step looks a parameter up](#how-a-step-looks-a-parameter-up) ·
[Empty values](#empty-values) · [`method` and `methodOrder`](#method-and-methodorder) ·
[`inputExtension`](#inputextension) · [KiloKlustaKwik parameters](#kiloklustakwik-parameters) ·
[The template](#the-template) · [ndmanager](#ndmanager)

---

## Where parameters live

```yaml
programs:
  - name: ndm_hipass            # one block per step
    parameters:
      - {name: filter,     value: butterworth, status: Optional}
      - {name: highPassHz, value: 800,         status: Optional}
  - name: general               # optional: values any step may fall back to
    parameters:
      - {name: memCapGB, value: 8}
```

Besides `programs:`, steps read the session description itself:
`acquisitionSystem` (channel count, sampling rate, bits), `fieldPotentials`,
`anatomicalDescription` and `spikeDetection` channel groups (written by
[`ndm_setupgroups`](commands/ndm_setupgroups.md)), `probes:`, and `files:`.

`status` (`Mandatory` / `Optional`) is a label for editors such as
ndmanager; the script decides what it requires. A missing value a script
needs stops it with an error naming the parameter.

A step is run with the session as its only argument — `ndm_hipass
SESSION.yaml`, `ndm_hipass SESSION`, or no argument inside a directory named
after the session. Legacy `.xml` parameter files are still read through
`xpathReader`, but convert them once with
[`ndm_xml2yaml`](commands/ndm_xml2yaml.md).

## How a step looks a parameter up

A step looks a parameter up in, in order:

1. its own block (`- name: ndm_<step>`);
2. a **fallback block** where the step declares one — for example
   `ndm_redetectspikes` and `ndm_reextractspikes` take `thresholdFactor`,
   `refractoryPeriod`, `peakSearchLength`, `start` and `duration` from
   `ndm_detectspikes` unless their own block sets them, and
   `ndm_refeaturize` takes `dropLastChannel` from `ndm_pca`. The deprecated
   aliases (`ndm_extractspikes_sdiff`, `_stderiv`, `ndm_pca_stderiv`) fall back
   to their old blocks the same way;
3. the `general` block.

Leaving a parameter out of a step's block is therefore how you inherit it.
A few parameters belong to another step's block outright — e.g.
`ndm_redetectspikes` reads `windowHalfLength` from `ndm_hipass`.

Some parameters have an older name that is still accepted
(`methodOrder` ← `sdiffOrder`, `channelsExcluded` ← `excluded`).

## Empty values

A parameter with no value, an empty string, or `~` (YAML null, also the
quoted string `"~"` older ndmanager builds wrote) counts as **unset**: the
step uses its default.

## `method` and `methodOrder`

Steps that read or write per-group spike files take a `method` — the
detection transform the files belong to. It is part of every per-group file
name, `SESSION.<type>.<method>.N` (see [file naming](formats/naming.md)):

| `method` | Meaning |
|---|---|
| `standard` | raw waveforms (default) |
| `sdiff` | spatial derivative across the group's channels |
| `stderiv` | spatial then temporal derivative |
| `stderiv_S<n>` / `stderiv_C<n>` | stderiv with a fixed order, plain (`S`) or with the group's `sdiffPairs` patterns (`C4`, `C5`, `C6` for expressions) |

`methodOrder` (1–3, default 3) is the spatial-derivative order for
`sdiff` / `stderiv`. Every step of one sort must use the same `method`: a
mismatch makes a step look for files that were never written. Steps that
accept `--method` on the command line let it override the YAML. Details:
[spike detection](commands/spike-detection.md#methods-method-methodorder).

## `inputExtension`

`ndm_hipass`, `ndm_bandpass`, `ndm_lfp`, `ndm_slicebinary`,
`ndm_detectspikes` and `ndm_extractspikes` read `SESSION.<inputExtension>`
(defaults `dat`, `dat`, `dat`, `lfp`, `fil`, `fil`). A leading dot is
ignored (`.lfp` = `lfp`). When the parameter is set, that file must exist: a
missing one stops the step with an error. Only an unset parameter whose
default input is missing gives the "skipping this step" warning.
`emgclean` reads the EMG-cleaned recording written by
[`ndm_extractemg`](commands/ndm_extractemg.md).

## KiloKlustaKwik parameters

[`ndm_klustakwik`](commands/ndm_klustakwik.md) and the steps that run
KiloKlustaKwik for it look each clustering parameter up in four places:

1. the spike group: `spikeDetection.channelGroups[g].klustakwik.<name>`;
2. the calling step's block (e.g. `ndm_subcluster_unmatched`), when another
   step runs the clustering;
3. the `ndm_klustakwik` block;
4. the built-in default.

So a per-group override goes in the group, a session-wide one in
`ndm_klustakwik`.

## The template

`templates/template.yaml` is the reference session file: every parameter
every step reads, with its default and help text. Two tests keep it honest
(`src/ndmanager-plugins/test/`):

- `test_template_parameters.py` derives each step's parameters from the
  scripts and fails when the template misses one or lists one nothing reads;
- `test_pipeline_designer_defs.py` checks that ndmanager's Pipeline designer
  offers every step with the template's parameters.

## ndmanager

ndmanager edits the same file: the **Plugins** tab shows each `programs:`
block with its help, and the **Pipeline** tab places steps in a graph saved
as `SESSION.ndm.<name>.pipeline`, which [`ndm_start`](commands/ndm_start.md)
runs. A step dropped onto the graph starts with the template's parameters.
See [the ndmanager guide](../ndmanager/README.md#pipeline-tab--editable-node-graph).

---

*Part of the [ndmanager-plugins](README.md) reference.*
