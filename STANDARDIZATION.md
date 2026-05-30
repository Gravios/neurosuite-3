# neurosuite-3 standardization prompt

This document is what I (Claude) wish I'd had at the start of every session
working on `github.com/Gravios/neurosuite-3.git`.  It's a checklist + reference
that prevents drift, miscalibration, and re-litigation of decisions already
made.  Read it first before designing or shipping any change.

---

## 1. Project map

`neurosuite-3` is a Qt6/C++20 modernisation of the Neurosuite electrophysiology
toolchain.  The repo contains six tightly-coupled subsystems:

| Subsystem | Path | Role |
|---|---|---|
| **klusters** | `src/klusters/` | interactive spike-sorting GUI (Qt6, ~30K LOC) |
| **kiloklustakwik** | `src/kiloklustakwik/` | GPU-accelerated CEM clustering engine, formerly the experimental KlustaKwikExp fork (CUDA primary, HIP/SYCL stubs) |
| **ndmanager** | `src/ndmanager/` | session management GUI |
| **ndmanager-plugins** | `src/ndmanager-plugins/` | preprocessing plugins (CLI tools + bash wrappers) |
| **neuroscope** | `src/neuroscope/` | trace viewer GUI |
| **libklustersshared** | `src/libklustersshared/` | shared library (Qt6 + yaml-cpp + KDE-port widgets) |

`libklustersshared` is consumed by all three Qt apps (klusters, ndmanager,
neuroscope) and provides shared widgets (DockArea, ZoomWindow), color
management (ItemColors, ChannelColors), schema I/O (ParameterYamlReader/
Writer/Modifier), and the progress bar used by ndmanager-plugins CLI tools.

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

### 2.4 Cluster colours

Hue formula: `hue = (clusterId * 7) mod 36) * 10`, `sat = 200`, `val = 255`.
Don't change this; the visual continuity across sessions depends on it.

### 2.5 Curation log primitives

`KlustersDoc::applyClusterRename(partial, fullOptional)` is the single
primitive for renumbering operations.  All curation actions
(dipsplit, watershed, basin labelling, undo/redo) go through it or
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

`peakPositionInWaveform` is **1-based internal**, **0-based YAML**.
Conversion sites:
- YAML reader subtracts 1 on load
- YAML writer adds 1 on save
- Internal code uses 1-based throughout

Don't off-by-one.

### 3.3 File formats (binary, little-endian)

| File | Header | Body |
|---|---|---|
| `.res.N` | none | int64 timestamps |
| `.spk.N` | none | int16 sample-major waveforms |
| `.spkD.N` | none | int16, full nCG channels (stderiv in-place, no channel dropped) |
| `.clu.N` | int32 (count) | int32 cluster IDs |
| `.fet.N` | int32 (nCols) | int32 features, row-major |
| `.fetD.N` | int32 (nCols) | int32 features, `(nChan-1)*nComp+1` cols (last linearly-dependent channel dropped before PCA in `process_pca_stderiv`) |
| `.col.N` | 32B + 32B params | template table + record table |

### 3.4 KiloKlustaKwik gotchas

- `MergeThresh` must be calibrated to `chi²(nSpatialDims, 0.99)` — the
  default `200` is catastrophically large for typical feature dimensionality.
- `UseFeatures` default is `"all"`; the legacy fixed-length default string
  caused session duration to compute as 0, bypassing chunked CEM entirely.
- `probabilities` must be initialised to `nullptr` in the constructor to
  avoid spurious delete on garbage pointer at first thread completion.
- GPU shared memory: query `sharedMemPerBlock` at allocate time; fall back
  to global atomics kernel when shared buffer would overflow.  RTX 5070 Ti
  has 128 KB shared memory per block; pre-Blackwell limit is 48 KB.
- `pickInputPath(out, outSize, base, ext, elec)` is the canonical helper
  for `.fet`/`.fetD` (and other dual-extension) lookups.  Tries
  `<base>.<ext>.<elec>` first, falls back to `<base>.<ext>D.<elec>`.

### 3.5 NTFS / fuseblk caveat

`HDF5_USE_FILE_LOCKING=FALSE` must be set **before HDF5 library initialisation**
— before `H5Eset_auto2`, not at `H5Fopen` call.  Reference data path
`/data` is on a Samsung 9100 PRO 8TB at NTFS/fuseblk; locking will fail
otherwise.

### 3.6 Reference data

Primary test session: `jg05-20120316`, group 7.  Secondary: `eb05-20251118`,
group 25.  Use these for any change that needs end-to-end verification.

---

## 4. Workflow

### 4.1 Iterative tarball pattern

The session loop is:

1. Gravio describes a goal (often terse: a paste of build log, runtime
   failure, or a one-line ask).
2. Claude diagnoses, designs, and stages changes in
   `/home/claude/release/<topic>/` mirroring repo structure.
3. Claude builds a **cumulative drop-in tarball** (`tar czf` with full
   paths from repo root) into `/mnt/user-data/outputs/`.
4. Claude ships via `present_files`.
5. Gravio extracts directly into the source tree (`tar xzf` from repo root).
6. Gravio reports build errors / runtime failures tersely.
7. GOTO 2 with version bump (`v2`, `v3`, ...).

Each tarball is a **complete drop-in for that topic** — not a delta.  Later
versions supersede earlier ones.  Files inside the tarball use the same
relative paths as the repo, so `tar xzf` lands them in the right place.

### 4.2 Tarball discipline

- Filename: `<topic>-<descriptor>[-vN].tar.gz` (e.g. `klusters-dipsplit-postcommit-v3.tar.gz`).
- Contents: **only files actually changed** (and a `<TOPIC>.md` doc explaining
  the drop, if non-trivial).
- Don't include unmodified files.  Don't include build artefacts.  Don't
  include `.gitignore`'d output.
- Use `--owner=0 --group=0` to avoid permission noise on extraction.

### 4.3 Reference clone discipline

Maintain a fresh clone alongside the working copy:
- `/tmp/ns3-fresh/` or `/tmp/neurosuite-3-fresh/` — fresh clone at HEAD,
  used for auditing / diff baselines.
- `/home/claude/release/<topic>/` — working copy, where changes are staged.

When verifying that a change preserves behaviour, diff the working copy's
file against the fresh clone's, ideally after normalising whitespace and
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
- Tarballs of source changes
- Generated docs (e.g. AUDIT.md, CHANGES.md when produced)
- Test reports if requested

Not:
- Intermediate analysis files
- Throwaway demos
- Logs from build attempts

Use `/home/claude/` (not `/mnt/user-data/outputs/`) for everything else.

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
materialise.

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

### 6.4 `peakPositionInWaveform` off-by-one

Internal code uses 1-based; YAML uses 0-based.  Conversion is at the I/O
boundary.  Easy to forget; sanity-check with a sample value when introducing
new code that touches this.

### 6.5 ndmanager-plugins parent CMakeLists

The parent `src/ndmanager-plugins/src/CMakeLists.txt` is intentionally not
touched when a working copy already has functional `process_*` builds.
Don't add new `add_subdirectory()` calls there as part of an unrelated
change.

### 6.6 `pca.nCh <= 64` reader bound

In `klustersdoc.cpp`, the PCA basis reader silently invalidates basis
when `nCh > 64`.  This breaks Neuropixels-class probes.  Known issue,
not yet fixed; don't replicate the bound in new code.

### 6.7 Scope creep in audits

When asked to audit "X", Claude tends to also fix Y and Z that look related.
Don't.  Audit X only.  File Y and Z as deferred items in the AUDIT.md.

---

## 7. Tooling

### 7.1 Analyzer scripts

- `scripts/audit/find_dead_and_dupes.py` — AST-based dead-code and
  clone analyser.  Tree-sitter-cpp, ~600 LOC.  Set `ROOT` to the
  scope you want.  Outputs to stdout.  Caveats are documented in
  the report header (Qt SLOT/SIGNAL invisibility, template
  instantiation gaps, etc.).

### 7.2 Test harnesses

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
- For ProgressBar additions: the modernised version (post-audit) is
  in `src/libklustersshared/src/klustersshared/progressbar.{h,cpp}`.
  Use bare member names, `/dev/tty` output channel, the `setFailed()`
  API for failure marking.

---

## 9. Verification gates before shipping

Before shipping any tarball, verify:

1. **Build**: `g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wshadow ...`
   on the changed source files.  Zero warnings.
2. **Convention**: `grep -n 'm_[a-zA-Z]' <changed-files>` returns only
   false positives (script names like `ndm_pca` in comments, tool names
   in strings).
3. **Domain invariants**: if the change touches curation, undo, spike
   files, or PCA — sanity-check that the relevant invariant from §3
   still holds (read the change, not the test suite).
4. **Tarball contents**: list with `tar tzf` and confirm only changed
   files are included (no stale staging detritus).
5. **AUDIT.md / CHANGES.md / DEDUPE.md**: present and accurate if the
   change is non-trivial.

---

## 10. End-of-session recap pattern

When wrapping up, summarise:
- What shipped (tarball name, file count, replaces previous version if any).
- What changed (concrete bullets).
- What was deferred (with reasons).
- What's blocked / awaiting verification.

This goes in the chat reply, not in the tarball.  Memory will pick up the
key facts on next session.

---

*This document is itself maintained.  When a session reveals a new
anti-pattern, convention, or invariant worth remembering, append a section
or update §6.  This is a working document, not a fixed spec.*
