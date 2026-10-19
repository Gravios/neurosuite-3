# neurosuite-3 — Developer Guide

The entry point for developers working on the **internals** of neurosuite-3 — the
code-level references you need to modify or extend a component, as distinct from the
user- and pipeline-facing documentation in [`doc/`](../README.md).

This page is the **map**: each subsystem gets its own reference document, and the table
of contents below links them together. Many subsystems now have a full reference; the
entries still marked 🚧 are intentional stubs to be filled in over time.

Where references live:

- Docs that describe a specific component's code live **next to that code** (e.g.
  [`src/klusters/docs/`](../../src/klusters/docs/)).
- Broad, project-wide references live in **`doc/dev/`** alongside this guide, or in `doc/`
  ([`ARCHITECTURE.md`](ARCHITECTURE.md), [`STANDARDIZATION.md`](STANDARDIZATION.md),
  [`BUILD.md`](../BUILD.md), [`GLOSSARY.md`](../GLOSSARY.md), [`ROADMAP.md`](ROADMAP.md)).
- User- and pipeline-facing documentation for every component is in [`doc/`](../README.md).

The code is always the source of truth; when a reference and the code disagree, fix the
reference.

**Legend:** ✅ written · 🚧 planned (an intentional stub — contribute here)

---

## Table of contents

### Orientation
- **[Building neurosuite-3](../BUILD.md)** — ✅ — the developer build reference: every CMake option, the GPU backends, the ctest suite, the AddressSanitizer build, and CI parity. (For a first build, start with [Installing neurosuite-3](../INSTALL.md).)
- **[Glossary](../GLOSSARY.md)** — ✅ — component names, domain terms, on-disk formats, and architecture concepts in one place.
- **[Architecture & restructuring plan](ARCHITECTURE.md)** — ✅ — the current layered structure, the maintainability problems being addressed, and the staged migration (project-wide; a working draft).
- **[Component & pipeline docs](../README.md)** — ✅ — user- and pipeline-facing documentation for every component (build, usage, the sorting pipeline, file formats).

### klusters internals
- **[Input system](../../src/klusters/docs/INPUT_SYSTEM.md)** — ✅ — the keyboard + mouse binding architecture: the `input::` registry, scopes and the tiered scope stack, chords, the capture / focus / repeat policies, the seam, and recipes for adding bindings and modal modes. The engine lives in `libklustersshared` and is **shared with NeuroScope** (see below).
- **[Plugin API](../../src/klusters/docs/PLUGIN_API.md)** — ✅ — the external plugin descriptor format, parameter dialog, and process runner.
- **[View system](../../src/klusters/docs/VIEW_SYSTEM.md)** — ✅ — the `BaseFrame` → `BufferedView` → `ViewWidget` frame chain, the view types (cluster, waveform, trace, correlograms, the curation matrices), the `KlustersView` / `DockArea` container, the `setConnections` signal wiring, and a checklist for adding a view.
- **[Document model](../../src/klusters/docs/DOCUMENT_MODEL.md)** — ✅ — `KlustersDoc` and the `Data` store: cluster / spike / feature storage, the iterator and id allocation, epochs & snapshots, the curation primitives (`prepareUndo`, `applyClusterRename`), the undo/redo stacks, the document→view signals, and file I/O.
- **[Concurrency](../../src/klusters/docs/CONCURRENCY.md)** — ✅ — the background-job architecture: the two `KlustersJobPool` lanes, the `KlustersJobToken` cancellation model, the snapshot-and-retire worker pattern, supersede-vs-stop, `RequestTicket`, OpenMP, and the serial realign lane.
- **Curation tools & workflows** — 🚧 — how a tool mode is wired end to end: new-cluster, split, watershed, hierarchy (child view), and templates. (Operationally, see the [cluster-curation workflow](../workflows/cluster-curation.md); the data-side primitives are in [Document model](../../src/klusters/docs/DOCUMENT_MODEL.md) §4.)
- **Curation matrices** — 🚧 — the error / template / residual / drift matrices and the shared navigator + click helpers. (Covered structurally in [View system](../../src/klusters/docs/VIEW_SYSTEM.md) §5 until a dedicated reference exists.)
- **Preferences & persistence** — 🚧 — the generated Preferences pages, QSettings storage, and keymap profiles. (The page framework and how to add a page are in the [libklustersshared reference](../../src/libklustersshared/docs/LIBRARY.md); the Preferences ▸ Input page, keymap profiles, and `InputPrefsStore` persistence are covered by the Input system references.)

### neuroscope internals
- **[Input system](../../src/neuroscope/docs/INPUT_SYSTEM.md)** — ✅ — how NeuroScope adopts the shared `input::` engine: the registration sites, the TraceView mouse decomposition (the seven tool gestures), the `BaseFrame` frame-zoom seam (`managesOwnPrimaryPress`), `InputPrefsStore`/`Configuration` persistence, the bundled Default keymap, and the derived cheat-sheet + active-tool indicator. Read the [Klusters Input system](../../src/klusters/docs/INPUT_SYSTEM.md) first for the engine itself.

### Core libraries
- **[libneurosuite-core API](../../src/libneurosuite-core/docs/CORE_API.md)** — ✅ — the Qt-free core (`Neurosuite::core`): how to link and include it, the shared conventions (waveform layouts, units, rounding), one section per header — the file layer and naming policy (summarised; detail in File formats below), spike extraction, the stderiv transform, PCA projection, projection scope, running statistics, collision decomposition (`decollide`, `decollide_eap`), drift partitioning, template generation and template classes — with each header's consumers and twin implementations, the tests and the cross-language conformance vectors, and known issues.
- **[File formats](../../src/libneurosuite-core/docs/FILE_FORMATS.md)** — ✅ — `neurofileio`, the Qt-free authoritative reader/writer for every on-disk format (`.dat`/`.res`/`.clu`/`.fet`/`.spk`/`.evt`/`.col` and the `.eap`/`.tcl`/`.wti`/`.wtf`/`.wtl` template-library family), with the exact byte layouts, the per-group chain-of-custody naming, and a consolidated doc-vs-code discrepancy table. Lives in `libneurosuite-core`.
- **[libklustersshared library reference](../../src/libklustersshared/docs/LIBRARY.md)** — ✅ — the shared Qt6 library (`Neurosuite::qt`, target `neurosuite-qt`): CMake package and build options, the YAML parameter layer (detail in the [YAML reference](../libklustersshared/README.md)), the `input::` engine (detail in [Input system](../../src/klusters/docs/INPUT_SYSTEM.md)), `DockArea` (detail in [View system](../../src/klusters/docs/VIEW_SYSTEM.md) §3), the `xcorr` realign engine and its OpenMP / CUDA / HIP / SYCL back ends, the paged preferences-dialog framework with a recipe for adding a page, the widgets and utilities (incl. the `sdiffPairs` grammar), the tests, and known issues.

### Other components
- **kiloklustakwik · ndmanager-plugins · ndmanager** — 🚧 — internals references as they are written; component **usage** docs already live in [`doc/`](../README.md), and the deep algorithm notes in [`doc/design/`](../design/README.md).
- **fiber-kit** — 🚧 — the sibling library (separate repo: `github.com/Gravios/fiber-kit`). It mirrors `neurofileio`'s on-disk contract byte-for-byte via its `neuro_io`; fiber-kit reads both `.wtl` versions but writes v1 (spike lists), which ns3 reads as legacy stubs; the shared format contract is in [File formats](../../src/libneurosuite-core/docs/FILE_FORMATS.md) §5.

### Design notes & task workflows
Deep per-topic technical notes and multi-program walkthroughs live under [`doc/`](../README.md); the two indexes below are canonical (the design index is cross-referenced by date from [`CHANGELOG.md`](../../CHANGELOG.md)).

- **[Design notes](../design/README.md)** — ✅ — the design-decision record, one document per substantial change:
  - *Spike detection & re-extraction* — [reextractspikes-v1](../design/reextractspikes-v1.md), [reextract-v2](../design/reextract-v2.md)
  - *Collision, modeling & priors* — [decomposecollisions](../design/decomposecollisions.md), [subtractspikes-botm](../design/subtractspikes-botm.md), [modeling-l1-vs-botm](../design/modeling-l1-vs-botm.md), [kk-prior](../design/kk-prior.md), [substrate-labeling-refactor](../design/substrate-labeling-refactor.md), [template-yaml](../design/template-yaml.md)
  - *Performance & GPU* — [errormatrix-compute-optimization](../design/errormatrix-compute-optimization.md), [realign-gpu-batch](../design/realign-gpu-batch.md), [optimization](../design/optimization.md)
  - *Apps & tooling* — [neuroscope-raster](../design/neuroscope-raster.md), [neuroscope-audit](../design/neuroscope-audit.md), [ndm-start-root](../design/ndm-start-root.md), [probe-maker](../design/probe-maker.md)
- **[Task workflows](../workflows/README.md)** — ✅ — recipes that span multiple programs:
  [first-time sort](../workflows/first-time-sort.md) · [cluster curation](../workflows/cluster-curation.md) · [re-extract with a lower threshold](../workflows/re-extract-lower-threshold.md) · [iterative refinement](../workflows/iterative-refinement.md) · [empirical priors](../workflows/empirical-priors.md) · [drift correction](../workflows/drift-correction.md) · [collision decomposition](../workflows/collision-decomposition.md)

### Working on the code
- **[Coding & layout standards](STANDARDIZATION.md)** — ✅ — directory layout, changelog, and coding conventions.
- **[Recipes & canonical references](RECIPES.md)** — ✅ — the one page that specifies each format, rule and schema, and step-by-step recipes (every file to touch, and the test that proves it) for adding a vector-table row, a PCAE method, a session-YAML section or a plugin step.
- **[Contributing](../../.github/CONTRIBUTING.md)** — ✅ — coding standards, the patch-series workflow, commit conventions, the changelog/doc layout, and the verification gates a change passes before shipping.
- **[Testing](../../tests/README.md)** — ✅ — running the ctest suite (all of it, one part, or without Qt), the fiber-kit contract, the build-system invariants, and where each subproject adds a test.

---

## Maintaining this guide

- **To add a topic:** write the reference next to the code it documents
  (`src/<component>/docs/<topic>.md`) or, if it is project-wide, in `doc/dev/`; then
  add a bullet here with a one-line scope and flip its marker to ✅.
- **🚧 entries are not missing work** — they are the structure we agreed to fill in over
  time. Leave one a stub until its reference exists rather than linking a placeholder file.
- **Keep it honest.** The code is the source of truth; update the reference when the code
  changes. A mirror of this guide is kept in the project knowledge base for reading inside
  Claude — keep the two in step when you change either.
