# Klusters source — naming and clarity audit

**Scope.** This audit covers the 25k LOC of `src/klusters/src/` plus
`src/libklustersshared/`.  It catalogs naming inconsistencies and
clarity issues found across the codebase, sorts them by severity and
fix-cost, and proposes the changes worth making versus the ones to
leave alone.

**Method.** Pattern-matched declarations and identifiers across all
17 source files; counted occurrences; grouped by issue class.

## Findings

### A. Member-prefix inconsistency (HIGH severity, MEDIUM effort)

Two conventions coexist in the codebase:

| Convention | Where | Approximate count |
|---|---|---|
| Bare camelCase (Qt-style) | All pre-2026 code in `data.{h,cpp}`, `klusters.{h,cpp}`, `klustersview.cpp`, `clusterview.{h,cpp}`, `clusterPalette.cpp` | ~1500 member references |
| `m_` prefix (post-2026 code) | `curationlogger.{h,cpp}`, watershed/dipsplit/curation extensions in `klusters.{h,cpp}`, `klustersdoc.{h,cpp}` | ~370 references |

The `m_` prefix entered the codebase via members I added during the
2026 work (curation logger, watershed live-preview, dipsplit live-
preview, pending-file infrastructure, etc.).  The original Lynn
Hazan code uses bare camelCase consistently.

**Recommendation.** Standardise on **bare camelCase** to match the
existing 80% of the code.  This is the Qt convention, matches every
QObject example in the Qt docs, and is what new contributors will
already expect.

The `m_` prefix's only real benefit — disambiguating member names
from local-variable shadows in long methods — is a non-issue here
because Klusters consistently shadows members through getter calls
(`clusteringData->`, `view->`, etc.) and the methods are not unusually
long.

**Effort.** ~6 mechanical renames, each ~5–60 sites: safe sed sweeps
once the names are confirmed not to collide with existing locals.
Recommend deferring until a quiet stretch — risk is low but not zero.

### B. Doxygen typo cluster (LOW severity, LOW effort)

* `absciss` → `abscissa`: 13 sites in `data.h`, `klusters.h`,
  `klustersdoc.h`, `klustersview.h`.  Comment text only.
* `Lenght` → `Length`: 2 member names + 2 references in `data.{h,cpp}`
  (`RMSIntWindowLenght`, `windowLenghtToRealign`).  These are real
  identifier typos, not just docs.
* `futur` → `future`: a few comment instances in `klustersdoc.h`.

**Recommendation.** Fix all three in this audit pass — they are
unambiguous improvements and don't cross any abstraction boundary.

### C. Single-letter / opaque local variables in algorithmic code

`klustersdoc.cpp` and `data.cpp` contain a number of single-letter
locals in algorithm bodies (`k`, `r`, `s`, `D`, `R`, `i`, `j`).  Most
are in tight loops or short helper lambdas where a longer name would
add noise without information (loop counters, iterator-style vars,
short-lifetime decision/result aliases).

**Recommendation.** Leave alone.  The clarity benefit of renaming
`R` → `result` in a 6-line lambda is negative.

### D. Adjacent declarations with shared prefix

The pending-files block in `klustersdoc.h`:

```cpp
QString m_origSpkPath;     QString m_pendingSpkPath;
QString m_origResPath;     QString m_pendingResPath;
QString m_origFetPath;     QString m_pendingFetPath;
                           QString m_pendingCluPath;
```

These are clear, well-grouped, and the two-prefix pattern is
intentional.  No action needed.

### E. Boolean naming conventions

Twenty-five boolean members lack an `is/has/can/should/will/was` prefix.
Most are valid English adjectives or past participles in their domain
context (`accepted`, `dataAvailable`, `polygonClosed`, `clusterModified`,
`processFinished`, `realignRunning`, `errorMatrixExists`,
`templateMatrixExists`, `useWhiteColorDuringPrinting`, `autoSelectFeatures`).

The two genuine outliers are mine: `m_wsActive` and `m_dipActive` —
better named `m_wsPreviewActive` / `m_dipPreviewActive` since they
specifically gate preview-mode UI flow.

**Recommendation.** Rename only the two preview-mode flags.  Leave
the domain booleans alone — renaming `accepted` to `isAccepted` adds
noise without clarity.

### F. `T` palette-focus event filter and `Z` zoom: documented elsewhere

The four characters `T`, `S`, `PageUp`, `PageDown` have palette-
context-only behaviour intercepted in the qApp event filter rather
than as QAction shortcuts.  This is documented at the intercept
site and again in `doc/klusters/README.md`.  Naming-wise,
`paletteHasFocus()` is a clear predicate.  No action needed.

### G. C-style casts (LOW severity)

12 instances across 4 files.  Most are in narrow numeric-conversion
spots (`(int)x`, `(long)y`).  They're not unsafe but they're not
the C++ idiom.

**Recommendation.** Replace opportunistically when touching surrounding
code, not as a sweep.

## Action plan for this audit pass

To keep the diff reviewable and the risk low, this pass shipped only
the unambiguous fixes:

1. **Spelling**: `absciss` → `abscissa` in 13 doc-comments;
   `Lenght` → `Length` in 4 sites (2 members + 2 refs); `futur` →
   `future` in 7 comments.
2. **Boolean clarity**: `m_wsActive` → `m_wsPreviewActive`,
   `m_dipActive` → `m_dipPreviewActive`.

## Phase 2: `m_` prefix homogenisation (completed)

A second cumulative tarball stripped the `m_` prefix from every
member it had been applied to (54 distinct names, 430 references
across 10 files).  Klusters now uses bare camelCase for member
fields uniformly.

Three local-disambiguation moves were needed before the bulk strip:

| Was | Now | Reason |
|---|---|---|
| `m_actionIdx` (counter) | `nextActionIdx` | `PendingEntry::actionIdx` (per-entry slot) is the natural target name; the counter takes a different name to make the read/write semantics explicit at every call site. |
| `QString out` (local in `jsonEscape`) | `QString escaped` | Was shadowing what is now `CurationLogger::out`. |

After these two preconditions, every other rename was a clean
strip with no shadowing.  Verified post-sweep: zero `m_`-prefixed
identifiers anywhere in `src/klusters/` or `src/libklustersshared/`,
all braces balanced, no partial-identifier mismatches.

The `CurationLogger::open()` parameters were also renamed to
`baseName` and `groupId` (from the previous longer names that
collided 1:1 with members) so the parameter list reads naturally
when the prefix is gone.

## Phase 3: function-and-class doc-comment audit (completed)

Scanned all 220 public declarations across 7 headers.  121 had
existing doc comments (55%); 99 did not.  Triaged by severity:

- **Trivial** (one-line getters/setters): 84.  Documented by name; no
  action.  Adding `@param x x to set` to `void setX(T x)` adds noise
  without information.
- **Slot-style** (`slotXxx` whose verb is unambiguous from name): 9.
  Examples: `slotCloseDocument`, `slotShowToolBar`, `slotZoom`.
  Already covered by the action they wire to.  No action.
- **Complex / non-obvious**: 6.  These were the real gaps and got
  doc comments in this pass.

### Stale doc-comment fixes (signature drift)

The pattern-match also flagged 7 `@param` references whose names
don't appear in the corresponding signature — i.e. the param was
renamed or removed but the doc-comment wasn't updated.  Each of
these is a documentation bug, fixed in this pass:

| Site | Stale ref | Fix |
|---|---|---|
| `clusterview.h:print()` | `@param metrics` | Replaced with the actual `width`/`height` params |
| `clusterview.h:paintEvent()` | `@param p` | Rewrote to describe what paintEvent actually does |
| `clusterview.h:eraseTheLastDrawnLine()` | `@param polygonColor` | Removed; rewrote to describe overdraw mechanism |
| `clusterview.h:eraseTheLastMovingLine()` | `@param polygonColor` | Same |
| `klusters.h:widgetAddToDisplay()` | `@param docWidget` | Removed (no such param) |
| `klusters.h:slotProcessExited()` | `@param process` | Replaced with explanation that signature matches Qt's `QProcess::finished` signal |
| `klustersdoc.h:realignSpikes()` | doc-block was sitting **above the wrong function** (`nudgeClusterTimestamps`) | Reordered so each function has its correct doc-block; expanded `realignSpikes` to document the four optional out-parameters that had no doc |

### Class-level doc additions

Three classes lacked an overview comment:

- `ClusterPalette` (`clusterPalette.h`) — added 13-line summary
  describing the dock widget's role, palette interactions, and the
  S/T/PageUp/PageDown event-filter intercepts.
- `Waveforms::WaveformStatus` (`data.h`, nested) — added 6-line
  summary describing the per-cluster cache flag tuple.
- `Waveforms::WaveformData<T>` (`data.h`, nested) — already had a
  comment; false-positive in the audit script.

### Q_SIGNALS section overview

`KlustersDoc` declares 23 signals.  Adding a doc-comment to each
would balloon the header without much value because signal semantics
are conventionally conveyed by the slots they connect to, not by
docs on the signal itself.  Instead, added a single block-comment at
the head of the `Q_SIGNALS:` section explaining the naming convention
(past-tense for completed mutations, undo*/redo* for reversion paths)
and a note about the legacy non-const-reference parameters.

### Spelling

One remaining typo cluster fixed: `immediat` → `immediate-update mode`,
`buton` → `button`, in `klustersdoc.h:singleColorUpdate`.

### Deferred

Trivial getter/setter documentation is **not** a goal — the codebase's
many one-line accessors stay self-documenting.  Adding `@brief Returns
the X` to `int getX() const { return x; }` adds noise without
information.  Future audit passes should keep this triage discipline.

## Phase 4: C-style cast → static_cast sweep (completed)

Item G in the original audit findings called out 12 C-style casts to
"replace opportunistically when touching surrounding code, not as a
sweep."  After phases 1–3 settled the codebase, a deliberate sweep
became low-risk.  Re-scanned with a tighter regex covering all
primitive type targets (`int`, `long`, `short`, `float`, `double`,
`size_t`, `qint64`, `dataType`, `int64_t`, `uint`, `uchar`, `ushort`,
`unsigned`, `char`); found 23 sites in code (the original audit's
12 was an undercount because its regex missed some primitive types).

Concentrated in two files: `data.cpp` (binary file I/O loops in
`load*` methods, line 250s–340s) and `klustersdoc.cpp` (a couple of
narrowing casts in display logic and waveform-energy summation).

### Translation rules

Every C-style cast was converted to `static_cast<T>(expr)`.  No
`reinterpret_cast` or `const_cast` were needed — every cast in the
codebase was a value-conversion (widening, narrowing, or signed/
unsigned reinterpretation in arithmetic).

### Verification

- All 19 source files re-checked: brace-balanced.
- Final scan shows **zero remaining C-style casts** of the targeted
  types anywhere in `*.cpp`.
- The targeted types include all primitive-type spellings; `(void*)`-
  style casts in C-API interop are not in this scope and remain.
- No behavioural change: `static_cast<T>(expr)` and `(T)expr` produce
  identical machine code for primitive type conversions.

