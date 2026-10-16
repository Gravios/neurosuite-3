# neurosuite-3 standardization prompt

This document is what I (Claude) wish I'd had at the start of every session
working on `github.com/Gravios/neurosuite-3.git`.  It's a checklist + reference
that prevents drift, miscalibration, and re-litigation of decisions already
made.  Read it first before designing or shipping any change.

**Contents:** [1. Project map](#1-project-map) · [2. Hard conventions](#2-hard-conventions) · [3. Domain invariants](#3-domain-invariants) · [4. Workflow](#4-workflow) · [5. Audit playbook (5-phase)](#5-audit-playbook-5-phase) · [6. Things I get wrong (anti-patterns to avoid)](#6-things-i-get-wrong-anti-patterns-to-avoid) · [7. Tooling](#7-tooling) · [8. Reading the repo first](#8-reading-the-repo-first) · [9. Verification gates before shipping](#9-verification-gates-before-shipping) · [10. End-of-session recap pattern](#10-end-of-session-recap-pattern)

---

## 1. Project map

`neurosuite-3` is a Qt6/C++20 modernization of the Neurosuite electrophysiology
toolchain.  The repo contains seven tightly-coupled subsystems:

| Subsystem | Path | Role |
|---|---|---|
| **klusters** | `src/klusters/` | interactive spike-sorting GUI (Qt6, ~30K LOC) |
| **kiloklustakwik** | `src/kiloklustakwik/` | GPU-accelerated CEM clustering engine, formerly the experimental KlustaKwikExp fork (CUDA primary, HIP/SYCL stubs) |
| **ndmanager** | `src/ndmanager/` | session management GUI |
| **ndmanager-plugins** | `src/ndmanager-plugins/` | preprocessing plugins (CLI tools + bash wrappers) |
| **neuroscope** | `src/neuroscope/` | trace viewer GUI |
| **libklustersshared** | `src/libklustersshared/` | shared library (Qt6 + yaml-cpp + KDE-port widgets) |
| **libneurosuite-core** | `src/libneurosuite-core/` | Qt-free core (std only): file I/O (`neurofileio`), file naming (`custody`), PCA, spike extraction, templates |

`libklustersshared` is consumed by all three Qt apps (klusters, ndmanager,
neuroscope) and provides shared widgets (DockArea, ZoomWindow), color
management (ItemColors, ChannelColors), schema I/O (ParameterYamlReader/
Writer/Modifier), and the progress bar used by ndmanager-plugins CLI tools.

An eighth subproject, `nphys-data` (`src/nphys-data/`), packages MIME/desktop
resources and contains no C++.  Every subproject under `src/` follows the
standard directory layout codified in §2.7.

`process_smrconvert` is an **external package** — never modify it.

---

## 2. Hard conventions

### 2.1 Naming

**Members are bare** — no `m_` prefix, no Hungarian.  Project-wide, klusters
uses zero `m_` on actual class members.  The canonical pattern is:

```cpp
class Foo {
public:
    Foo(int initialValue) : value(initialValue) {}
private:
    int value;       // bare, project style
    bool isActive;   // bare, predicate-style for booleans
};
```

When a member name would collide with a constructor parameter, **rename the
parameter** (e.g. `lbl`, `stp`, `tot` for `label`, `step`, `total`); do not
introduce `m_` to disambiguate.  This avoids `-Wshadow` warnings while
preserving the bare-name convention.

The parameteryaml family in libklustersshared was the historical outlier
(used `m_`); it has been stripped.  Any future PR that introduces `m_` is a
regression.

**Booleans are predicate-style** — `isChanged`, `colorChanged`, `valid`,
`enabled`, `started`, `finished`, `failed`, `unicode`, `isTty`.  Non-predicate
boolean names (`flag`, `state`, `mode` when actually `enableMode`) are not used.

### 2.2 Casts

**No C-style casts in new code.** Use `static_cast<T>`, `qobject_cast<T*>` (for
QObject polymorphism), `reinterpret_cast<T*>` (rare, document why), and
`const_cast<T*>` (rarer; document why).  The legacy code is already clean —
don't introduce regressions.

### 2.3 Iterators and ordering

- `Data::Iterator::operator()` returns `QPoint` with **ordinate already negated**
  (Qt graphical orientation, see `data.h:405-409`).  Never apply an extra flip
  when computing screen-space bounds from iterator output.
- `QMap` iterates **ascending key order** by default.  Many algorithms in
  klusters rely on this; don't sort separately.
- `Data::nextFreeClusterId() = highestClusterId() + 1` is canonical.  Don't
  invent your own gap-finding strategy.

### 2.4 Cluster colors

Hue formula: `hue = ((clusterId * 7) mod 36) * 10`, `sat = 200`, `val = 255`.
Don't change this; the visual continuity across sessions depends on it.

### 2.5 Curation log primitives

`KlustersDoc::applyClusterRename(partial, fullOptional)` is the single
primitive for renumbering operations.  All curation actions
(dipsplit, watershed, basin labeling, undo/redo) go through it or
its siblings (`prepareReclusteringUndo`, `addNewClustersToView`,
`emit newClustersAdded(emptiedClusters)`).  **Never pre-mutate
`clusterInfoMap`** before `prepareUndo` — it pollutes the undo
snapshot.  Read from current state, write to fresh temp tables, then
commit.

### 2.6 Multi-cluster commit signal

For commits that produce >1 new cluster (recluster, watershed, dipsplit),
emit `newClustersAdded(QList<int>&)` — the recluster-shaped multi-cluster
signal — **once**.  Do not emit `newClusterAdded(int)` (single-cluster
signal) multiple times.  Doing so causes view inconsistency.

### 2.7 Subproject directory layout

The repo is a monorepo, but every subproject under `src/` has the same
standard shape:

```
<subproject>/
  CMakeLists.txt          # build entry (+ cmake/ modules, config-*.cmake.in)
  src/                    # ALL C/C++ sources and headers
  test/                   # unit tests, if any
  doc/                    # user- & packaging-facing docs (DocBook handbook under doc/en/), if any
  docs/                   # developer code references in Markdown, next to the code, if any
  <packaging>             # description, AUTHORS, COPYING, … (no postinst/postrm:
                          # cmake/NsDesktopCaches.cmake refreshes MIME/icon caches)
```

- **No C/C++ sources at a subproject root** — they live under `src/`.
  kiloklustakwik was the last flat outlier (all sources in its root);
  they were moved into `src/` and the CMakeLists source lists are now
  `src/`-prefixed, with a directory-scoped `include_directories(src)`
  so every target (including the HIP/SYCL object libraries) finds them.
- The Qt apps build via `add_subdirectory(src)`; `src/CMakeLists.txt`
  lists sources relative to `src/` and sets the include path to `src/`.
  libklustersshared keeps its public headers under `src/klustersshared/`.
- **`doc/` vs `docs/` are distinct, and both are standard.** `doc/` holds the
  user- and packaging-facing documentation that is built and installed — the
  DocBook handbook lives under `doc/en/` (klusters, ndmanager, neuroscope).
  `docs/` holds developer code references in Markdown that live *next to the
  code* and are read on GitHub / in an editor, not built —
  `klusters/docs/INPUT_SYSTEM.md` and `PLUGIN_API.md`,
  `neuroscope/docs/INPUT_SYSTEM.md`. This is the "references live next to the
  code" split the [Developer Guide](DEVELOPER_GUIDE.md) uses; §2.8 covers the
  project-wide doc homes (`doc/design/`, `doc/workflows/`).
- **Tests live in `<sub>/test/`**, never `tests/` (klusters was the last
  outlier).  The top-level `tests/` holds the build-system checks and is not a
  subproject.
- **Never keep a file at both `<sub>/X` and `<sub>/src/X`.**  See §6.8.
- `build_invariants` (check 9) fails if a subproject has a `tests/` directory
  or a C/C++ source at its root.

### 2.8 Documentation and changelog layout

- `CHANGELOG.md` (repo root) is the **single canonical, date-ordered**
  project changelog.  There is no `CHANGES.md` at the root — it was
  consolidated into `CHANGELOG.md`.
- Deep per-topic technical notes live in `doc/design/<topic>.md`, indexed
  from `doc/design/README.md` and from the reference table at the end of
  `CHANGELOG.md`.  Do not re-create flat `CHANGES-<topic>.md` files at the
  root; add a `doc/design/` page and index it.
- Task-oriented walkthroughs spanning multiple programs live in
  `doc/workflows/`.
- Program-internal histories (`src/<prog>/CHANGES.md`,
  `CHANGES-inherited-from-canonical.md`) stay in the source tree on
  purpose; the top-level changelog references them but does not absorb
  them.
- Vendored third-party changelogs (e.g. libsamplerate) are never touched.

### 2.9 Language standard

- **C++20 everywhere, C11 for C**, extensions off.  Every project root sets
  `CMAKE_CXX_STANDARD 20` (required); this holds for the apps, both libraries,
  the plugins and kiloklustakwik alike.
- A target that declares a compile feature declares `cxx_std_20`, not an
  older floor: a `cxx_std_17` there builds as C++20 anyway and only misleads.
- There is no "C++17-clean" subset.  An older claim that the core headers had
  to stay C++17 because the plugins built as C++17 was never true.
- Exempt: `process_smrconvert` (an external package, §1) and the vendored
  libsamplerate.
- `build_invariants` (check 8) enforces this.  Syntax checks use
  `-std=c++20` (§7.5, §9).

---

## 3. Domain invariants

These are facts about the data model that aren't obvious from the code but
matter for correctness.

### 3.1 Spike file invariant

**`.spk[i]` peak sample ≡ `.fil` at `.res[i]`.**  The waveform's peak sample
must align with the timestamp file's entry.  All curation operations that
modify timestamps (nudge, realign, extractspikes_stderiv) must preserve this
invariant.  After nudge, `.res.pending` (not the original `.res`) holds the
authoritative timestamp.

### 3.2 Peak position indexing

Two **distinct** peak fields exist, and **neither is converted** at the I/O
boundary:

- **Session `peakSampleIndex`** (the `peakPositionInWaveform` of the parameter
  file) round-trips through YAML **verbatim** — the reader does not subtract 1,
  the writer does not add 1 (`parameteryamlreader.cpp`, `parameteryamlwriter.cpp`).
  A value of `0` means "missing/invalid" (`data.cpp`), so the on-disk value is
  itself used **1-based**.
- **`.wti` `peakSample`** (the extraction window index in `neurofileio`) is
  **0-based**, with `-1` = unknown; it too is stored verbatim.

Don't reintroduce a ±1 at the YAML boundary, and don't conflate the two fields.
See `src/libneurosuite-core/docs/FILE_FORMATS.md` §3.6.

### 3.3 File formats (binary, little-endian)

| File | Header | Body |
|---|---|---|
| `.res.N` | none | int64 timestamps |
| `.spk.N` | none | int16 sample-major waveforms |
| `.spkD.N` | none | int16, full nCG channels (stderiv in-place, no channel dropped) |
| `.clu.N` | int32 (count) | int32 cluster IDs |
| `.fet.N` | int32 (nCols) | int64 features, row-major |
| `.fetD.N` | int32 (nCols) | int64 features, `(nChan-1)*nComp+1` cols (last linearly-dependent channel dropped before PCA in `process_pca_stderiv`) |
| `.col.N` | 32B + 32B params | template table + record table |

### 3.4 KiloKlustaKwik gotchas

- `MergeThresh` must be calibrated to `chi²(nSpatialDims, 0.99)` — the
  default `200` is catastrophically large for typical feature dimensionality.
- `UseFeatures` default is `"all"`; the legacy fixed-length default string
  caused session duration to compute as 0, bypassing chunked CEM entirely.
- `probabilities` must be initialized to `nullptr` in the constructor to
  avoid spurious delete on garbage pointer at first thread completion.
- GPU shared memory: query `sharedMemPerBlock` at allocate time; fall back
  to global atomics kernel when shared buffer would overflow.  RTX 5070 Ti
  has 128 KB shared memory per block; pre-Blackwell limit is 48 KB.
- `pickInputPath(out, outSize, base, ext, elec)` is the canonical helper
  for `.fet`/`.fetD` (and other dual-extension) lookups.  Tries
  `<base>.<ext>.<elec>` first, falls back to `<base>.<ext>D.<elec>`.

### 3.5 NTFS / fuseblk caveat

`HDF5_USE_FILE_LOCKING=FALSE` must be set **before HDF5 library initialization**
— before `H5Eset_auto2`, not at `H5Fopen` call.  Reference data path
`/data` is on a Samsung 9100 PRO 8TB at NTFS/fuseblk; locking will fail
otherwise.

### 3.6 Reference data

Primary test session: `jg05-20120316`, group 7.  Secondary: `eb05-20251118`,
group 25.  Use these for any change that needs end-to-end verification.

---

## 4. Workflow

### 4.1 Iterative patch-series pattern

The session loop is:

1. Gravio describes a goal (often terse: a paste of build log, runtime
   failure, or a one-line ask).
2. Claude diagnoses, designs, and commits the change in a working clone.
3. Claude exports a numbered `git format-patch` series and sends the
   `.patch` files.
4. Gravio applies them with `git am` and build-tests on `nphy-069`.
5. Gravio reports build errors / runtime failures tersely.
6. GOTO 2: fixes are **new patches on top**, not rewrites of shipped ones.

Each commit ends with a NOTE distinguishing what was verified (built, ctest,
`git am`) from what still needs a hardware build (Qt/CUDA/HIP/SYCL wiring
that could not be compiled where the patch was made).

Tarballs are no longer used.  Older entries in `CHANGELOG.md` and
`doc/history/` mention `*.tar.gz` drops; that was the earlier delivery format.

### 4.2 Patch discipline

- **One concern per commit**, with an imperative, component-scoped subject
  (`klusters: …`, `build: …`, `doc: …`).
- **Numbering continues across sessions**: `git format-patch --start-number N
  <base>..HEAD`, where `N` follows the last patch already shipped.
  fiber-kit patches are numbered separately from 0001 and kept apart from
  neurosuite-3's.
- **Author** `Justin Graboski <justingraboski@gmail.com>`; the commit message
  ends with the attribution trailers the session specifies.
- **Verify the whole series** with `git am` onto a fresh checkout of the base,
  and confirm the resulting tree matches the working clone.
- Only source changes go in a patch: no build output, no `.gitignore`d files,
  no generated `ui_*.h`.

### 4.3 Reference clone discipline

Maintain a fresh clone alongside the working copy:
- a fresh clone at the base commit, used for auditing, diff baselines and
  the `git am` check;
- the working clone, where the commits are made.

When verifying that a change preserves behavior, diff the working copy's
file against the fresh clone's, ideally after normalizing whitespace and
the things the change is supposed to alter.

### 4.4 Tone and density

- **Terse iterative loop** is the norm.  Diagnose without enumerating;
  fix without asking permission for each step; ship; let Gravio direct
  the next iteration.
- **Don't ask "should I do X?"** when X is the obvious next step.  Just
  do it and ship.  Gravio will redirect if wrong.
- **Don't enumerate options** when one is clearly correct.  Pick and ship.
- **Do enumerate options** when a real architectural fork exists that
  Gravio should weigh in on.  Mark options A/B/C, give a recommendation,
  let Gravio pick.

### 4.5 What ships in `/mnt/user-data/outputs/`

Only artefacts Gravio will use directly:
- The `.patch` files (fiber-kit's under `fiber-kit/`)
- Generated docs (e.g. AUDIT.md, DEDUPE.md, or a per-topic change note)
- Test reports if requested

Not:
- Intermediate analysis files
- Throwaway demos
- Logs from build attempts

Keep everything else in the session's scratch space.

---

## 5. Audit playbook (5-phase)

When asked to audit a subsystem, the established phases are:

1. **Phase 1 — Spelling.** Grep for the canonical misspell list:
   `absciss[^a]`, `Lenght`, `futur[^e]`, `immediat[^e]`, `buton`,
   `recieve`, `occured`, `seperate`, `existance`, `compatable`,
   `accessable`, `successfull`, `neccessary`, `begining`, `comparaison`,
   `occurence`, `wether`, `wich`, `thier`, `preceeded`, `alligned`,
   `indipendent`, `definately`, `transfered`, `accomodate`, `occassion`.

2. **Phase 2 — Boolean naming.** Find boolean members not following the
   predicate-style convention and rename for clarity.

3. **Phase 3 — `m_` prefix homogenisation.** Direction is **STRIP**, not
   add.  Project convention is bare names.  Watch for collisions with
   constructor parameters; rename params (`label` → `lbl`) rather than
   keeping `m_` to avoid shadowing.  Verify clean compile under
   `-Wall -Wextra -Wshadow`.

4. **Phase 4 — Stale `@param` documentation.** Scan for doc/sig
   mismatches where doc lists a `@param X` that doesn't exist in the
   signature.  Don't flag missing-docs cases — those are a separate,
   lower-priority concern.

5. **Phase 5 — C-style cast sweep.** Convert `(int)x`, `(QFoo*)bar` to
   `static_cast<int>(x)`, `qobject_cast<QFoo*>(bar)` etc.

Plus: **dead-code analyzer pass** (`scripts/audit/find_dead_and_dupes.py`),
manually triaged.  Likely false positives: Qt SLOT/SIGNAL string macros,
function pointer captures, template instantiations.  Likely true positives:
static helpers, getters added speculatively against schemas that didn't
materialize.

---

## 6. Things I get wrong (anti-patterns to avoid)

This section documents Claude's failure modes specifically.

### 6.1 m_-prefix direction

**I have inverted this twice.**  The project convention is **bare** member
names, not `m_`-prefixed.  When asked to "homogenise" or "audit for
consistency", the operation is to **strip** `m_` from outliers, not add it.
Klusters uses zero `m_` prefix on actual class members; libklustersshared's
parameteryaml family was the outlier.

If I find myself writing `m_foo` in new code: **stop** and verify project
style before continuing.  Default to bare.

### 6.2 Pre-mutating before prepareUndo

When implementing a curation operation, the natural-feeling pattern is
"mutate the state, then snapshot for undo."  This is **wrong** for klusters.
The correct pattern is:

```cpp
void someCuration() {
    // 1. Read current state
    QMap<int, ClusterInfo> oldMap = clusterInfoMap;
    // 2. Build new state in fresh tables
    QMap<int, ClusterInfo> newMap = ...;  // computed from oldMap
    // 3. Snapshot current state for undo BEFORE mutating
    prepareUndo();
    // 4. Atomically swap
    clusterInfoMap = std::move(newMap);
    // 5. Emit the recluster-shaped signal
    emit newClustersAdded(emptiedClusters);
}
```

Pre-populating `clusterInfoMap` with placeholder entries before `prepareUndo`
pollutes the undo snapshot.  This caused the dipsplit-postcommit-v1 segfault.

### 6.3 Multiple single-cluster signals for multi-cluster commits

When a curation creates >1 cluster, emit `newClustersAdded(QList<int>&)` once,
not `newClusterAdded(int)` multiple times.  This caused the second
dipsplit-postcommit-v1 bug (view inconsistency).

### 6.4 `peakPositionInWaveform` indexing

**Correction (see §3.2).** The session `peakSampleIndex` is **not** converted
at the YAML boundary — it round-trips verbatim and is used 1-based (`0` =
missing). The separate `.wti` `peakSample` is 0-based with `-1` = unknown. The
real trap is adding a phantom ±1 "to match the file," or conflating the two
fields; the current reader/writer do neither.

### 6.5 ndmanager-plugins parent CMakeLists

The parent `src/ndmanager-plugins/src/CMakeLists.txt` is intentionally not
touched when a working copy already has functional `process_*` builds.
Don't add new `add_subdirectory()` calls there as part of an unrelated
change.

### 6.6 `pca.nCh` reader bound

**Resolved.** The fixed `nCh > 64` rejection is gone. The PCA-basis sanity
check in `klustersdoc_realign.cpp` now bounds `nCh` against the group's own
`nChan` (a valid per-group basis has `nCh == nChan`, or `nChan-1` for stderiv)
and caps only `nComp > 64`, so Neuropixels-class probes are no longer rejected
on channel count. `neurofileio`'s `.fet` reader imposes no channel cap, and the
basis is read by `core::loadPca`, not `neurofileio`.

### 6.7 Scope creep in audits

When asked to audit "X", Claude tends to also fix Y and Z that look related.
Don't.  Audit X only.  File Y and Z as deferred items in the AUDIT.md.

### 6.8 Loose or duplicate sources at a subproject root

The standard layout (§2.7) puts all sources under `src/`.  Two failure
modes have appeared in the tree:

- A subproject left fully flat (every source at its root).  kiloklustakwik
  was the last one; its sources were moved under `src/`.
- A file present at **both** `<sub>/X.h` and `<sub>/src/X.h`, where only
  the `src/` copy is compiled and on the include path and the root copy is
  a stale leftover from an earlier move (ndmanager and neuroscope each had
  several, some byte-diverged from the live `src/` versions).

When consolidating, do **not** "merge" blindly.  Confirm via the build
which copy is live — `_srcs` in `src/CMakeLists.txt` is relative to `src/`,
and the include path is `src/` (`CMAKE_CURRENT_SOURCE_DIR` inside
`src/CMakeLists.txt`) — then remove the dead root copy.  Removing a dead
duplicate touches no build wiring; moving a genuinely-flat subproject's
sources does, and must be build-tested per backend.

---

## 7. Tooling

### 7.1 Analyzer scripts

- `scripts/audit/find_dead_and_dupes.py` — AST-based dead-code and
  clone analyzer.  Tree-sitter-cpp, ~600 LOC.  Set `ROOT` to the
  scope you want.  Outputs to stdout.  Caveats are documented in
  the report header (Qt SLOT/SIGNAL invisibility, template
  instantiation gaps, etc.).

### 7.2 Test harnesses

**The regression suite** is opt-in: configure with `-DNS_BUILD_TESTS=ON` and
run `ctest --test-dir build --output-on-failure`.  [tests/README.md](../../tests/README.md)
is the one reference: running one subproject (`ctest -R`), a Qt-free configure
that builds the core, kiloklustakwik and build-system tests in a few minutes,
the ndmanager-plugins script tests and the fiber-kit contract check (neither
runs under ctest).

For terminal/TTY-related features (progressbar):
- `script(1)` for PTY emulation in container — `script -q -c '<cmd>' /tmp/cap.log`.
- Python `pty` module for finer control with redirections.
- `LANG=C.UTF-8 LC_ALL=C.UTF-8` to enable Unicode paths in test child processes.

### 7.3 Reference data

`jg05-20120316` group 7 is the canonical end-to-end test session.  Available
at the standard project paths on Gravio's machine; not in the container.
Container-side testing is limited to unit-level (demo programs, fake data)
plus visual verification of TTY rendering.

### 7.4 Build environment

- CMake-based build, multiple GPU backends optional (CUDA primary).
- `CMakeLists.txt` files often have plugin-specific quirks; respect the
  pattern of sibling plugins when adding a new one.
- The `install(PROGRAMS …)` in `src/ndmanager-plugins/scripts/CMakeLists.txt`
  must include any new `ndm_*` script for it to be installed.

### 7.5 Compiler flags worth always using

- `-Wall -Wextra` — baseline, project compiles clean under these.
- `-Wshadow` — useful for catching member/parameter collisions during
  audit work, especially Phase 3 m_-prefix strips.
- `-std=c++20` — Qt6 + std::span etc.

---

## 8. Reading the repo first

The session's biggest single point of failure has been **designing without
reading the existing code first**.  Early sessions diverged from repo
conventions (wrong YAML schema, wrong plugin patterns, wrong undo flow)
until Gravio prompted a repo audit, after which everything was rewritten
to match real codebase structure.

**Always read the actual repo before designing.**  Specifically:

- For curation operations: read `klustersdoc.{h,cpp}` and `data.{h,cpp}`
  before designing the operation.  Find a sibling operation (recluster,
  watershed) and follow its shape.
- For YAML schema changes: read `parameteryamlreader.cpp` to find the
  current schema.  Ask Gravio if uncertain — schema changes affect
  multiple consumers.
- For plugin additions: read a sibling plugin's `CMakeLists.txt`,
  argument-parsing block, and main flow.  Don't invent a new pattern.
- For ProgressBar additions: the modernized version (post-audit) is
  in `src/libklustersshared/src/klustersshared/progressbar.{h,cpp}`.
  Use bare member names, `/dev/tty` output channel, the `setFailed()`
  API for failure marking.

---

## 9. Verification gates before shipping

Before shipping any patch series, verify:

1. **Build**: `g++ -std=c++20 -fsyntax-only -Wall -Wextra -Wshadow ...`
   on the changed source files (§2.9).  Zero warnings.
2. **Convention**: `grep -n 'm_[a-zA-Z]' <changed-files>` returns only
   false positives (script names like `ndm_pca` in comments, tool names
   in strings).
3. **Domain invariants**: if the change touches curation, undo, spike
   files, or PCA — sanity-check that the relevant invariant from §3
   still holds (read the change, not the test suite).
4. **Tests**: build with `-DNS_BUILD_TESTS=ON` and run `ctest` for the
   area touched, plus the ndmanager-plugins script tests it covers
   ([tests/README.md](../../tests/README.md)).  Where Qt is missing, run the
   Qt-free set and say which tests could not be built.
5. **Patch series**: it applies with `git am` onto a fresh checkout of the
   base, the resulting tree matches the working clone, and each patch holds
   only its own concern (§4.2).
6. **AUDIT.md / DEDUPE.md / per-topic change notes**: present and accurate
   if the change is non-trivial.  The single canonical project changelog is
   `CHANGELOG.md` (§2.8) — record user-visible changes there, not in a new
   root file.

---

## 10. End-of-session recap pattern

When wrapping up, summarize:
- What shipped (patch numbers and subjects).
- What changed (concrete bullets).
- What was deferred (with reasons).
- What's blocked / awaiting verification.

This goes in the chat reply, not in a patch.  Memory will pick up the
key facts on next session.

---

*This document is itself maintained.  When a session reveals a new
anti-pattern, convention, or invariant worth remembering, append a section
or update §6.  This is a working document, not a fixed spec.*
