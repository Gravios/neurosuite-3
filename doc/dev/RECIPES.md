# Recipes — where things are defined, and how to change them

*Part of the [Developer Guide](DEVELOPER_GUIDE.md).*

This page answers two questions:

- Where is the one place a format, rule or schema is specified? (§1)
- To make a common change, which files do I touch and which test proves it? (§2)

Paths are relative to the repository root. `fiber-kit:` means a path in the
fiber-kit repository. When a recipe and the code disagree, the code is right:
fix the recipe in the same patch.

**Contents:** [1. Canonical references](#1-canonical-references) ·
[2. Recipes](#2-recipes) ·
[2.1 Add a row to a shared vector table](#21-add-a-row-to-a-shared-vector-table) ·
[2.2 Add a PCAE method](#22-add-a-pcae-method) ·
[2.3 Add a session-YAML section](#23-add-a-session-yaml-section) ·
[2.4 Add an ndmanager plugin step](#24-add-an-ndmanager-plugin-step) ·
[2.5 Change a format version, the plugin protocol or the session schema](#25-change-a-format-version-the-plugin-protocol-or-the-session-schema) ·
[2.6 Give a Klusters action an icon](#26-give-a-klusters-action-an-icon) ·
[2.7 Recipes on other pages](#27-recipes-on-other-pages)

---

## 1. Canonical references

Each subject below has one canonical page. Other pages may give a one-line
summary and a link, but they do not restate the layout or the rule. A copy
that drifts from its canonical page is a bug in the copy. The code column
names the implementation the page describes. When the page and the code
differ, fix the page.

### 1.1 File formats

| Subject | Code | Canonical page | Other pages (summary + link only) |
|---|---|---|---|
| `.res` `.clu` `.fet` `.spk` `.evt` `.dat`/`.fil`/`.lfp` | `src/libneurosuite-core/src/neurosuite/core/neurofileio.{h,cpp}` | `doc/ndmanager-plugins/formats/<ext>.md` ([index](../ndmanager-plugins/formats/README.md)) | [FILE_FORMATS.md](../../src/libneurosuite-core/docs/FILE_FORMATS.md) maps each format to its reader, writer and `file:line`; [GLOSSARY.md](../GLOSSARY.md) |
| `.eap` `.tcl` `.wti` `.wtf` `.wtl` `.col` | `neurofileio.{h,cpp}` | `doc/ndmanager-plugins/formats/<ext>.md` | FILE_FORMATS.md §4 |
| `.pca` (PCAE basis) | `core/pca_projection.hpp` | [formats/pca.md](../ndmanager-plugins/formats/pca.md) | [CORE_API.md](../../src/libneurosuite-core/docs/CORE_API.md) §3.6 (the API) |
| File naming, method tokens, resolution order | `core/custody.hpp` | [formats/naming.md](../ndmanager-plugins/formats/naming.md) | CORE_API.md §3.1, FILE_FORMATS.md §1.1 |
| `.probe` | `src/libklustersshared/src/klustersshared/parameteryamlreader_probes.cpp`, `src/ndmanager-plugins/scripts/probe_geometry.py` | [formats/probe.md](../ndmanager-plugins/formats/probe.md) | [doc/libklustersshared/README.md](../libklustersshared/README.md) (the `probes:` section of the session YAML) |
| `.drift`, `.chunks`, `.loc`, `.pos`, `.clc`/`.clp`/`.cli`, Plexon `.px*`, EMG files | the step that writes them | `doc/ndmanager-plugins/formats/<ext>.md` | — |

`core/` is short for `src/libneurosuite-core/src/neurosuite/core/`.

### 1.2 Session parameter file and plugin parameters

| Subject | Code | Canonical page |
|---|---|---|
| Session YAML schema (every top-level key) | `src/libklustersshared/src/klustersshared/parameteryaml{reader,writer,modifier}.cpp` | [doc/libklustersshared/README.md](../libklustersshared/README.md#yaml-schema-reference), "YAML schema reference" |
| How a step finds its parameters (`programs:`, `general`, fallbacks, `method`/`methodOrder`) | `src/ndmanager-plugins/scripts/ndm_functions` (`read_script_parameter`) | [doc/ndmanager-plugins/parameters.md](../ndmanager-plugins/parameters.md) |
| Each step's parameters and defaults | `templates/template.yaml` (checked against the scripts) | `doc/ndmanager-plugins/commands/<step>.md` ([index](../ndmanager-plugins/commands/README.md)) |
| Session templates (`template:` block, sections) | `src/libklustersshared/src/klustersshared/sessiontemplate.{h,cpp}` | the file comment of `sessiontemplate.h` |
| `sdiffPairs` patterns (orders 4, 5, 6) as a user writes them | `libklustersshared/sdiff_pairs.h`, `core/channel_expr.hpp` | [spike-detection.md](../ndmanager-plugins/commands/spike-detection.md#custom-difference-patterns-and-alignment) |

### 1.3 Shared rules and project conventions

| Subject | Canonical page |
|---|---|
| Versions at the boundary with fiber-kit and ndm plugins: plugin protocol, session schema, format versions, shared-table hashes | `contract.yaml` at the repository root (one in each repository), stated in code by `core/version_contract.hpp`; §2.5 here |
| Rules with one implementation and mirrors in other languages (stderiv transform, sdiff grammars, custody, PCAE `Method`, formats) | [STANDARDIZATION.md](STANDARDIZATION.md) §2.10 (the table of homes); the change procedure is CORE_API.md §4.3 |
| Code conventions, directory layout, language standard | [STANDARDIZATION.md](STANDARDIZATION.md) §2 |
| Action icons: one table keyed by action id, loading forms, checks | [STANDARDIZATION.md](STANDARDIZATION.md) §2.11; Klusters' table is `src/klusters/src/actionicons.h` |
| Running and adding tests | [tests/README.md](../../tests/README.md) |
| Build options, GPU back ends, CI parity | [doc/BUILD.md](../BUILD.md) |
| Layering and what may depend on what | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Terms | [GLOSSARY.md](../GLOSSARY.md) |

---

## 2. Recipes

Each recipe lists every file to touch, in order, and ends with the test that
proves the change. Run `scripts/lint.sh` before sending any patch.

### 2.1 Add a row to a shared vector table

The tables are the contract between the C++ code, the plugin scripts and
fiber-kit. The neurosuite-3 copies are in `src/libneurosuite-core/test/`, and
byte-identical copies are in `fiber-kit:test/`.

| Table | What it pins | neurosuite-3 runners (ctest name) | fiber-kit runner |
|---|---|---|---|
| `custody_vectors.tsv` | naming, method tokens, waveform token, dropped channel | `core_test_custody_conformance`, `custody_conformance_python`, `custody_conformance_bash` | `test/test_custody_token_conformance.py` (some kinds) |
| `pca_method_vectors.tsv` | PCAE magic, version, `Method` values and helpers | `core_test_pca_method_conformance` | `test/test_pca_method_conformance.py` |
| `stderiv_vectors.tsv` | the spatial derivative + temporal difference | `core_test_stderiv_vectors` | `test/test_stderiv_vectors.py` |
| `sdiff_expr_vectors.tsv` | order-6 expressions | `core_test_sdiff_expr` | `test/test_sdiff_expr.py` |
| `sdiff_grammar_vectors.tsv` | order-4/5 patterns and pattern → order | `klustersshared_sdiff_pairs_test` (needs Qt), `ndm_test_sdiff_grammar` | `test/test_sdiff_grammar.py` |

Each table's header comment gives its row kinds and columns, and CORE_API.md
§4.3 does the same for the first two. Fields are separated by single tabs, and
empty fields count, so never align columns with spaces.

1. Add the row to `src/libneurosuite-core/test/<table>.tsv`, next to the rows
   of the same kind. If the case is new, add a `#` comment saying what it
   covers.
2. Run the neurosuite-3 runners:
   `ctest --test-dir build -R '<runner names>' --output-on-failure`.
   - **The row passes everywhere.** It records behaviour that already holds.
     Go to step 3.
   - **A runner fails.** The row describes new behaviour, and this is no
     longer a one-row change. Follow the procedure in CORE_API.md §4.3:
     change the home and every mirror in the same patch.
3. Copy the table unchanged to `fiber-kit:test/<table>.tsv` in a separate
   fiber-kit patch. Run `python3 test/run_all.py` there.
4. Record the table's new MD5 (`md5sum`) under `vectors:` in `contract.yaml`
   in both repositories. `core_shared_contract` fails, even without a
   fiber-kit checkout, while the table and the hash differ.
5. Prove the two copies, and the two contracts, agree:
   `FK_ROOT=/path/to/fiber-kit python3 src/libneurosuite-core/test/test_shared_contract.py`.
   In ctest this is `core_shared_contract` (configure with
   `-DNS_FIBER_KIT_ROOT` for the cross-repository part).

Adding a column, or a new row kind, changes every runner in both
repositories. Prefer a new kind only when no existing kind can express the
case. A new table must also be added to the file list in both
`test_shared_contract.py` scripts (neurosuite-3 and fiber-kit) and to
`vectors:` in both `contract.yaml` files, or the copies are never compared.

**Reference files for the template-library formats.** `test/golden/` (in both
repositories) holds small `.wti`/`.wtf`/`.wtl`/`.eap`/`.tcl` files: `golden.*`
written by the C++ writers, `fk.*` by fiber-kit's `neuro_io`. Each side's
test reads all of them and checks that its own writers still produce its own
files byte for byte: `core_test_golden_formats` here, `test/test_golden_formats.py`
in fiber-kit. The content is spelled out in both tests. When a writer's output
changes on purpose, regenerate (`core_test_golden_formats <dir> --write`, or
`test_golden_formats.py --write`), copy the files to the other repository and
record their new MD5s in both `contract.yaml` files, as for a table.

### 2.2 Add a PCAE method

A PCAE `Method` value is stored in every `.pca` basis header. Readers in two
repositories interpret it, so a value is never renumbered or reused: always
append a new value. The current values are 0–9 ([formats/pca.md](../ndmanager-plugins/formats/pca.md)).

**If the method needs a new spatial order** (a transform that
`stderiv_transform.hpp` cannot compute yet), do that first, as its own change:

1. `core/pca_projection.hpp`: add a value to `SdiffOrder`.
2. `core/stderiv_transform.hpp`: add the case to `spatialDerivBy`. Every
   engine calls this function, so the engines need no change of their own.
3. `src/libneurosuite-core/test/stderiv_vectors.tsv`: add rows for the new
   order, including a group it does not fit (the misfit rule).
4. `core/custody.hpp`: if the order leaves a linearly dependent channel, add
   it to `sdiffDropsLastChannel`. Make the same change in `ndm_sdiff_drops_last`
   (`src/ndmanager-plugins/scripts/ndm_custody`) and `sdiff_drops_last`
   (`src/ndmanager-plugins/scripts/ndm_resolve_io.py`), and add a `drops_last`
   row to `custody_vectors.tsv`.
5. If the order has a session-YAML spelling (an `sdiffPairs` grammar), add
   its parser and rows to `sdiff_grammar_vectors.tsv` or
   `sdiff_expr_vectors.tsv`. Teach `ndm_custody`'s pattern → order logic about
   it.
6. In fiber-kit, separately: `src/fiber_kit/fiber_stderiv.py` (and
   `neuro_io.py` for step 4), plus the copied tables.

**Then the method itself:**

1. `core/pca_projection.hpp`:
   - append the value to `enum class Method`, with its method token in the
     comment;
   - raise the upper bound in `methodValid`;
   - add the case to `spatialOrder`, `hasTemporalDiff` and `methodTag`.
2. `src/libneurosuite-core/test/pca_method_vectors.tsv`: add a `method` row
   (value, name, spatial order, has-temporal, tag).
3. `src/libneurosuite-core/test/pca_method_conformance_test.cpp`: add the name
   to the name → `Method` lookup, and move the out-of-range check
   (`!methodValid(N)`) to the new bound. `sdiff_expr_test.cpp` checks the same
   bound; update it too.
4. `src/ndmanager-plugins/scripts/ndm_pca`: map the `method`/`methodOrder`
   pair to the new value in the `pcaMethod` case.
5. `src/ndmanager-plugins/src/process_pca/process_pca.cpp`: add the value to
   the `--pca-method` usage text. Its `nInputChannels` comes from
   `custody::sdiffDropsLastChannel`, so it needs no other change.
6. Docs: the `method` table in [formats/pca.md](../ndmanager-plugins/formats/pca.md),
   and the enum listing in CORE_API.md §3.6.
7. In fiber-kit, separately: `src/fiber_kit/fiber_pca.py` (`Method`,
   `method_tag`, `has_temporal_diff`, `spatial_order`), plus the copied
   `test/pca_method_vectors.tsv`.

**Test:**

- neurosuite-3: `ctest -R 'core_test_pca_method_conformance|core_test_sdiff_expr|core_test_stderiv_vectors|custody_conformance|ndm_test_subset_pca'`.
- fiber-kit: `python3 test/run_all.py`.
- Both: `core_shared_contract`.

Klusters, kiloklustakwik and the refeaturize/align plugins read the method
only through `spatialOrder` and `hasTemporalDiff`, so they need no change.
Check with `grep -rn 'Method::' src`. A consumer that names individual values
needs the new case.

### 2.3 Add a session-YAML section

A section is a top-level key of `SESSION.yaml`. The parameters of a single
plugin step are not a section: they belong in that step's `programs:` block
(§2.4).

The apps keep a top-level key they do not know about. ndmanager saves through
`ParameterYamlWriter` on top of the file as it is on disk
(`setBaseDocument`), and NeuroScope edits the file in place with
`ParameterYamlModifier`. Both keep the key, and its comments
(`yamlformat.h`). A section that only scripts read therefore needs no C++
change to survive a save.

1. **Schema doc.** Add the key to the "YAML schema reference" in
   [doc/libklustersshared/README.md](../libklustersshared/README.md#yaml-schema-reference).
   If its layout needs more than one comment line, give it a subsection there.
2. **Template.** If new sessions should get the section, add it to
   `templates/template.yaml` with its defaults.
3. **Session templates.** Add a `SectionInfo` row to `knownSections()` in
   `src/libklustersshared/src/klustersshared/sessiontemplate.cpp`. The row
   gives the key, the dialog label, a one-line hint, and whether Export and
   Apply check it by default. A recording's own setup is exported but not
   applied, and session-specific data is not exported (see the comment on
   `knownSections()` in `sessiontemplate.h`). Without a row, the section is
   still exported, under its key name, but it is not applied.
4. **C++ readers** (only if an app reads it):
   - add an accessor to `ParameterYamlReader` (`parameteryamlreader.{h,cpp}`).
     The reader is `const`, so descend through the `safeGet2` helper in
     `parameteryamlreader.cpp` rather than chained `operator[]`
     (doc/libklustersshared/README.md, yaml-cpp 0.8 note);
   - add a setter to `ParameterYamlWriter` if an app writes it, and to
     `ParameterYamlModifier` if NeuroScope edits it in place (ndmanager
     writes through its own code, not the modifier);
   - add a wrapper method to the app's reader (`NdManagerYamlReader`,
     `NeuroscopeYamlReader` or `KlustersYamlReader`) only if that app needs
     it.
5. **Script readers** (only if a step reads it):
   - in bash, use `yaml_read` / `yaml_count` from `ndm_functions` with the
     key's path. They read any key, so they need no change;
   - in Python, read it with PyYAML from the session file, as
     `kk_build_prior.py` does.
6. **fiber-kit** (only if it reads it): `src/fiber_kit/session_yaml.py`, in a
   separate patch.

**Test:**

- C++: `ctest -R 'klustersshared_yamlwriter_test|klustersshared_yamlmodifier_test|klustersshared_sessiontemplate_test'` (needs Qt). Add a case for the new
  key to the writer test (round trip) and to the session-template test (it is
  listed, exported and applied as declared).
- Scripts: the step's own `ndm_test_*`.
- fiber-kit: `test/test_session_cfg.py`.

### 2.4 Add an ndmanager plugin step

A step is a script `src/ndmanager-plugins/scripts/ndm_<name>`, usually
backed by a C++ program `process_<name>` or a Python helper. The tests in
`src/ndmanager-plugins/test/` keep the script, the template, the Pipeline
designer and the docs in step. They fail until all four agree.

1. **The script.** Write `src/ndmanager-plugins/scripts/ndm_<name>` on the
   skeleton described in
   [commands/ndm_functions.md](../ndmanager-plugins/commands/ndm_functions.md).
   Read its parameters with `read_script_parameter`. Resolve per-group inputs
   and outputs through `ndm_custody` (the naming rule is
   [naming.md](../ndmanager-plugins/formats/naming.md)); do not build file
   names by hand. Add the script to the `install(PROGRAMS …)` list in
   `src/ndmanager-plugins/scripts/CMakeLists.txt`. A Python helper goes in the
   Python-helpers block of the same file.
2. **The program** (if any). Add `src/ndmanager-plugins/src/process_<name>/`
   with its sources and a `CMakeLists.txt` modelled on
   `src/process_pca_stderiv/CMakeLists.txt` (`add_executable`,
   `target_compile_features(… cxx_std_20)`, `install(TARGETS …)`,
   `add_manpage`). Add `add_subdirectory(process_<name>)` to
   `src/ndmanager-plugins/src/CMakeLists.txt`. A new plugin is the one change
   that may touch that file (STANDARDIZATION.md §6.5). Use the
   `neurosuite/core` headers for file formats, naming and transforms; a
   private copy of any of them is what §2.10 rules out.
3. **The template block.** Add a `programs:` entry for `ndm_<name>` to
   `templates/template.yaml`. List every parameter the script reads, with its
   default, status and help.
4. **The Pipeline designer.** Add the step's row (palette label, tooltip,
   category) to the table in `src/ndmanager/tools/pipeline_script_defs.py`.
   Run it to regenerate `src/ndmanager/src/pipelinescriptdefs.inc`.
5. **The docs.** Add `doc/ndmanager-plugins/commands/ndm_<name>.md` (or a
   section of a family page) and list it in
   [commands/README.md](../ndmanager-plugins/commands/README.md). If the step
   writes a new file type, add `doc/ndmanager-plugins/formats/<ext>.md` and list
   it in [formats/README.md](../ndmanager-plugins/formats/README.md).
6. **Optional:**
   - a `.docbook` man page next to the script or program (`add_manpage` skips
     it when it is absent);
   - nothing in `src/ndmanager-plugins/descriptions/`: those XML files are
     installed but not read (NDManager reads YAML). NDManager's description
     of the step, its parameters with their help for the Plugins pages and
     their **Add**, is the template block of step 3.
7. **A test.** Add `src/ndmanager-plugins/test/test_<name>.py` or `.sh`. It is
   registered as `ndm_test_<name>` on the next configure. If it runs the C++
   program, map the program in that directory's `CMakeLists.txt`, the way
   `test_subset_pca` maps `process_pca`.

**Test:**
`ctest -R 'ndm_test_(template_parameters|pipeline_designer_defs|docs|<name>)'`,
plus `python3 src/ndmanager/tools/pipeline_script_defs.py --check`.

### 2.5 Change a format version, the plugin protocol or the session schema

`contract.yaml` at the root of each repository states what that repository
promises at the boundary:

- `plugin_protocol`: the ndm-plugin-protocol it speaks;
- `session_schema`: the `parameters.version` it writes;
- `formats`: for each version-tagged format (`pca`, `wti`, `wtl`, `tcl`,
  `eap`, `nst`), the highest version it reads and the version it writes;
- `vectors`: the MD5 of each shared table and reference file.

The code states the same values: in neurosuite-3, the constants in
`core/version_contract.hpp`, `neurofileio.h` (`kWtiVersion`, `kWtlVersion`,
`kTclVersion`, `kEapVersion`), `core/pca_projection.hpp` (`kPcaeVersion`) and `core/noise_stats.hpp` (`kNsteVersion`).
In fiber-kit they are the version constants in `src/fiber_kit/neuro_io.py`,
`fiber_pca.py` and `ndm_fiber_kit.py`. The rule between the repositories is
that each reads every version the other writes.

**A new version of a format** (say `.wtl` v3):

1. Teach the readers the new version first, in both repositories, and release
   that. Then the reader side can already take what the writer will write.
2. neurosuite-3: change the reader and writer in `neurofileio.cpp`, bump the
   constant in `neurofileio.h` (or `kPcaeVersion`), and update the format page
   under `doc/ndmanager-plugins/formats/`.
3. Update `formats:` in `contract.yaml` and bump `contract:` (and
   `kContractVersion`).
4. fiber-kit, separately: the same in `neuro_io.py` / `fiber_pca.py`, its
   `contract.yaml`, and the format lines `ndm_fiber_kit.py --ndm-version`
   prints.

**The plugin protocol or the session schema:** change `kNdmPluginProtocol` or
`kSessionSchema` in `core/version_contract.hpp`, the matching line of
`contract.yaml`, and bump `contract:`. ndmanager reads the protocol from that
header; the YAML writer and modifier stamp the schema from it. fiber-kit's
`PROTOCOL` (`ndm_fiber_kit.py`) and its `contract.yaml` change in the same
release.

**Test:**

- neurosuite-3: `ctest -R 'core_test_contract|core_shared_contract'`.
  `core_test_contract` writes each format and checks the version on disk, and
  checks that each reader rejects the version after the one it claims.
- fiber-kit: `test/test_contract.py`.
- Both, with the other checkout: `core_shared_contract` / fiber-kit's
  `test_shared_contract.py`.

ndmanager warns when a plugin's `--ndm-version` format lines do not match this
build (`contract::formatMismatches`), so a mismatch that reaches users is
visible there too.

### 2.6 Give a Klusters action an icon

1. Find the action's id: its `registerActionCommand` id in `klusters.cpp`, or,
   if it has none, `menu:` + its menu path (`menu:Tools/Split Clusters`;
   mnemonics and a trailing "…" dropped — `actionid.h`). Preferences ▸ Radial
   menu shows every id as a tooltip in *Available actions*.
2. Pick the kind (STANDARDIZATION.md §2.11):
   - **Standard** (a generic action): the freedesktop icon name; if the bundled
     copy in `libklustersshared/src/gui/libklustersshared-icons.qrc` has another
     name, give it as the fallback.
   - **Bundled**: put the PNG in `src/klusters/src/icons/` and list it in
     `klusters-icons.qrc` as `<file alias="icons/<alias>">icons/<file>.png</file>`,
     the alias lower-case and hyphenated.
   - **Drawn**: add a painter to `actionicons::drawnIcon()` that takes its
     outline colour from the palette.
3. Add the row to the table in `src/klusters/src/actionicons.cpp` (and raise the
   array size). Do not add a `setIcon()` call where the action is created.

**Test:** `ctest -R klusters_test_actionicons` (the row's icon is bundled and
decodes; a drawn icon follows the palette). A build with `-DNS3_VERBOSE=1`
prints any row whose id names no action when Klusters starts.

### 2.7 Recipes on other pages

| Change | Recipe |
|---|---|
| Add a libneurosuite-core header or test | [CORE_API.md](../../src/libneurosuite-core/docs/CORE_API.md) §4.4 |
| Change behaviour a vector table pins | CORE_API.md §4.3 |
| Add a libklustersshared test | [LIBRARY.md](../../src/libklustersshared/docs/LIBRARY.md) §8 |
| Add a Preferences page | LIBRARY.md §5.3 |
| Add a realign (xcorr) back end | LIBRARY.md §4.7 |
| Add a Klusters view | [VIEW_SYSTEM.md](../../src/klusters/docs/VIEW_SYSTEM.md) §7 |
| Add a Klusters modal input mode | [klusters INPUT_SYSTEM.md](../../src/klusters/docs/INPUT_SYSTEM.md) §9 |
| NeuroScope input bindings | [neuroscope INPUT_SYSTEM.md](../../src/neuroscope/docs/INPUT_SYSTEM.md) §7 |
| Add a test anywhere else | [tests/README.md](../../tests/README.md), "Adding a test" |
| Add a build-system check | tests/README.md, "Build-system invariants" |
