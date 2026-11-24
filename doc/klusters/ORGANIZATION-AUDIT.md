# Klusters source — legibility and organization audit (2026-11-24)

**Scope.** `src/klusters/src/` (128 `.cpp`, 130 `.h`, 114,363 lines) and how its tests and
internal documents are arranged. The April 2026 audit
([CODING-STYLE-AUDIT.md](CODING-STYLE-AUDIT.md)) settled names, casts and the `m_` prefix when the
tree was 25k lines; this one asks whether the code can still be read and found at 4.5 times that.

**Method.** Measured, then read where the numbers pointed. Function metrics with `lizard`
(`scripts/audit/function_metrics.py`); unused functions and duplicated bodies by a textual pass
(`scripts/audit/dead_and_dupes_textual.py`); the rest by `grep` counts given with each finding.
State: 943e7d9 plus the patches ns-0042 to ns-0059.

**Nothing was changed.** This is the audit; each finding ends in what a fix would be.

**Limits.** The unused-function and duplicate lists are textual candidates, spot-checked, not
triaged one by one. Member counts come from regular expressions over the headers and are
approximate. `libklustersshared` and the other applications were not audited.

## Summary

| # | Finding | Evidence | Weight |
|---|---|---|---|
| 1 | Five functions are programs of their own | 6,356 lines, 8% of all function lines | high |
| 2 | Two classes hold half the application | `KlustersApp` 16.8k lines, `KlustersDoc` 15.8k | high |
| 3 | An action is described in four places | a median of 9 mentions in 3 files per action | high |
| 4 | The self-test harness is part of the application class | 1,271 lines; one 973-line dispatcher | medium |
| 5 | Comments tell the history, not the code | 27% comment lines; 309 narrate what used to be | medium |
| 6 | Two vocabularies for the same things | parent/child in code, fiber/atom in the manual; artefact and artifact | medium |
| 7 | Code that is not used or not built | 3 files outside the build; 388 candidate functions | medium |
| 8 | Parallel copies | the four matrix views; undo / redo twins | medium |
| 9 | Where things are | 258 files in one directory; undo in the hierarchy file; documents in two trees | medium |
| 10 | Small debts | spelling, stale `@param`, long lines, long parameter lists, one global | low |

What is in good order: no `m_` member and no C-style cast anywhere in the tree; 2 `TODO` /
`FIXME` lines in 114k; the 31 UI state names are each handled and each used (no orphan, no
typo); the median function is 6 lines; 27 unit-test programs and 36 scripted self-tests.

## 1. Five functions are programs of their own

3,655 functions; median 6 lines, 90th percentile 51. The tail is the problem: 173 functions
over 100 lines hold 45% of all function lines, 47 over 200 lines hold 24%.

| Lines | Cyclomatic complexity | Function |
|---|---|---|
| 2,085 | 349 | `KlustersDoc::realignSpikes` (`klustersdoc_realign.cpp`) |
| 1,368 | 54 | `KlustersApp::createMenus` (`klusters.cpp`) |
| 1,156 | 218 | `KlustersDoc::nudgeClusterTimestamps` (`klustersdoc_realign.cpp`) |
| 973 | 292 | `KlustersApp::selfTestRunStep` (`klusters_selftest.cpp`) |
| 774 | 132 | `KlustersApp::slotRecluster` (`klusters_recluster.cpp`) |
| 716 | 77 | `KlustersApp::registerInputBindings` (`klusters.cpp`) |
| 538 | 65 | `KlustersApp::slotStateChanged` (`klusters_state.cpp`) |
| 532 | 130 | `Data::computeSnapshot` (`data.cpp`) |
| 504 | 67 | `KlustersDoc::openDocument` (`klustersdoc_io.cpp`) |
| 478 | 90 | `TraceView::drawTraces` (`traceview.cpp`) |

- `realignSpikes` is one function with 7 local lambdas, 39 section banners and code indented
  16 levels deep. The banners are the function boundaries that were never drawn: choose the
  working space, open the files, compute the shifts, write the rows, mirror to the child layer,
  journal, log. It cannot be tested in parts, and it is why a realign cannot be cancelled or
  undone halfway (both asked for on 2026-11-24 and both stopped at this function).
- `nudgeClusterTimestamps` repeats much of it for the nudge.
- 84 functions have a cyclomatic complexity above 30, 20 above 60.
- 70 functions take more than 6 parameters; the constructors of `KlustersView` (28),
  `TraceView` (27), `TraceWidget` (27) and `WaveformView` (19) take the display settings one by
  one.

**Fix.** `realignSpikes` first: a `RealignPlan` (what will be written where) computed by one
function and applied by another, with the stages its banners name as functions. That is also the
precondition for a realign that can be undone. A settings `struct` for the four constructors.

## 2. Two classes hold half the application

| Class | Functions | Lines | Files | Header | Data members (approx.) |
|---|---|---|---|---|---|
| `KlustersApp` | 364 | 16,849 | 13 | `klusters.h`, 2,309 lines | 390, of them 168 `QAction*` |
| `KlustersDoc` | 383 | 15,840 | 16 | `klustersdoc.h`, 3,507 lines | 396 |
| `Data` | 312 | 7,692 | 2 | `data.h`, 2,878 lines | 246 |

Together 52% of all method lines.

- The implementation is already split by concern into files (`klusters_realign.cpp`,
  `klustersdoc_hierarchy.cpp`, 11 and 14 of them). The **state is not**: every one of those files
  sees all ~390 members of its class, so the file split says where code is, not what it may touch.
  The realign code reaches the recluster flags and the reverse; the edit lock of 2026-11-24 had
  to list eight unrelated flags by hand to know whether a job runs.
- `klustersdoc.h` is included by 41 files, `data.h` by 29, `klusters.h` and `klustersview.h` by
  27 each: a new member of the document recompiles a third of the tree.
- `Data` grants 16 `friend`s.

**Fix.** Turn the file split into objects, one concern at a time, each owning its members:
an undo history (the timeline, the groups, the depth — today spread over `KlustersDoc`, `Data`
and each view), a realign controller (the three lanes and their flags), a recluster controller,
the hierarchy model (the child layer and its maps). The document keeps them as members. The job
flags then become one question asked of the controllers.

## 3. An action is described in four places

`KlustersApp` has 168 `QAction` members. An action is created in `createMenus` (1,368 lines),
given a command id in `registerInputBindings` (716 lines), switched on and off in the lists of
`slotStateChanged` (538 lines), and declared in `klusters.h`; an icon and a default key are two
more tables. A member is mentioned a median of 9 times in 3 files; `mReCluster` 23 times.

- Adding a command means editing four places, and forgetting the state list is silent: that is
  how the Hierarchy menu and a dozen split commands stayed enabled during a recluster (the lock
  audit of 2026-11-24).
- The members follow three naming patterns: 117 `mName`, 23 `nameAction`, 22 neither.

**Fix.** One table of commands — id, menu, text, key, icon, slot, and whether it edits — from
which the menus, the registry and the busy lock are built. `createMenus`,
`registerInputBindings` and the per-state lists reduce to loops over it.

## 4. The self-test harness is part of the application class

`klusters_selftest.cpp` is 1,271 lines of `KlustersApp` member functions, compiled into the
shipped binary; `selfTestRunStep` is one 973-line chain of `if (step.contains(...))` with a
complexity of 292. Each new scripted step is another branch with full access to the
application's privates.

- 36 scripted self-tests exist; **6 were registered with `ctest`**
  (`src/klusters/test/CMakeLists.txt`) and the other 30 ran only when someone ran them by hand.
  *Done the same day:* every `test_*.py` is registered by being in the directory.
- Helpers are imported from one test file into another (`test_undo_pairing` from five others),
  so a test file is also a library.

**Fix.** A step registry (name → handler) in a class of its own that is a `friend` of the
application, one handler per step; the shared session builders in `session.py`; all self-tests
registered, with a label for the slow ones.

## 5. Comments tell the history, not the code

30,935 of 114,363 lines are comments (27%); `klustersdoc.h` is 61% comment, `data.h` 53%,
`klusters.h` 50%.

- 309 comment lines in 78 files narrate the past — "used to", "no longer", "the old path",
  dates, "this bug". A reader has to subtract the history to learn what the code does now, and
  the same account is already in `CHANGELOG.md`. The patches of 2026-11-24 added to it.
- The headers carry the design discussion inline. A declaration of one line under forty lines
  of rationale is hard to find, and the member list of a class can no longer be read as a list.
- 1,131 lines are longer than 120 columns, 140 longer than 160.

**Fix.** A rule for `STANDARDIZATION.md`: a comment says what the code does and what must stay
true; why it changed goes in the commit and the changelog. Move the long rationales from the
three big headers to `src/klusters/docs/` and leave a pointer.

## 6. Two vocabularies for the same things

- **Layers.** The manual and the menus say *fiber* and *atom*; the code says *parent* and
  *child*, and sometimes *atom*: in the application and document files, `child` 3,197 times,
  `parent` 2,429, `atom` 857, `fiber` 190. `mergeChildren` and "Merge Atoms" are one command.
- **Bins.** `artefact` 168 times, `artifact` 105, both in identifiers
  (`moveClustersToArtefact`, `deleteArtifact`).
- **Files.** `autoMerge.cpp` and `clusterPalette.cpp` are camelCase among 126 lowercase names.

**Fix.** Pick one word per thing and record it in `doc/GLOSSARY.md`. Renaming identifiers is a
mechanical patch per word; the manual and the code should not need a translation table.

## 7. Code that is not used or not built

- **Not in any `CMakeLists.txt`:** `klustersxmlreader.cpp` (589 lines),
  `parameterxmlmodifier.cpp` (244), `waveformcomparewidget.cpp` (160).
- **388 member functions (1,408 lines) whose name appears nowhere else in code.** Most are short
  accessors. The larger ones, spot-checked: `AutoMerge::computeProposalsByErrorMatrix` (97
  lines), `EventsProvider::loadData` (93), `KlustersDoc::rejectLastRealign` (50, which three
  status messages told the user to use until 2026-11-24), `Data::clusterEnvelopeOverlap` (31),
  `Data::saveClusters` (28), the whole `BinFile` reader and writer set in `binfile.h`.
- `src/klusters/docs/CONCURRENCY.md` already notes `stopAllViewThreads()` has no caller.

The list is textual: a function reached only through a string or a template would be on it
wrongly. `scripts/audit/dead_and_dupes_textual.py` prints it.

**Fix.** Delete the three files; triage the list largest first.

## 8. Parallel copies

- **The four matrix views** (`errormatrixview`, `templatematrixview`, `residualmatrixview`,
  `driftmatrixview`, 6,732 lines together) were copied from one another: `mouseReleaseEvent`,
  `drawClusterIds`, `effZoomMin` are identical between pairs; `paintEvent`,
  `launchComputeThread` and the three `connect…MatrixView` functions of `KlustersView` differ
  only in names. A fix to one (the click from the child view, 2026-11-24) has to be checked in
  four.
- **Undo and redo** are written twice wherever they exist: `Data`, `EventsProvider`,
  `ErrorMatrixView`, and the entry workers of the new undo history.
- 21 groups of functions share a structure and differ in names (834 lines).

**Fix.** A `MatrixView` base for selection, zoom, ids and the compute thread; one
`step(direction)` for each undo / redo pair.

## 9. Where things are

- **One directory, 258 files.** Views, document, data, threads, dialogs, preferences, GPU
  kernels and test support sit side by side; the `klusters_` / `klustersdoc_` prefixes are the
  only grouping.
- **The undo history is in `klustersdoc_hierarchy.cpp`** (16 functions: the timeline, the
  dispatch, the entry workers) while `klustersdoc_undo.cpp` holds the level functions. The child
  undo started there and the single history of 2026-11-24 was built on the spot.
- **`klusters.cpp` is 10,895 lines** after eleven concerns were moved out; what is left is 206
  functions: the menus, the event filter and 108 slots of every kind.
- **Documents live in two trees:** `doc/klusters/` (the manual) and `src/klusters/docs/` (five
  internal references, 2,126 lines, last changed 2026-11-12), plus `MIGRATION_NOTES.md` and
  `BUILD.md` beside them. `DOCUMENT_MODEL.md` §5.2 still says the child layer has its own
  undo stack and that the scope is switched to undo the other layer, and neither it nor
  `CONCURRENCY.md` knows the edit lock: the patches of
  2026-11-24 changed the code and the manual and did not touch them.
- `scripts/audit/find_dead_and_dupes.py` has its source root fixed to a `/tmp` path and needs a
  package that is not in the build environment.

**Fix.** Subdirectories by layer (`app/`, `doc/`, `data/`, `views/`, `jobs/`, `dialogs/`,
`prefs/`); the timeline moved to `klustersdoc_undo.cpp`; one home for the internal references
and a line in `RECIPES.md` naming which of them a change to undo, jobs or input must update.

## 10. Small debts

- **Spelling** (the list of `STANDARDIZATION.md` §5): 37 hits — `begining` 22, `wich` 3,
  `reciever` 3, `futur` 3, `abscisses` 3, `occured`, `existance`, `absciss`.
- **Stale `@param`:** 27 names that are not in the signature below them (`metrics` on three
  `print` functions, `wflags`, `docWidget`).
- **Boolean names:** of 183 `bool` members in the six main headers, about 89 are not
  predicate-style by a loose test (`reclusterRefine`, `nudgeInProgress` pass in spirit;
  `children`, `significant`, `on`, `off` do not).
- **A global:** `extern int nbUndo;` is declared in 15 files.
- **A trailing underscore** marks the self-test members only (`selfTestIndex_`), a third member
  style beside the bare and the `m`-capital ones.

## Order of work

1. ~~Register the 30 self-tests with `ctest`.~~ Done: `ctest -L selftest`, 36 tests, about
   three minutes.
2. Delete the three unbuilt files; triage the unused functions.
3. The command table (§3). The three functions it replaces are 2,622 lines, and it removes
   the class of fault the lock audit found.
4. Move the undo history into one object (§2, §9); update the two internal references.
5. Split `realignSpikes` into a plan and its application (§1). The largest single gain in
   legibility, and the way to a realign that can be cancelled and undone.
6. The matrix-view base (§8).
7. The glossary and the renames (§6), one word per patch.
8. Directories (§9), last: it touches every include and is cheap once the rest is settled.

Each is one concern and one patch series; none changes behaviour, so the existing tests are the
check.
