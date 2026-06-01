# NeuroSuite — Architecture & Restructuring Plan

Status: working draft — updated to reflect work through patch 0027
Scope: `neurosuite-3` (neuroscope, klusters, kiloklustakwik, ndmanager,
ndmanager-plugins, libklustersshared, nphys-data)

This document describes the current structure, the problems that hurt
maintainability, a target layered architecture, and a staged migration that
keeps the tree buildable at every step. It is deliberately incremental: no
big-bang rewrite, because the interactive apps (klusters especially) are daily
tools and the GPU/Qt build cannot be exercised in every environment.

---

## 0. Where we are (progress to date)

Delivered as a 27-patch `git format-patch` series on top of base `9c9eb04`,
applied and (except where noted) build-confirmed on `nphy-069`.

| Area | Patches | State |
|---|---|---|
| xcorr/realign kernel fix + RMS circular recenter + prefs UI | 0001–0005 | done, hardware-confirmed |
| Dedup RMS recenter into shared `realign_center` | 0006–0007 | done |
| `neurofileio` shared format I/O + **all** reader migrations (Stage 0) | 0008–0017 | done |
| KlustaKwikExp → KiloKlustaKwik rebrand + removal of old `klustakwik` | 0018–0022 | done, hardware-confirmed (engine duplication resolved) |
| Per-subproject directory standardization (sources under `src/`) | 0023–0025 | done; **0023 (kiloklustakwik) needs a CUDA build-test** |
| Changelog consolidation into a single `CHANGELOG.md` + `doc/design/` | 0026 | done (docs-only) |
| `STANDARDIZATION.md` updated for the layout + changelog standards | 0027 | done (docs-only) |

Mapped onto the staged plan in §3:

- **Stage 0 (finish reader migrations): COMPLETE.** `neurofileio` owns
  `.evt`, `.clu`/`.res`, `.fet`, and interleaved-int16 `.dat`; all three
  neuroscope providers and klusters `data.cpp` consume it.
- **Stage 4 (engine de-duplication): the duplication is GONE — by a different
  route than planned.** Rather than folding `klustakwikExp` *into*
  `klustakwik`, the smaller canonical `klustakwik` was removed and the superset
  fork was kept and renamed `kiloklustakwik` (the single engine). The remaining
  Stage-4 work — decomposing the 17.5k-line `KK.cpp` into phase TUs — is **not**
  done.
- **Stages 1, 2, 3, and the GUI de-fork (Stage 5): NOT started.** Six YAML
  readers still exist; `${_xcorr_dir}` shared-source compilation is still in
  use; libklustersshared is still a mixed-layer library; the neuroscope/klusters
  TraceView fork is untouched.
- **Not in the original plan but done:** the product rebrand (0018–0022), a
  *per-subproject internal* layout standardization (0023–0025, a precursor to
  the §2 `libs/apps/plugins` layering, not the layering itself), and the
  documentation/changelog consolidation (0026–0027).

The foundation work the plan front-loaded (format I/O centralization, killing
the engine duplication) is in place. The risky structural layers
(io/core/compute split, GUI de-fork) are still ahead.

---

## 1. Current structure

Seven peers under `src/` (was eight; the second clustering engine has been
removed):

```
src/
  neuroscope/         Qt viewer (traces, clusters, events, positions)
  klusters/           Qt cluster-cutting GUI
  kiloklustakwik/     clustering engine (the former klustakwikExp superset,
                      renamed; old canonical klustakwik removed)
  ndmanager/          parameter-file manager
  ndmanager-plugins/  processing plugins (pca, nlxconvert, drifttracker, …)
  libklustersshared/  shared library (Qt widgets + types + I/O + compute)
  nphys-data/         data/resources
```

Each peer now follows one standard internal layout (patches 0023–0025): build
and packaging files at the subproject root, **all** C/C++ sources under `src/`,
tests in `test/`, docs in `doc/`. This is internal tidy-up within the existing
flat peer structure — not yet the layered `libs/apps/plugins` reorganization of
§2.

### 1.1 Problems (status against the original measurements)

The recurring theme was **the same logic maintained in 2–4 hand-edited
copies.** Status of each:

- **Two parallel clustering engines — RESOLVED (0018–0022).** `klustakwik` and
  `klustakwikExp` shared ~28 identically-named files; `KK.cpp` was 5,239 lines
  in one and 17,545 in the other. The smaller canonical engine was removed and
  the superset kept as the single `kiloklustakwik`. There is now one engine.
  (Its `KK.cpp` is still 17,545 lines — see "monolithic TUs" below.)
- **Format I/O — CENTRALIZED (0008–0017).** The `.clu`/`.res`/`.fet`/`.evt`/
  `.dat` readers that were re-implemented per app now go through a single
  `neurofileio` in libklustersshared. (The *parameter/session YAML* readers are
  a separate problem, still open — next item.)
- **Six config readers for two file formats — OPEN.** `klustersyamlreader`,
  `neuroscopeyamlreader`, `descriptionyamlreader`, `ndmanageryamlreader`,
  `KlustaKwikYaml`, and `sessionyamlreader` all still parse the same
  parameter/session YAML alongside the shared `ParameterYamlReader`. (The
  ndmanager/neuroscope *root duplicates* of these were dead leftovers and were
  removed in 0024/0025; the live per-app readers remain.)
- **Forked GUI between neuroscope and klusters — OPEN.** `traceview.cpp` (~5k
  lines each), `tracewidget`, and the four providers are still two copies.
  (The neuroscope providers' *format reads* now share `neurofileio`, but the
  view/widget/provider classes themselves are still forked.)
- **Monolithic translation units — OPEN.** `KK.cpp` 17,545; `klustersdoc.cpp`
  ~6.8k; `klusters.cpp` ~6.7k; `data.cpp` ~5.7k; the two `traceview.cpp` ~5k.
- **Two different "sharing" idioms — OPEN.** libklustersshared is still consumed
  both as a linked library and as *shared source* (`${_xcorr_dir}` xcorr/realign
  kernels compiled per-app in kiloklustakwik, klusters, and libklustersshared).
- **libklustersshared is a grab-bag — OPEN.** It still holds Qt widgets, data
  types, parameter I/O, compute kernels, and file I/O in one library.
- **Dead code retained — OPEN (tracked).** The QtXml/`QDomDocument` readers and
  the `SpikeRealign` class are still present, unbuilt and uncalled, per the
  "leave any QtXml alone" decision. See §5.

---

## 2. Target architecture

Organizing principle: **layered libraries with single ownership; apps are thin
wiring.** Layers are split by dependency weight — Qt-free at the bottom, Qt only
at the top — so a clustering engine never links Qt and a format reader never
links the GPU runtime.

```
src/
  libs/
    neuro-io/        formats: clu/res/fet/evt/dat + param/session YAML   [no Qt]
    neuro-core/      data model, types, channel/cluster structures       [no Qt]
    neuro-compute/   xcorr, realign, sorting math; CPU + GPU backends     [no Qt]
    neuro-ui/        shared Qt widgets, providers, views                  [Qt]
  apps/
    neuroscope/      document + wiring over ui/core/io
    klusters/        clustering UI over ui/core/io/compute
    kiloklustakwik/  one engine over core/io/compute
    ndmanager/       parameter manager over io
  plugins/
    ndmanager-plugins/...
```

### 2.1 Dependency rules (must not be violated)

| Layer         | May depend on            | Must NOT depend on        |
|---------------|--------------------------|---------------------------|
| neuro-io      | std, yaml-cpp            | Qt, GPU runtime, core/ui  |
| neuro-core    | neuro-io, std            | Qt, GPU runtime, ui       |
| neuro-compute | neuro-core, std, CUDA/HIP/SYCL | Qt, ui               |
| neuro-ui      | neuro-core, neuro-io, Qt | neuro-compute internals*  |
| apps          | any libs                 | each other                |

\* neuro-ui may *invoke* compute via a narrow façade, but should not embed
kernel source. Apps never depend on other apps (kills the neuroscope↔klusters
fork at the build level).

### 2.2 Per-layer detail

**neuro-io** (foundation landed; not yet a separate target). Single source of
truth for every on-disk format. Pure, std-only, unit-testable without Qt or
hardware. Owns:
- `neurofileio` — `.clu`/`.res`/`.fet`/`.evt` and interleaved-int16 `.dat`/`.lfp`
  (**landed and in use**: `klustersshared/neurofileio.{h,cpp}`, all readers
  migrated).
- one **parameter model** + reader/writer, replacing the six per-app YAML
  readers (**not yet** — this is Stage 1).
- the **session model** primitives the `.nrs` reader/writer build on.
Apps consume the model via a thin adapter; they no longer carry a parser.

**neuro-core.** The Qt-free data model the apps and engine share: `Array`,
`types`, `customtypes`, channel/cluster/feature structures, the in-memory
cluster/spike tables. Depends only on neuro-io. (Today these types live in
libklustersshared alongside Qt code — §3 Stage 3 carves them out.)

**neuro-compute.** The realignment/xcorr/sorting math as a *real linked
library*, not per-app source. CPU path always compiled; CUDA/HIP/SYCL as
per-backend OBJECT/INTERFACE targets selected at configure time, linked once.
Owns the xcorr backends, `realign_center` (landed), and the dispatch. (Today
still compiled per-app via `${_xcorr_dir}` — Stage 2.)

**neuro-ui.** The shared Qt layer: the widgets currently in
`libklustersshared/gui` **plus the de-forked providers and views**. One
`TraceView`, one `TraceWidget`, one of each provider, parameterized over the
real app differences (klusters is interactive and edits clusters; neuroscope is
read-only display).

### 2.3 What the apps become

Thin. neuroscope = document + session + app wiring over ui/core/io. klusters =
cluster-editing controller + the clustering-specific UI over
ui/core/io/compute. kiloklustakwik = one engine over core/io/compute. ndmanager
= parameter editor over io.

---

## 3. Migration plan (staged, each step independently buildable)

Ordering front-loads value and settles the foundation before the risky forks.
Do not start the GUI de-fork or the engine decomposition until io/core/compute
are clean — those layers are the substrate the de-forked code sits on.

### Stage 0 — reader migrations — DONE
- [x] `neurofileio` shared module, unit-tested (patch 0008).
- [x] neuroscope `eventsprovider` → `readEvt` (0009).
- [x] neuroscope `clustersprovider` (.clu/.res) (0010–0011).
- [x] neuroscope `tracesprovider` (.dat) (0012).
- [x] Export the public API (`KLUSTERSSHARED_EXPORT`) (0013).
- [x] klusters `data.cpp` `loadClusters` (.clu) (0014) and `loadFeatures`
      (.fet) (0016–0017).
- [x] neuroscope session-parse robustness fix (0015).
- [ ] Re-confirm the full read path on `nphy-069` after the 0023 restructure.

### Stage 1 — neuro-io as a layer (low risk) — NOT STARTED
- [ ] Promote `neurofileio` + the parameter/session model into a `neuro-io`
      target (initially still inside libklustersshared, then split out).
- [ ] Introduce one parameter model + reader/writer; retire the six per-app
      YAML readers one at a time, each behind a thin adapter. Build between each.

### Stage 2 — neuro-compute as a real library (mechanical) — NOT STARTED
- [ ] Convert the `${_xcorr_dir}` shared-source pattern into a linked
      `neuro-compute` target with per-backend OBJECT libraries.
- [ ] Drop `${_xcorr_dir}` from the three consumers; link the library instead.

### Stage 3 — split the grab-bag — NOT STARTED
- [ ] Carve libklustersshared into `neuro-core` (Qt-free types/model) and
      `neuro-ui` (Qt widgets). Pure dependency hygiene; no logic change.

### Stage 4 — collapse the engine duplication — PARTIALLY DONE
- [x] One engine, not two: old canonical `klustakwik` removed; the superset
      fork kept and renamed `kiloklustakwik` (0018–0022). This resolved the
      duplication liability by removal rather than the originally-planned merge.
- [ ] Break `KK.cpp` (still 17,545 lines) along phase boundaries (EM core,
      phase drivers, realign orchestration, I/O) into separate TUs — continuing
      the existing pattern (`dipsplit`, `adapt_model`, … are already separate).
- [ ] Gate experimental phases behind strategy objects / runtime flags rather
      than leaving them inline in the one large TU.

### Stage 5 — de-fork the GUI (high value, highest risk) — NOT STARTED
- [ ] Unify the two `TraceView`/`TraceWidget`/providers into `neuro-ui`,
      parameterized over the interactive-vs-read-only difference.

### Interleaved structural / hygiene work (done, off the critical path)
- [x] Per-subproject internal layout standardized: sources under `src/`, build
      files at the root (0023–0025). A precursor to the §2 layering, applied
      without changing the flat peer structure.
- [x] Documentation consolidated: single canonical `CHANGELOG.md`, topic notes
      under `doc/design/`, workflows under `doc/workflows/` (0026); standards
      doc `STANDARDIZATION.md` updated to codify the layout and changelog
      conventions (0027).

### Sequencing note
Stage 4's `KK.cpp` decomposition rewrites the file where the recenter work
(0005/0007) lives, and Stages 1–3 are the substrate for the GUI de-fork. The
order above still holds: io → core/compute → engine internals → GUI.

---

## 4. Conventions to standardize during the move

- One sharing mechanism: linked libraries with public headers
  (`#include <neuro-io/...>` etc.). No shared-source compilation. *(Still
  violated by `${_xcorr_dir}`; Stage 2 closes it.)*
- Qt-free below neuro-ui. A library either links Qt or it does not; no partial.
- File I/O only through neuro-io. No app re-implements a format parser. *(Holds
  for the binary/text formats via `neurofileio`; the YAML readers are the
  remaining exception, Stage 1.)*
- Existing house style is good and should carry across: box-drawing section
  dividers, prose docstrings, `[INFO]`/`[WARN]` log prefixes.
- Subproject layout and documentation conventions are now written down in
  `STANDARDIZATION.md` §2.7 (directory layout) and §2.8 (docs/changelog).

## 5. Dead-code disposition (tracked, not yet actioned)

All XML readers/writers are `QtXml`/`QDomDocument`-based and were left untouched
by request. They are unbuilt and uncalled. Recommended eventual handling: move
to an `attic/` or exclude with an explicit deprecation note rather than delete,
so the QtXml decision is honored while signaling they are not live:
`neuroscopexmlreader`, `klustersxmlreader`, `parameterxmlmodifier` (neuroscope +
klusters), `parameterxmlcreator`, `sessionxmlwriter`, ndManager
`descriptionwriter`, and the dead `SpikeRealign` class.
