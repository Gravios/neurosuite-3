# ndm_start as the pipeline root: graph-driven dispatcher

The Pipeline Designer page already has the full mechanics for an
editable node graph (palette, drag-to-place, drag-to-connect,
inspector, presets, topo-sorted Apply→YAML round-trip via
`PipelineDesignerPage::applyRequested` → `ParameterView::setProgramList`).
This change pins **`ndm_start` as the sticky entry node** of every
graph, then rewrites `ndm_start` the bash to read its own program
list and dispatch in graph order.  (The graph itself later moved to
separate pipeline files, which `ndm_start` reads first — see
[Pipeline files](#pipeline-files-sessionndmnpipeline); the in-YAML
form below remains its second source.)

For the operational walkthrough, see
[`../ndmanager/README.md` → Pipeline tab](../ndmanager/README.md#pipeline-tab--editable-node-graph).

**Contents:** [Why](#why) · [C++ — sticky-root semantics in PipelineDesignerPage](#c--sticky-root-semantics-in-pipelinedesignerpage) · [Bash — `ndm_start` as YAML-driven dispatcher](#bash--ndm_start-as-yaml-driven-dispatcher) · [Round-trip behavior](#round-trip-behavior) · [Files touched](#files-touched) · [Limitations & follow-ons](#limitations--follow-ons) · [Pipeline files (`<session>.ndm.<n>.pipeline`)](#pipeline-files-sessionndmnpipeline)

---

## Why

The Pipeline page produces a topologically-sorted ordered list of
plugins on **Apply**. Without a privileged starting node, the
ordering of a disconnected DAG is ambiguous (the topo sort breaks
ties by insertion order, which depends on user click history).
Pinning `ndm_start` as the entry node makes the graph's execution
order canonical: after **Apply** the Plugins tab lists `ndm_start`
first and the rest in graph order, and ndmanager writes the session
file's `programs:` list in that order (`ParameterView::programOrder`;
before that fix the list was written alphabetically from a `QMap`),
so the saved `programs[0]` is `ndm_start`.

Once `ndm_start` is pinned in the GUI, the bash script needs to read
that order back. This makes `ndm_start` a thin abstraction layer:
bash reads the program list from the YAML, classifies each plugin as
per-session or per-directory, and dispatches.

---

## C++ — sticky-root semantics in PipelineDesignerPage

### Script def

`ndm_start` is added as the first entry in `ndmScriptDefs()` with
category `"orchestrator"` and the seven legacy flag parameters as
its `params` (so the inspector still surfaces them when the user
clicks the root). The category is intentionally not in `catOrder`
(the palette-display order list), so the palette doesn't show
`ndm_start` and users can't drag-drop a second instance.

### Sticky-root invariants enforced by the canvas

- **Always present.** `setPrograms()` lays the session's `programs:`
  list out as a chain only when `programs[0]` is `ndm_start` (the
  in-YAML form); otherwise — the usual case, `programs:` being a
  parameter store — the graph is just a synthesised defaults-only root
  (`clearGraphToRoot()`).  **Clear** also calls `clearGraphToRoot()`,
  and `loadPipelineFile()` adds a root to a file that has none (with a
  warning).

- **Always position 0.** `getPrograms()` topo-sorts as before and
  belt-and-braces hoists `ndm_start` to position 0 of the output
  even when the topo sort placed it later.

- **Always exported, even when "disabled".** `getPrograms()` skips
  disabled nodes for everything except `ndm_start` — the
  orchestrator entry is the run, so the concept of disabling it
  doesn't apply.

- **Never deleted.** `PipelineCanvas::deleteSelected` short-circuits
  when the selected node is `ndm_start`. Backspace and Delete keys
  are silently ignored.

- **Indegree 0.** Edge-completion in `mouseReleaseEvent` rejects any
  attempt to connect *into* a node whose type is `ndm_start`. The
  topo sort can therefore never push it later than position 0.

- **No second instance.** `dropEvent()` rejects drops of
  `ndm_start` when one already exists. (The palette doesn't show
  it, but the mime payload is still possible from external sources.)
  `loadPipelineFile()` keeps a file's first `ndm_start` and drops any
  other with its edges, and drops edges into the root, with warnings.

- **Acyclic.** Edge-completion refuses an edge whose target already
  reaches its source (`PipelineCanvas::wouldCreateCycle`), with the
  reason in the page's notice bar; `loadPipelineFile()` drops, in file
  order, each edge that would close a cycle.  `ndm_start`'s sort never
  emits the nodes of a cycle (or anything after them), so the two sorts
  only agree on acyclic graphs.

- **`loadPreset` integration.** Every preset prepends `ndm_start`
  if absent, then chains the rest. Presets stay declarative —
  e.g. Pipeline-D is `{ndm_hipass, ndm_extractspikes_stderiv,
  ndm_alignspikes, ndm_pca, ndm_klustakwik}` — and the canvas prepends
  `ndm_start` automatically.

### Sticky-root visual

- New `"orchestrator"` `CategoryStyle` in `categoryStyles()` with
  gold accent `QColor(0xfa, 0xc1, 0x5c)` over a dark amber base
  `QColor(0x2a, 0x1d, 0x05)`. Visually unique, no other category
  uses gold.

- `drawNode()` adds a small "★ ROOT" pill in the top-right of the
  header stripe when `n.type == kRootType`. Drawn over a
  semi-transparent black backing in the orchestrator accent.

- `drawNode()` skips the input port circle for the root (it can't
  receive edges), and never draws the disabled overlay (the root is
  always active).

- `populateInspector()` hides the **Enabled** checkbox when the
  root node is selected (since the flag is meaningless for it).

The accent color is the only signal added to the *border*
treatment — selection still uses the existing blue border so
`ndm_start` looks consistent with other nodes when chosen.

---

## Bash — `ndm_start` as YAML-driven dispatcher

The new `ndm_start` reads its own program list from the YAML and
dispatches each entry in order. Two execution phases run inside
the directory loop:

```
for directory in $directories; do
    cd $directory

    # PER-SESSION phase:
    #   loops over individual session files inside the directory,
    #   dispatching plugins classified as per-session
    for session in $(list_sessions $directory ${suffixes[*]}); do
        dispatch_graph_per_session
    done

    # PER-DIRECTORY phase:
    #   one pass over plugins classified as per-directory
    dispatch_graph_per_directory

    cd ..
done
```

### Per-session vs per-directory classification

The classification list lives in the bash itself, since it isn't
visible in the YAML or to the GUI:

```bash
PER_SESSION_PLUGINS=(
    ndm_ncs2dat
    ndm_smr2dat
    ndm_aom2dat
    ndm_smr2evt
    ndm_nev2evt
    ndm_extractleds
    ndm_transcodevideo
    ndm_extractchannels
    ndm_mergedat
    ndm_resample
)
```

These match the plugins the legacy `do_sessions` block invoked.
Everything else (concatenation, hipass, spike detection, PCA,
KiloKlustaKwik, post-sort, LFP, drift estimation, decompose collisions,
strip-dat, clean) is per-directory.

The classification is bash-internal because:

1. It maps to existing pipeline semantics that already worked.
2. The user shouldn't need to know about it — they just connect
   nodes in the graph.
3. Adding a new ndm_* plugin requires deciding which phase it
   belongs to anyway; the developer who writes the plugin updates
   the list at the same time.

### Backward compatibility

When `programs[0] != ndm_start`, the script falls through to the
legacy hard-coded dispatch path. This means existing session YAMLs
(those produced before the graph design landed) continue to work
without change — the bash reads the seven flags from
`programs[ndm_start].parameters`, and if no `ndm_start` block
exists at all, `read_script_parameter` returns empty so the
defaults take effect just as before.

The branch decision is made once at the top of the script:

```bash
if graph_has_root "$template"; then
    USE_GRAPH=true
    GRAPH_PLUGINS=( ... )    # populated from read_program_list
fi
```

Inside the directory loop, `$USE_GRAPH` selects which dispatcher
runs.

### YAML reader

Two helpers do the work:

- `graph_has_root` — exit 0 when `programs[0].name == 'ndm_start'`,
  exit 1 otherwise. Used as a guard.

- `read_program_list` — emits every program name except `ndm_start`,
  one per line, in YAML order. Used to populate `GRAPH_PLUGINS`.

Both helpers handle YAML and legacy XML; the YAML branch uses
`yaml.safe_load`, the XML branch uses `xml.etree.ElementTree` and
also accepts the legacy `<n>` element name in addition to `<name>`
(matching the existing `yaml_read_key` shim in `ndm_functions`).

### Error handling

Per-plugin failure is **logged but doesn't abort the pipeline**:

```bash
"$plugin" $directory
step_completed "$plugin" || {
    echo_warning "'$plugin' failed for '$directory'; continuing"
}
```

This matches user expectation — a failed `ndm_pca` shouldn't
prevent `ndm_lfp` from running. Plugins reporting `OUTPUT_EXISTS`
or `INPUTS_MISSING` exit codes are treated as non-failures by
`step_completed`.

If a plugin name in the graph isn't on PATH (typo, deleted plugin,
external script not installed), a warning is emitted and execution
continues:

```bash
if ! command -v "$plugin" >/dev/null 2>&1; then
    echo_warning "plugin '$plugin' not found on PATH; skipping"
    continue
fi
```

---

## Round-trip behavior

| Action | Result |
|---|---|
| Open a session YAML with `programs[0] == ndm_start` | Graph loads as a chain in YAML order, root first |
| Open a session YAML whose `programs[0]` is anything else, or with no `programs:` | Graph shows just a defaults-only `ndm_start` |
| …and `<session>.ndm.default.pipeline` exists | That file replaces the graph; if it fails to load, a red notice says so and Save Pipeline asks before overwriting it |
| Edit graph (add/remove/connect/move, inspector edit, Clear) | Apply is highlighted; the document counts as modified until Save Pipeline |
| Click **Apply Pipeline** | `getPrograms()` topo-sorts → `applyRequested` → `setProgramList` reorders the Plugins tab (existing pages kept, inspector edits applied, new plugins added, others kept after) |
| Save document (Ctrl+S) | `programs:` written in Plugins-tab order, `ndm_start` first after Apply; the pipeline file is *not* written |
| Run `ndm_start session.yaml` in the session file's directory | Bash takes the pipeline file if present, else the in-YAML list, and dispatches per-session and per-directory phases |

---

## Files touched

```
src/ndmanager/src/pipelinedesignerpage.cpp     (sticky-root: script def,
                                                 category style, helpers,
                                                 setPrograms/getPrograms,
                                                 loadPreset, deleteSelected,
                                                 dropEvent, edge-completion,
                                                 drawNode, populateInspector;
                                                 +146 lines net)
src/ndmanager-plugins/scripts/ndm_start         (rewritten as YAML-driven
                                                 dispatcher with legacy
                                                 fallback; +197 lines net)
doc/ndmanager/README.md                         (Pipeline tab: sticky root
                                                 + graph-vs-legacy mode)
doc/design/ndm-start-root.md                    (this file)
CHANGELOG.md                                    (dated entry)
```

## Limitations & follow-ons

- **Per-session vs per-directory classification is hard-coded.** A
  plugin's phase is bash-internal. To make it user-configurable
  would require a new field in `NdmScriptDef` (e.g. `phase: "session"
  | "directory"`) and a YAML serialization of it. Not a hard
  change but not worth doing speculatively — every existing
  ndm_* plugin already has a clear phase.

- **No conditional branches.** Every plugin in the graph runs
  unconditionally (modulo `inputs_missing` checks inside each
  plugin). The seven legacy flags survive only as fallback; the
  graph itself doesn't surface them. If you want
  conditional/optional steps, mark a node disabled in the inspector
  — `getPrograms()` filters disabled nodes from the export
  (except for `ndm_start` itself).

- **No parallel branches.** Topo sort emits a single linear order
  even when the graph has fan-out. Plugins themselves are
  internally parallel (OpenMP, CUDA), so this rarely matters in
  practice, but if you need explicit `&` backgrounding between
  independent sub-pipelines, do it as a separate ndm_start
  invocation rather than within one graph.

---

## Pipeline files (`<session>.ndm.<n>.pipeline`)

The graph itself doesn't live in the session YAML's `programs:` block.
It lives in a separate `<session>.ndm.<n>.pipeline` YAML file alongside
the session, with a minimal schema:

```yaml
nodes:
  - id: n1
    type: ndm_start
    pos: [60, 80]
    enabled: true
    params:
      wideband: 'true'
      events: 'true'
      ...
  - id: n2
    type: ndm_hipass
    pos: [320, 80]
    enabled: true
    params:
      windowHalfLength: '16'
      chunkSize: '134217728'
edges:
  - {from: n1, to: n2}
```

Values are written single-quoted when every character is printable on
one line and double-quoted with escapes (`\n`, `\t`, `\xHH`, `\uHHHH`, …)
otherwise, so any string reads back exactly; ids, types and parameter
names are written bare when they are plain identifiers.  The reader is a
small PyYAML program run with `python3` (`kPipelineParser` in
`pipelinedesignerpage.cpp`) that prints the graph as JSON; it reads a
parameter value as its YAML text (`0.50` stays `0.50`, `~` becomes
empty) and `enabled`, ids and edges the way `ndm_start`'s
`read_pipeline_file` does.

Node parameters in the file are the designer's copy only: at run time
each plugin reads its parameters from its entry in the session file's
`programs:` block.

### Why a separate file?

- **Independence from session schema.** Pipeline editing doesn't dirty
  the session YAML — saving a pipeline doesn't trigger session
  re-validation, doesn't show up in `git diff session.yaml`, doesn't
  conflict with parameter overrides recorded by individual plugin runs.
- **Named variants.** A user can keep a `default`, an `experimental`,
  and a `best` pipeline alongside one another and switch between them
  without copying YAMLs. The Save As menu writes
  `<session>.ndm.<n>.pipeline`; Load Pipeline lets the user pick.
- **CI friendliness.** A workflow runner can override which pipeline to
  use via `NDM_PIPELINE=experimental ndm_start session.yaml` (run in the
  session file's directory) without modifying the session.
- **Namespace cleanliness.** The `.ndm.` infix is intentional. Other
  tools in the same session directory might also produce `.pipeline`
  files (Klusters export, snakemake workflow descriptions, nextflow
  metadata). The infix prevents collisions.

### Naming

```
<session>.ndm.<n>.pipeline

  session-name.ndm.default.pipeline       ← auto-loaded on document open
  session-name.ndm.best.pipeline          ← named variant via Save As
  session-name.ndm.experimental.pipeline
  session-name.ndm.pipeline-A.pipeline
```

Sanitisation rule for `<n>`: lowercased, whitespace collapsed to
underscore, only `[a-z0-9_-]` retained. The empty result is rejected
with a re-prompt.

### Load order on document open

`ParameterView::initialize` calls `pipelineDesigner->setPrograms(programList)`
to seed the empty graph + root, then opportunistically calls
`loadPipelineFile("<session>.ndm.default.pipeline")` if it exists.
File-exists is the only signal — every session starts without a
pipeline file the first time.  A failed load is logged (`qWarning`)
and shown in the Pipeline tab's notice bar, and the path is remembered
(`failedLoadPath()`) so Save Pipeline asks before overwriting it; load
repairs (dropped unknown types, renamed duplicate ids, a second root,
edges into the root or closing a cycle) are shown there too.  Loading
keeps the file's node order, keeps its ids and moves the id counter
past them (`reserveIds()`; `allocateId()` also skips any id in use).

### Save / Save As / Load actions

| Action | Where | Shortcut | Behavior |
|---|---|---|---|
| Save Pipeline | File menu, page toolbar | Ctrl+Alt+P | Overwrite `<session>.ndm.default.pipeline` (asks first if that file failed to load) |
| Save Pipeline As… | File menu, page toolbar | Ctrl+Alt+Shift+P | Prompt for `<n>`, write `<session>.ndm.<n>.pipeline`, confirm overwrite if file exists |
| Load Pipeline… | File menu | — | File dialog filtered to `*.pipeline` beside the session directory |

Both saves require a document backed by its own session file
(`ndManagerDoc::isSaved()`): a New / Untitled document or one started
from a template would otherwise write `./Untitled.ndm.default.pipeline`
or a file beside the template, so they refuse with "Save the session
file first".  A successful save clears the designer's unsaved flag,
which `ParameterView::isModified()` includes; saving the session file
does not write the pipeline file and leaves the flag set.

### Apply Pipeline vs Save Pipeline

These are two separate operations with different semantics:

- **Apply Pipeline** (existing button) → `ParameterView::setProgramList`
  orders the Plugins tab (`programOrder`) as the graph runs.  It keeps
  each existing plugin page (values, help, script) and writes onto it
  only the parameters edited in the inspector since the last Apply; adds
  pages, with the node's values, for plugins new to the tab; maps
  several nodes of one type to one entry; and keeps plugins not in the
  graph (or only on disabled nodes) after the graph's plugins, since
  their entries hold the overrides those plugins read.  The next
  document save (Ctrl+S) writes the list to the session YAML's
  `programs:` block in that order.

- **Save Pipeline / Save As** (new) → writes the graph itself
  (positions, edges, params) to a `.pipeline` file. Doesn't touch
  the session YAML, doesn't change anything in the Plugins tab. Used
  for keeping the graph as a reusable artefact.

A typical workflow: design a graph, **Save Pipeline** it as `default`,
then run `ndm_start session.yaml` in the session file's directory and
the dispatcher reads `session.ndm.default.pipeline` directly.
**Apply Pipeline** (then Ctrl+S) is needed to carry inspector
parameter edits into the session file, which is where plugins read
their parameters, and to give the session file the in-YAML graph form.

### Bash dispatch priority

The new `ndm_start` chooses its source of plugin order in this order:

1. `<template>.ndm.${NDM_PIPELINE:-default}.pipeline` — pipeline file
2. In-YAML graph (`programs[0]==ndm_start`) — legacy form, kept for
   backward compatibility with the earlier in-YAML design
3. Hard-coded flag-driven `do_*` sequence — original ndm_start, kept
   for sessions with no graph at all

`ndm_start [parameter_file] [directory ...]` works in the current
directory: the argument is reduced to its stem (directory part and
extension dropped), `<stem>.yaml|.yml|.xml` must exist there (no
argument: the stem is the current directory's name), and the pipeline
file is `<stem>.ndm.${NDM_PIPELINE:-default}.pipeline` in that same
directory — exactly where ndmanager's Save Pipeline puts it for that
parameter file.  The `NDM_PIPELINE` environment variable lets a CI run
select between named variants without editing files:

```sh
cd /data/Rat
NDM_PIPELINE=best ndm_start Rat.yaml           # uses Rat.ndm.best.pipeline
NDM_PIPELINE=experimental ndm_start Rat.yaml   # uses Rat.ndm.experimental.pipeline
ndm_start Rat.yaml                             # uses Rat.ndm.default.pipeline
```

When the default file doesn't exist, ndm_start falls through to
priority 2/3 without a message; when a file named with `NDM_PIPELINE`
doesn't exist it stops with an error, so a mistyped name never runs a
different pipeline; when the file exists but yields no plugins
(unparsable, or only `ndm_start`), it warns and falls through.

### Topological sort in the bash

The `read_pipeline_file` helper does a Kahn's-algorithm topological
sort in Python: indegree-0 nodes go on the queue, each emit decrements
successor indegrees, ties break by YAML insertion order. The GUI's
`topoSort` (used by `getPrograms` for Apply) does the same, seeding its
queue in node order — which for a loaded pipeline is the file order,
since loading no longer moves the root — so the runtime order is
identical to what the user sees.  (It used to seed in `QMap` id order,
`n10` before `n2`.)  Nodes on a cycle are never emitted by the bash
sort; the GUI keeps graphs acyclic (see the sticky-root invariants).
