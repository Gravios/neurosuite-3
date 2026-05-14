# libklustersshared coding-style audit

## Scope

29 project-original files / ~4060 LOC across:

- `src/libklustersshared/src/klustersshared/`  (24 files)
- `src/libklustersshared/src/gui/dockarea.{h,cpp}`
- `src/libklustersshared/src/gui/klusterrubberband.{h,cpp}`
- `src/libklustersshared/src/gui/page/klusterseparator.{h,cpp}`

KDE-Libraries verbatim ports excluded (qextenddialog tower, qpageview,
qcolorbutton, qhelpviewer, qrecentfileaction, qextendtabwidget — these
are kept as upstream-fidelity ports).

## Five-phase pipeline

**Phase 1 — Spelling.** 5 fixes across 3 files:
- `zoomwindow.h`: 3× `absciss` → `abscissa` (in @param doc comments)
- `channelcolors.h`: 1× `existance` → `existence` (in @param doc)
- `itemcolors.h`: 1× `existance` → `existence` (in @param doc)

Common klusters-era misspellings (`Lenght`, `futur`, `buton`, `recieve`,
`occured`, `seperate`, `compatable`, `accessable`, `neccessary`,
`begining`, `comparaison`, `occurence`, `wether`, `wich`, `thier`,
`preceeded`, `alligned`, `indipendent`, `definately`, `transfered`,
`accomodate`, `occassion`) are all absent from this scope.

**Phase 2 — Boolean naming clarity.** Zero issues found.  All bool
members in scope already use predicate-style names (`isChanged`,
`colorChanged`, `valid`, `enabled`, `unicode`, `isTty`, `ttyFileOwned`,
`started`, `finished`, `failed`).

**Phase 3 — `m_` prefix homogenisation.** Project convention is **bare**
member names (no `m_` prefix), as established across `src/klusters/`
where ~30K LOC has zero `m_` usage on actual class members.  The
parameteryaml family in libklustersshared had been using `m_` —
inconsistent with the rest of the codebase — and was stripped.  The
progressbar files (recently rewritten in the modernisation drop) had
also used `m_`; same treatment.

Stripped from 8 files:
- `parameteryamlreader.{h,cpp}`
- `parameteryamlwriter.{h,cpp}`
- `parameteryamlmodifier.{h,cpp}`
- `progressbar.{h,cpp}`

Identifiers stripped: 5 in parameteryaml family (`m_root`, `m_valid`,
`m_doc`, `m_hipass`, `m_functions`); 15 in progressbar (`m_label`,
`m_step`, `m_total`, `m_done`, `m_barWidth`, `m_lastDrawnEighths`,
`m_unicode`, `m_isTty`, `m_ttyFile`, `m_ttyFileOwned`,
`m_nonTtyMilestones`, `m_started`, `m_finished`, `m_failed`).
Constructor params for `ProgressBar` were renamed `lbl`/`stp`/`tot` to
avoid `-Wshadow` warnings against the now-bare members.

Verified: clean compile under `-Wall -Wextra -Wshadow`.  Demo runtime
output identical pre/post strip (verified via PTY capture against a
script(1) harness, both ASCII and UTF-8 paths).

**Phase 4 — Stale `@param` documentation.** Zero stale doc/sig
mismatches in scope.  Several functions lack `@param` docs entirely
(parameteryaml getter family); not in audit scope.

**Phase 5 — C-style cast sweep.** Zero hits in scope.  Legacy code
(`itemcolors`, `channelcolors`, `zoomwindow`, `utilities`) is already
fully `static_cast`/`qobject_cast`-clean.

## Dead-code analyzer pass

Ran `find_dead_and_dupes.py` (AST-based) over the merged tree.  After
filtering to in-scope candidates, 13 functions had zero call-count.
Triage:

| Identifier | Where | Decision |
|---|---|---|
| `safeGet3` | `parameteryamlreader.cpp:78` | **Deleted.** Static helper, never used. |
| `copyData` | `array.h:163` (template) | Kept — utility primitive on foundational template class. |
| `createBackup` | `utilities.{h,cpp}` | Kept — public Utilities API; plausibly useful. |
| `deleteWidgets` | `dockarea.{h,cpp}` | Kept — public DockArea API, KDE-pattern surface. |
| `dockWidgetNames` | `dockarea.h:91` | Kept — public DockArea API. |
| `getChannelDisplayInfo` | `parameteryamlreader.{h,cpp}` | Kept — schema migration surface. |
| `getProgramParameter` | `parameteryamlreader.{h,cpp}` | Kept — schema migration surface. |
| `getShankIndex` | `parameteryamlreader.{h,cpp}` | Kept — probe-tab branch surface. |
| `readAnatomyGroupMeta` | `parameteryamlreader_probes.{h,cpp}` | Kept — probe-tab branch surface. |
| `setScalar` | `parameteryamlmodifier.{h,cpp}` | Kept — modifier API surface. |
| `showDockWidget` | `dockarea.{h,cpp}` | Kept — public DockArea API. |
| `widgetsByName` | `dockarea.{h,cpp}` | Kept — public DockArea API. |
| `writeAnatomyGroupMeta` | `parameteryamlreader_probes.{h,cpp}` | Kept — probe-tab branch surface. |

Net deletion: 1 function.  The 12 kept functions are part of intentional
API surface for in-flight migrations (probe-tab branch, ndmanager
schema modifier work).

## Files in this drop

13 files total:

```
src/libklustersshared/src/klustersshared/zoomwindow.h            (Phase 1)
src/libklustersshared/src/klustersshared/channelcolors.h         (Phase 1)
src/libklustersshared/src/klustersshared/itemcolors.h            (Phase 1)
src/libklustersshared/src/klustersshared/parameteryamlreader.h   (Phase 3)
src/libklustersshared/src/klustersshared/parameteryamlreader.cpp (Phases 3 + dead-code)
src/libklustersshared/src/klustersshared/parameteryamlwriter.h   (Phase 3)
src/libklustersshared/src/klustersshared/parameteryamlwriter.cpp (Phase 3)
src/libklustersshared/src/klustersshared/parameteryamlmodifier.h (Phase 3)
src/libklustersshared/src/klustersshared/parameteryamlmodifier.cpp (Phase 3)
src/libklustersshared/src/klustersshared/progressbar.h           (Phase 3)
src/libklustersshared/src/klustersshared/progressbar.cpp         (Phase 3)
AUDIT.md                                                         (this file)
```

## Verification commands after extraction

```bash
# 1. Confirm zero m_ prefix anywhere in libshared:
git diff --no-prefix -- 'src/libklustersshared/**/*.cpp' 'src/libklustersshared/**/*.h' \
  | grep -E '^\+.*\bm_[a-zA-Z]' | grep -vE 'ndm_|//|/\*' || echo "  clean"

# 2. Confirm safeGet3 removed:
git diff --no-prefix -- src/libklustersshared/src/klustersshared/parameteryamlreader.cpp \
  | grep -E '^-.*safeGet3' && echo "  removed"

# 3. Build with -Wshadow to verify no resurrected shadows:
cd build && cmake --build . -- -j$(nproc)
# Expect: no -Wshadow warnings on parameteryaml*, progressbar files.
```
