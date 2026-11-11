# Klusters Plugin / Extension API

Status: **phases 1–3 implemented** (discovery + typed dialog + runner + `none` /
`hierarchy-reload` / `decollide-apply` integration, and `recluster-integrate` through the
**sub-session** — the temp-file method, §3b); Preferences → Plugins (§3c) holds each
plugin's parameter defaults.  See §6.
Formalizes how external engines (recluster backends, re-fibering tools, analyses,
exporters) are registered with and invoked from Klusters, modeled on the existing
**ndmanager-plugins** mechanism already in this repository (`src/ndmanager-plugins`,
parsed by `src/ndmanager`).

The goal is that adding a new engine to Klusters requires **no Klusters code
change** — only a descriptor file and a program on `PATH`, exactly as adding an
ndManager step requires only a descriptor + a `process_*` binary.

---

## 1. Reuse, don't reinvent

Three things already exist in-tree and are reused verbatim:

| Concern | Existing component | Reused as |
| --- | --- | --- |
| Descriptor format + parse | `ndmanager/descriptionyamlreader.{h,cpp}` → `ProgramInformation` (name, parameters[name/value/status], help) | the plugin descriptor parser |
| Descriptor schema | `ndmanager-plugins/descriptions/*.xml` + `description.xsd` | the plugin descriptor schema, extended (§3) |
| External-process run + live log | `klusters/processwidget.{h,cpp}` (`QProcess childproc`, `finished(int,ExitStatus)`) | the plugin runner surface |

`DescriptionYamlReader` should move from `ndmanager` to `libneurosuite-core`
(already a shared lib) so both ndManager and Klusters link one copy. No format
change — an existing `ndm_*.xml` parses unmodified.

---

## 2. What a plugin is

A plugin is a **descriptor** (`*.xml` or `*.yaml`, ndManager `<program>` schema)
plus an **executable** of the same name on `PATH`. The descriptor declares the
parameters Klusters renders as a form; the executable does the work and writes
files Klusters knows how to reload.

Discovery (as implemented in `pluginregistry.cpp`): the registry globs **`*.xml`
only** — `.yaml` descriptors are not currently scanned — across these directories,
and **a later directory wins on a name collision**:

1. `<AppDataLocation>/plugins/descriptions/` for each of Qt's
   `QStandardPaths::AppDataLocation` directories (per-user first). Klusters sets
   the organization name `neurosuite-3` and the application name `klusters`
   (`main.cpp`, through `neurosuite::initApplicationIdentity`), so on Linux the
   per-user directory is `~/.local/share/neurosuite-3/klusters/plugins/descriptions/`.
   (Before 2026-10-29 it was under `~/.local/share/sourceforge/klusters/`; the
   first start of a newer Klusters copies that folder over.)
2. `<GenericDataLocation>/klusters/plugins/descriptions/` — the install share
   (`…/share/klusters/plugins/descriptions/`) on most layouts — then the
   `<bindir>/../share/klusters/plugins/descriptions/` fallback
3. each path in `$KLUSTERS_PLUGIN_PATH` (separated by `:`, or `;` on Windows)

Because later wins, the install share currently overrides a same-named user
descriptor, and `$KLUSTERS_PLUGIN_PATH` overrides both — i.e. the opposite of the
"user overrides system" order this section originally specified; revisit the dir
order in `pluginregistry.cpp` if user-override is the intent.

---

## 3. Descriptor schema (ndManager `<program>` + a `<klusters>` block)

The `<program>`/`<parameters>`/`<help>` block is **unchanged** from
ndmanager-plugins. Klusters adds one optional `<klusters>` block that declares the
plugin's **kind** and its **context contract** — what Klusters injects as
arguments and what it does with the result:

```xml
<?xml version='1.0'?>
<parameter>
 <program>
  <n>fiber-refiber</n>
  <parameters>
   <parameter><n>min-cos</n><value>0.85</value><status>Optional</status></parameter>
   <parameter><n>dy-um</n><value>6</value><status>Optional</status></parameter>
   <parameter><n>clique</n><value>0</value><status>Optional</status></parameter>
  </parameters>
  <help>Group the microfiber atom layer (.clc) into fibers ...</help>
 </program>
 <klusters>
  <kind>refiber</kind>                 <!-- recluster | refiber | analysis | export -->
  <consumes>base group variant tag</consumes>
  <produces>triple</produces>          <!-- clu | triple | report | none -->
  <integration>hierarchy-reload</integration>
  <selection>none</selection>          <!-- none | clusters | children -->
 </klusters>
</parameter>
```

A descriptor must be **well-formed XML**: a `--` inside a comment, an unescaped `<` or `&` in a help
text, an unclosed element -- the file does not load.  It is not dropped silently: the *Plugins* menu
lists it disabled as `<file> — not loaded` (the parser's message with its line and column in the
tooltip and the status bar), *Preferences → Plugins* lists the broken descriptors with the same message,
*Reload Plugins* reports them, and the console gets `[plugins] <path> not loaded: ...`
(`PluginRegistry::broken()`, `KlustersPlugin::error`).

### Parameter types (optional, under `<parameter>`)

A parameter may declare how it is edited; Klusters infers it from the default otherwise:

| element | meaning |
| --- | --- |
| `<type>` | `bool` (a check box; passed as the bare `--name` flag when true, nothing when false), `int` / `real` (spin boxes; `<min>`, `<max>`, `<step>` bound them; a real's decimals follow its default, "0.125" → 3), `path` (a line with Browse…; a name ending in `dir`/`directory`/`folder` browses for a directory), `choice` (a combo box over `<choices>`), `text` |
| `<choices>` | `a|b|c` — an empty entry (`|base|exp`) is "unset" (the parameter is omitted from the argv) |
| `<help>` | shown as the editor's tooltip |

Inference without `<type>`: `true`/`false`/`on`/`off`/`yes`/`no` → bool; an integer literal → int; a real
literal → real; a name ending in `path`/`file`/`dir`… or a value with a slash and a dot → path; else
text.  `PluginParameter::effectiveType()` is the single implementation (tested in
`klusters_test_pluginregistry`).

### `<kind>` taxonomy

| kind | meaning | typical produces / integration |
| --- | --- | --- |
| `recluster` | re-partition selected clusters' spikes | `clu` / `recluster-integrate` |
| `refiber` | (re)group the atom layer into fibers | `triple` / `hierarchy-reload` |
| `analysis` | compute a report next to the session | `report` / `none` (open result) |
| `export` | write a foreign format | `none` |

### Context contract (`<consumes>`)

Klusters resolves these tokens from the open document and prepends them to the
program invocation **before** the descriptor parameters, so engines need no
knowledge of Klusters internals:

| token | value Klusters injects |
| --- | --- |
| `base` | session base path (the `<base>` of the open `.clu`) |
| `group` | electrode group id of the active view |
| `variant` | method/variant token of the open clustering (e.g. `stderiv`) |
| `tag` | stage tag of the open clustering, if any |
| `selection` | space-separated selected cluster ids (when `<selection>clusters</selection>`) |
| `children` | selected child ids (when `<selection>children</selection>`) |

So the actual argv is: `program  <resolved consumes...>  --param value ...`,
which is exactly how `fiber-refiber <base> <group> --min-cos 0.85` is already
shaped — the CLI and the descriptor are the same contract.

**`<command>` template (alternative to `<consumes>`).**  An engine whose CLI is not
"program, positional context, then options" declares the invocation as a template;
the first word is the program and the rest is substituted:

```xml
<command>fk run %group --session %base --final-clu %final %params</command>
```

| token | value |
| --- | --- |
| `%base` `%group` `%variant` `%tag` `%nsamp` `%nchan` `%dir` | the context (as the `<consumes>` tokens); for a sub-session plugin `%base` is the SUB-SESSION's base name |
| `%ticks` | samples per waveform of the space SHOWN (`%nsamp` × its super-sampling factor; `%nsamp` itself is always the recording's) |
| `%spk` `%supersample` | the waveform space SHOWN: its `.spk`'s method token (`standard`, `stderiv_C5`, `standard_U4` in super-sample mode) and its super-sampling factor (1 when none), so an engine that fits waveforms follows the space Klusters is in (fiber-kit-manifold's `FK_MANIFOLD_SPK` defaults to `%spk`) |
| `%final` | sub-session plugins: the `.clu` Klusters reads back (`<sub>.clu.klusters.<group>`) |
| `%selection` / `%children` | the selected ids, one argument each |
| `%params` | where the `--name value` parameters go (appended when the template has none) |

A `%token` inside a parameter VALUE is substituted too, so a descriptor default of `%variant` means
"the open clustering's variant" (fiber-kit's `method` parameter).

### `<input>`: session or sub-session (§3b)

| value | the engine runs on |
| --- | --- |
| `session` (default) | the session files on disk (`%base` = the session base); the document must be saved |
| `subsession` (default for kind `recluster`) | the selected clusters' (or atoms') spikes exported as a session of their own — see §3b |

### Result integration (`<integration>`)

| value | Klusters action on success |
| --- | --- |
| `recluster-integrate` | the engine ran on the sub-session (§3b); its `.clu` named by `%final` is fed through the existing recluster integration (`integrateReclusteredClusters` / `reclusteringUpdate`): one undo step, in the layer the selection came from; a `.clc` beside it installs the atoms in a hierarchical session |
| `refine` | a sub-session engine that REFINES the selected unit(s) into children and never moves a spike between units (fiber-kit's `fiber-kit-manifold`).  Klusters runs it on the ATOM layer whatever the scope: from the parent scope the selected parents become their atoms (`childrenOf`); the seed `.clu` carries the atoms' PARENT ids (the engine sees each unit whole) and a seed `.clc`/`.clp` beside it the atoms (the engine keeps the ones it does not examine); the engine's final `.clc` (delivered beside `%final`) is staged as the `.clu` the atom-layer recluster integrates (`KlustersDoc::stageRefinedChildren`), so the children land under the parents they belong to, one child-layer undo step, the parents' ids untouched.  The wheel's *Recluster ▸ Plugin* ring carries it as **Refine** (`recluster.engine.manifold`, *Actions ▸ Recluster ▸ Engine ▸ Refine: fiber-kit manifold*; enabled once the descriptor is installed) |
| `hierarchy-reload` | reload the `.clu/.clc/.clp` triple (the detection + `buildHierarchyMaps` path), refreshing both palettes |
| `decollide-apply` | apply the collision sidecar (`<base>.col.<method>.<group>[.<tag>]`) the engine wrote, through the same code as the lasso decollide |
| `none` | just report exit status; offer to open any produced file |

`hierarchy-reload` is the hook the re-fibering workflow needs: `fiber-refiber`
writes the triple, Klusters reloads it, the atoms reappear as children of the new
fibers — no manual file juggling.

### 3b. The sub-session: the temp-file method for any sorter

A `recluster` plugin never touches the session files.  Klusters writes the selection as a
**sub-session** in the session directory under a temporary base (`<base>--<ids>--<date>`,
`KlustersDoc::createReclusterSubsession`, `subsessionfiles.{h,cpp}`):

| file | what |
| --- | --- |
| `<sub>.res[.<v>].<g>` | the selected spikes' times — row subsets of every `.res` of the group, ABSOLUTE (the real recording stays valid) |
| `<sub>.spk.<v>.<g>`, `<sub>.fet.<v>.<g>` | row subsets of every waveform / feature variant on disk |
| `<sub>.fet.klusters.<g>` | the open clustering's features (the built-in recluster's file; `%final` is its `.clu` twin) |
| `<sub>.clu.<v>.<g>[.<tag>]` | the SEED: the selection's current ids from the active layer |
| `<sub>.yaml` / `.xml` | copies of the parameter files |
| `<sub>.fil`, `.dat`, `.nst.*`, `.pca.*`, `.eap.*`, `.tcl.*` | symlinks to the session's |

Rows are in time order, identical in every file, so the engine's final `.clu` (one id per sub-session
spike — relabel, never drop) maps back to the selection by position, exactly as a KlustaKwik result does.
A feature file the engine WRITES in the sub-session (`<sub>.fet.<method>.<g>`, a method that is
neither one the export copied in nor `klusters`) is carried back before the sub-session goes:
`<base>.fet.<method>.<g>` gets the sub-session's rows at their spikes, zeros with the time column
elsewhere (an existing file of the same shape is kept, so several refinements accumulate in one
space), and the *Space* selector lists it (`KlustersDoc::carryBackSubsessionFeatures`,
`subsession::scatterFetRows`).  fiber-kit's `fiber-manifold` stage uses this for its
`.fet.manifold.<g>` (per-spike field parameters; `fiber-kit-manifold.xml`, profile
`klusters-manifold`).
Everything `<sub>.*` is removed when the run ends, whatever the outcome.  The same layout is what
fiber-kit's `fk subsession export` writes (its `docs/pipeline.md`, "Reclustering a selection"), so a
sorter can be developed against the CLI and plugged into Klusters by a descriptor alone.  Shipped:
fiber-kit's `fiber-kit-recluster` (`fk install klusters-plugin`, which also installs
`fiber-kit-klusters.yaml` beside the descriptor -- the tuned profile made session-agnostic for a
sub-session -- and points the descriptor's `config` parameter at it, so the plugin runs the same
configuration whatever session is open; edit that file to change what a recluster runs).  *Actions →
Recluster → Recluster with fiber-kit* (`Shift+Q`) runs it with the Preferences defaults (no dialog),
falling back to the `fiberKitReclusterCommand` setting when the descriptor is not installed.

### 3c. Preferences → Plugins

Preferences has a **Plugins** group with one page per installed descriptor, generated from it
(`PrefPluginPage` / `PluginParamForm`): the descriptor's facts (kind, what it runs on, command, path)
and the **parameter defaults** the run dialog opens with, stored per plugin in the settings
(`Plugins/<name>/<parameter>`, `PluginSettings`; only values that differ from the descriptor are
stored, so a descriptor's new default shows through).  *Reset to descriptor defaults* per page;
*Default* resets every page.  The run dialog shows the same typed editors, prefilled with the
document's resolved value (nsamp / nchan) > the stored default > the descriptor's, and its
*Remember these values as the plugin's defaults* stores them.  *Plugins → Reload Plugins* rebuilds the
dialog's pages.

---

## 4. C++ architecture (classes to add; all thin over existing code)

```
libneurosuite-core/
  DescriptionYamlReader        (moved here from ndmanager; unchanged)

klusters/src/
  KlustersPlugin               value type: ProgramInformation + kind + context contract
  PluginRegistry               scans the discovery dirs, parses each descriptor
                               via DescriptionYamlReader, exposes plugins()
  PluginDialog                 parameter form from the descriptor (mirror
                               ndmanager programpage/parameterview) + help pane
  PluginRunner                 builds argv (resolved <consumes> + form params),
                               runs via ProcessWidget, on finished() dispatches by
                               <integration> to the existing reload paths
  (klusters.cpp)               a "&Plugins" menu populated from PluginRegistry;
                               each entry opens PluginDialog -> PluginRunner
```

No new process machinery, no new file format, no new reload code — `PluginRunner`
routes to `integrateReclusteredClusters` (recluster kind) or the triple-reload
(refiber kind) that already exist.

---

## 5. Worked examples (shipped as starter descriptors)

Four descriptors ship in `src/klusters/plugins/descriptions/` and are installed
to `share/klusters/plugins/descriptions`:

| Descriptor | kind | integration | What it runs |
| --- | --- | --- | --- |
| `fiber-refiber.xml` | `refiber` | `hierarchy-reload` | groups the `.clc` atom layer into fibers and rewrites the triple |
| `fiber-template.xml` | `analysis` | `none` | writes the marked units' linked template series (`.wtf`), one per method and stage |
| `klustakwik.xml` | `recluster` | `recluster-integrate` (`<selection>clusters</selection>`) | reclusters the selected clusters with KlustaKwik |
| `process_decomposecollisions.xml` | `recluster` | `decollide-apply` | full-group collision decomposition, the batch counterpart of the lasso decollide |

---

## 6. Implementation phases

1. ✅ **Core move + registry (read-only):** `KlustersPlugin` + `PluginRegistry` +
   a `&Plugins` menu that lists discovered plugins. (`DescriptionYamlReader` has
   not yet moved to `libneurosuite-core`; the registry parses the XML directly —
   YAML/`DescriptionYamlReader` consolidation remains a follow-up.)
2. ✅ **Dialog + runner for `analysis`/`export`/`refiber`:** `PluginDialog`
   (parameter form, prefilled from the resolved context) + a runner over
   `ProcessWidget` — here `KlustersApp::runPlugin` / `slotPluginFinished` /
   `integratePluginResult`, deliberately independent of the recluster process
   slots.  `<integration>none` reports the files the engine wrote;
   `<integration>hierarchy-reload` reloads the session from disk via the proven
   `slotFileClose()` + `openDocumentFile()` path (a full reopen — heavier than an
   in-place triple refresh, but it reuses one well-tested code path).  Starter
   descriptors are installed to `share/klusters/plugins/descriptions`.
3. ✅ **`recluster` kind** (0939): a `recluster` plugin (or any `<input>subsession</input>`
   descriptor) runs on the sub-session (§3b) through the built-in recluster path
   (`KlustersApp::startSubsessionRecluster` → `slotRecluster`), and its `%final`
   `.clu` is integrated as a KlustaKwik result.  Still open: moving the built-in
   KlustaKwik action itself onto a descriptor so there is a single recluster code path.
4. ✅ **Preferences → Plugins** (0939, §3c): typed parameters, per-plugin defaults.

The runner's `selection` is the palette the descriptor names: `<selection>children</selection>`
receives the child palette's atoms (it received the parent palette's clusters before 0938).

A future refinement of phase 2's `hierarchy-reload` is an in-place triple refresh
(re-read `.clu/.clc/.clp` + `buildHierarchyMaps`) to avoid the full reopen.
