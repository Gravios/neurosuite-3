# neurosuite-3 — Developer Guide

The entry point for developers working on the **internals** of neurosuite-3 — the
code-level references you need to modify or extend a component, as distinct from the
user- and pipeline-facing documentation in [`doc/`](doc/README.md).

This page is the **map**: each subsystem gets its own reference document, and the table
of contents below links them together. Many subsystems now have a full reference; the
entries still marked 🚧 are intentional stubs to be filled in over time.

Where references live:

- Docs that describe a specific component's code live **next to that code** (e.g.
  [`src/klusters/docs/`](src/klusters/docs/)).
- Broad, project-wide references live at the **repository root** alongside this guide
  ([`ARCHITECTURE.md`](ARCHITECTURE.md), [`STANDARDIZATION.md`](STANDARDIZATION.md),
  [`BUILD.md`](BUILD.md), [`GLOSSARY.md`](GLOSSARY.md), [`ROADMAP.md`](ROADMAP.md)).
- User- and pipeline-facing documentation for every component is in [`doc/`](doc/README.md).

The code is always the source of truth; when a reference and the code disagree, fix the
reference.

**Legend:** ✅ written · 🚧 planned (an intentional stub — contribute here)

---

## Table of contents

### Orientation
- **[Building neurosuite-3](BUILD.md)** — ✅ — the developer build reference: every CMake option, the GPU backends, the ctest suite, the AddressSanitizer build, and CI parity. (For a first build, start with the README [Quick start](README.md#quick-start) and the per-component `install/` guides under [`doc/`](doc/README.md).)
- **[Glossary](GLOSSARY.md)** — ✅ — component names, domain terms, on-disk formats, and architecture concepts in one place.
- **[Architecture & restructuring plan](ARCHITECTURE.md)** — ✅ — the current layered structure, the maintainability problems being addressed, and the staged migration (project-wide; a working draft).
- **[Component & pipeline docs](doc/README.md)** — ✅ — user- and pipeline-facing documentation for every component (build, usage, the sorting pipeline, file formats).

### klusters internals
- **[Input system](src/klusters/docs/INPUT_SYSTEM.md)** — ✅ — the keyboard + mouse binding architecture: the `input::` registry, scopes and the tiered scope stack, chords, the capture / focus / repeat policies, the seam, and recipes for adding bindings and modal modes. The engine lives in `libklustersshared` and is **shared with NeuroScope** (see below).
- **[Plugin API](src/klusters/docs/PLUGIN_API.md)** — ✅ — the external plugin descriptor format, parameter dialog, and process runner.
- **[View system](src/klusters/docs/VIEW_SYSTEM.md)** — ✅ — the `BaseFrame` → `BufferedView` → `ViewWidget` frame chain, the view types (cluster, waveform, trace, correlograms, the curation matrices), the `KlustersView` / `DockArea` container, the `setConnections` signal wiring, and a checklist for adding a view.
- **[Document model](src/klusters/docs/DOCUMENT_MODEL.md)** — ✅ — `KlustersDoc` and the `Data` store: cluster / spike / feature storage, the iterator and id allocation, epochs & snapshots, the curation primitives (`prepareUndo`, `applyClusterRename`), the undo/redo stacks, the document→view signals, and file I/O.
- **[Concurrency](src/klusters/docs/CONCURRENCY.md)** — ✅ — the background-job architecture: the two `KlustersJobPool` lanes, the `KlustersJobToken` cancellation model, the snapshot-and-retire worker pattern, supersede-vs-stop, `RequestTicket`, OpenMP, and the serial realign lane.
- **Curation tools & workflows** — 🚧 — how a tool mode is wired end to end: new-cluster, split, watershed, hierarchy (child view), and templates. (Operationally, see the [cluster-curation workflow](doc/workflows/cluster-curation.md); the data-side primitives are in [Document model](src/klusters/docs/DOCUMENT_MODEL.md) §4.)
- **Curation matrices** — 🚧 — the error / template / residual / drift matrices and the shared navigator + click helpers. (Covered structurally in [View system](src/klusters/docs/VIEW_SYSTEM.md) §5 until a dedicated reference exists.)
- **Preferences & persistence** — 🚧 — the generated Preferences pages, QSettings storage, and keymap profiles. (The Preferences ▸ Input page, keymap profiles, and `InputPrefsStore` persistence are covered by the Input system references.)

### neuroscope internals
- **[Input system](src/neuroscope/docs/INPUT_SYSTEM.md)** — ✅ — how NeuroScope adopts the shared `input::` engine: the registration sites, the TraceView mouse decomposition (the seven tool gestures), the `BaseFrame` frame-zoom seam (`managesOwnPrimaryPress`), `InputPrefsStore`/`Configuration` persistence, the bundled Default keymap, and the derived cheat-sheet + active-tool indicator. Read the [Klusters Input system](src/klusters/docs/INPUT_SYSTEM.md) first for the engine itself.

### Core libraries
- **[File formats](src/libneurosuite-core/docs/FILE_FORMATS.md)** — ✅ — `neurofileio`, the Qt-free authoritative reader/writer for every on-disk format (`.dat`/`.res`/`.clu`/`.fet`/`.spk`/`.evt`/`.col` and the `.eap`/`.tcl`/`.wti`/`.wtf`/`.wtl` template-library family), with the exact byte layouts, the per-group chain-of-custody naming, and a consolidated doc-vs-code discrepancy table. Lives in `libneurosuite-core`.
- **libklustersshared** — the shared Qt6 library (`neurosuite-qt`). Its two biggest surfaces have their own references: the `input::` engine (see [Input system](src/klusters/docs/INPUT_SYSTEM.md)) and the `DockArea` container (see [View system](src/klusters/docs/VIEW_SYSTEM.md) §3).

### Other components
- **kiloklustakwik · ndmanager-plugins · ndmanager** — 🚧 — internals references as they are written; component **usage** docs already live in [`doc/`](doc/README.md), and the deep algorithm notes in [`doc/design/`](doc/design/README.md).
- **fiber-kit** — 🚧 — the sibling library (separate repo: `github.com/Gravios/fiber-kit`). It mirrors `neurofileio`'s on-disk contract byte-for-byte via its `neuro_io`; the shared format contract and the one known divergence (`.wtl` v1 vs v2) are in [File formats](src/libneurosuite-core/docs/FILE_FORMATS.md) §5.

### Design notes & task workflows
Deep per-topic technical notes and multi-program walkthroughs live under [`doc/`](doc/README.md); the two indexes below are canonical (the design index is cross-referenced by date from [`CHANGELOG.md`](CHANGELOG.md)).

- **[Design notes](doc/design/README.md)** — ✅ — the design-decision record, one document per substantial change:
  - *Spike detection & re-extraction* — [reextractspikes-v1](doc/design/reextractspikes-v1.md), [reextract-v2](doc/design/reextract-v2.md)
  - *Collision, modeling & priors* — [decomposecollisions](doc/design/decomposecollisions.md), [subtractspikes-botm](doc/design/subtractspikes-botm.md), [modeling-l1-vs-botm](doc/design/modeling-l1-vs-botm.md), [kk-prior](doc/design/kk-prior.md), [substrate-labeling-refactor](doc/design/substrate-labeling-refactor.md), [template-yaml](doc/design/template-yaml.md)
  - *Performance & GPU* — [errormatrix-compute-optimization](doc/design/errormatrix-compute-optimization.md), [realign-gpu-batch](doc/design/realign-gpu-batch.md), [optimization](doc/design/optimization.md)
  - *Apps & tooling* — [neuroscope-raster](doc/design/neuroscope-raster.md), [neuroscope-audit](doc/design/neuroscope-audit.md), [ndm-start-root](doc/design/ndm-start-root.md), [probe-maker](doc/design/probe-maker.md)
- **[Task workflows](doc/workflows/README.md)** — ✅ — recipes that span multiple programs:
  [first-time sort](doc/workflows/first-time-sort.md) · [cluster curation](doc/workflows/cluster-curation.md) · [re-extract with a lower threshold](doc/workflows/re-extract-lower-threshold.md) · [iterative refinement](doc/workflows/iterative-refinement.md) · [empirical priors](doc/workflows/empirical-priors.md) · [drift correction](doc/workflows/drift-correction.md) · [collision decomposition](doc/workflows/collision-decomposition.md)

### Working on the code
- **[Coding & layout standards](STANDARDIZATION.md)** — ✅ — directory layout, changelog, and coding conventions.
- **[Contributing](CONTRIBUTING.md)** — ✅ — coding standards, the patch-series workflow, commit conventions, the changelog/doc layout, and the verification gates a change passes before shipping.
- **Testing** — 🚧 — how to add a ctest suite. (Building and running the existing suites is in [BUILD.md](BUILD.md) §6.)

---

## Maintaining this guide

- **To add a topic:** write the reference next to the code it documents
  (`src/<component>/docs/<topic>.md`) or, if it is project-wide, at the repo root; then
  add a bullet here with a one-line scope and flip its marker to ✅.
- **🚧 entries are not missing work** — they are the structure we agreed to fill in over
  time. Leave one a stub until its reference exists rather than linking a placeholder file.
- **Keep it honest.** The code is the source of truth; update the reference when the code
  changes. A mirror of this guide is kept in the project knowledge base for reading inside
  Claude — keep the two in step when you change either.
