# neurosuite-3 — Developer Guide

The entry point for developers working on the **internals** of neurosuite-3 — the
code-level references you need to modify or extend a component, as distinct from the
user- and pipeline-facing documentation in [`doc/`](doc/README.md).

This page is the **map**: each subsystem gets its own reference document, and the table
of contents below links them together. It is deliberately a skeleton right now — the
filled-in entries are the input system and the plugin API; the rest are stubs we will
expand over time.

Where references live:

- Docs that describe a specific component's code live **next to that code** (e.g.
  [`src/klusters/docs/`](src/klusters/docs/)).
- Broad, project-wide references live at the **repository root** alongside this guide
  ([`ARCHITECTURE.md`](ARCHITECTURE.md), [`STANDARDIZATION.md`](STANDARDIZATION.md),
  [`ROADMAP.md`](ROADMAP.md)).
- User- and pipeline-facing documentation for every component is in [`doc/`](doc/README.md).

The code is always the source of truth; when a reference and the code disagree, fix the
reference.

**Legend:** ✅ written · 🚧 planned (an intentional stub — contribute here)

---

## Table of contents

### Orientation
- **Getting started** — build & run: see [Quick start](README.md#quick-start) in the top-level README and the per-component `install/` guides under [`doc/`](doc/README.md).
- **[Architecture & restructuring plan](ARCHITECTURE.md)** — ✅ — the current layered structure, the maintainability problems being addressed, and the staged migration (project-wide; a working draft).
- **[Component & pipeline docs](doc/README.md)** — ✅ — user- and pipeline-facing documentation for every component (build, usage, the sorting pipeline, file formats).

### klusters internals
- **[Input system](src/klusters/docs/INPUT_SYSTEM.md)** — ✅ — the keyboard + mouse binding architecture: the `input::` registry, scopes and the tiered scope stack, chords, the capture / focus / repeat policies, the seam, and recipes for adding bindings and modal modes.
- **[Plugin API](src/klusters/docs/PLUGIN_API.md)** — ✅ — the external plugin descriptor format, parameter dialog, and process runner.
- **View system** — 🚧 — `BaseFrame` / `ViewWidget`, the view types (cluster, waveform, trace, correlograms, the curation matrices), and how they share state without a common base class.
- **Document model** — 🚧 — `KlustersDoc`: cluster / spike storage, the undo/redo stack, and file I/O.
- **Curation tools & workflows** — 🚧 — how a tool mode is wired end to end: new-cluster, split, watershed, hierarchy (child view), and templates.
- **Curation matrices** — 🚧 — the error / template / residual / drift matrices and the shared navigator + click helpers.
- **Preferences & persistence** — 🚧 — the generated Preferences pages (including Preferences ▸ Input), QSettings storage, and keymap profiles.

### Other components
- **kiloklustakwik · ndmanager-plugins · neuroscope · ndmanager · libklustersshared** — 🚧 — internals references as they are written; component **usage** docs already live in [`doc/`](doc/README.md).
- **fiber-kit** — 🚧 — the sibling library (separate repo: `github.com/Gravios/fiber-kit`) and how it relates to neurosuite-3.

### Working on the code
- **[Coding & layout standards](STANDARDIZATION.md)** — ✅ — directory layout, changelog, and coding conventions.
- **Testing** — 🚧 — the ctest suites and how to add one.
- **Contributing** — 🚧 — patch discipline, commit conventions, and the build / verify workflow.

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
