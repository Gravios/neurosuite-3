# ndm_start as the pipeline root: graph-driven dispatcher

The Pipeline Designer page already has the full mechanics for an
editable node graph (palette, drag-to-place, drag-to-connect,
inspector, presets, topo-sorted Apply→YAML round-trip via
`PipelineDesignerPage::applyRequested` → `ParameterView::setProgramList`).
This change pins **`ndm_start` as the sticky entry node** of every
graph, then rewrites `ndm_start` the bash to read its own program
list from the YAML and dispatch in graph order.

For the operational walkthrough, see
[`../ndmanager/README.md#pipeline-tab`](../ndmanager/README.md#pipeline-tab).

---

## Why

The Pipeline page produces a topologically-sorted ordered list of
plugins on **Apply**. Without a privileged starting node, the
ordering of a disconnected DAG is ambiguous (the topo sort breaks
ties by insertion order, which depends on user click history).
Pinning `ndm_start` as the entry node makes the graph's execution
order canonical: the YAML's `programs[0]` is always `ndm_start`, and
the rest follow in graph order.

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

- **Always present.** `setPrograms()` hoists an existing
  `ndm_start` to position 0; if the loaded YAML has no `ndm_start`
  entry, a defaults-only `ProgramInformation` is synthesised and
  prepended before laying out.

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

- **`loadPreset` integration.** Every preset prepends `ndm_start`
  if absent, then chains the rest. Presets stay declarative —
  e.g. Pipeline-D is `{ndm_extractspikes_stderiv, ndm_alignspikes,
  ndm_pca_stderiv, ndm_klustakwik}` — and the canvas prepends
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

The accent colour is the only signal added to the *border*
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
KlustaKwik, post-sort, LFP, drift estimation, decompose collisions,
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

## Round-trip behaviour

| Action | Result |
|---|---|
| Open a session YAML with `programs[0] == ndm_start` | Graph loads with root pinned at position 0; rest of nodes laid out in YAML order |
| Open a session YAML without `ndm_start` | Graph synthesises a defaults-only `ndm_start` and prepends it; rest laid out in YAML order |
| Open a session YAML with no `programs:` | Graph shows just `ndm_start` |
| Edit graph (add/remove/connect/move) | `m_modified` flips true; **Apply** button enables |
| Click **Apply Pipeline** | `getPrograms()` topo-sorts → `applyRequested` → `setProgramList` rewrites `programs:` block |
| Save document (Ctrl+S) | `programs:` written with `ndm_start` at position 0 |
| Run `ndm_start session.yaml` | Bash detects root, reads remaining programs, dispatches per-session and per-directory phases |

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
  | "directory"`) and a YAML serialisation of it. Not a hard
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
