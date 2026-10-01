# Klusters Plugin / Extension API

Status: **phases 1–2 implemented** (discovery + dialog + runner + `none`/
`hierarchy-reload` integration); phase 3 (`recluster-integrate`) pending — see §6.
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

Discovery order (later overrides earlier on name collision):

1. `${CMAKE_INSTALL_PREFIX}/share/klusters/plugins/descriptions/*.{xml,yaml}`
2. `$XDG_DATA_HOME/klusters/plugins/descriptions/*.{xml,yaml}`  (user plugins)
3. a path in `$KLUSTERS_PLUGIN_PATH`

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

### Result integration (`<integration>`)

| value | Klusters action on success |
| --- | --- |
| `recluster-integrate` | feed the engine's `.clu` through the existing recluster integration (`integrateReclusteredClusters` / `reclusteringUpdate`) |
| `hierarchy-reload` | reload the `.clu/.clc/.clp` triple (the detection + `buildHierarchyMaps` path), refreshing both palettes |
| `none` | just report exit status; offer to open any produced file |

`hierarchy-reload` is the hook the re-fibering workflow needs: `fiber-refiber`
writes the triple, Klusters reloads it, the atoms reappear as children of the new
fibers — no manual file juggling.

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

`plugins/descriptions/fiber-refiber.xml` — kind `refiber`, integration
`hierarchy-reload`: turns the spike-recluster atoms into fibers.

`plugins/descriptions/klustakwik.xml` — kind `recluster`, integration
`recluster-integrate`, `<selection>clusters</selection>`: wraps the existing
KlustaKwik backend as a first-class plugin so the built-in recluster and a
user's alternative backend are registered the same way.

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
3. ⏳ **`recluster` kind:** route a plugin's `.clu` through
   `integrateReclusteredClusters` / `reclusteringUpdate`; migrate the built-in
   KlustaKwik action onto the plugin path so there is a single recluster code
   path.  Until then the runner executes a `recluster-integrate` engine but leaves
   integration to the built-in Recluster action.

A future refinement of phase 2's `hierarchy-reload` is an in-place triple refresh
(re-read `.clu/.clc/.clp` + `buildHierarchyMaps`) to avoid the full reopen.
