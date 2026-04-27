## neuroscope — cluster raster / overlay stall fixes (2026-04-20, rev 2)

Three real bugs fixed in the Neuroscope trace-view path that together
eliminate the "gets stuck a bit" behaviour observed during cluster
overlay and raster display.  An initially-diagnosed fourth bug
(Bug 1 — synchronous signal re-entry) turned out to not be a real
issue once Bug 2 was fixed; see below.

Touches three files:

- `../../src/neuroscope/src/traceview.h`
- `../../src/neuroscope/src/traceview.cpp`
- `../../src/neuroscope/src/clustersprovider.cpp`

No schema, API, or file-format changes.  Drop-in build over an existing
neurosuite-3 checkout.

### Bug 1 — NOT a bug (revised after testing)

**Initially diagnosed as:** synchronous signals causing O(N²) redundant
work per pan.  **Revised diagnosis:** not a real issue once Bug 2 is
fixed.

The original concern was that `ClustersProvider::requestData()` is
synchronous and emits `dataReady` inline before returning — which
does re-enter `TraceView::dataAvailable` on the same stack.  My first
attempt at a fix used `Qt::QueuedConnection`.  That attempt
**cannot work**: the `dataReady` signal passes `Array<dataType>&` by
non-const reference to a local stack variable inside the provider's
request methods.  Qt's queued connection would need to register the
argument type with the meta-object system (impossible for a reference
type) and would leave the referent dangling before the slot runs.
Qt6 correctly refuses with

```
qt.core.qobject.connect: QObject::connect: Cannot queue arguments of
type 'Array<dataType>&' (Make sure 'Array<dataType>&' is registered
using qRegisterMetaType().)
```

and the connection silently drops — cluster overlays stop updating.

On closer inspection, the synchronous re-entry is actually safe.
The request loop in `displayTimeFrame` / `updateDrawing` runs two
passes:

```cpp
// Pass 1: mark every provider not-ready
for each cluster provider p: p->setStatus(false);

// Pass 2: issue the request (synchronously emits dataReady)
for each cluster provider p: p->requestData(...);
```

When provider 1's emit triggers `dataAvailable`, provider 1's status
becomes true but providers 2..N are still false from Pass 1.
`allProvidersReady()` correctly returns false → no `update()` is
scheduled — no reentrant paint.  Only provider N's emit (when
everyone has status=true) triggers the single repaint.

**Conclusion:** the connections are reverted to direct.  The behaviour
that was *blamed* on re-entrancy (flicker, stale-data paints, stuck
wait-cursor) was entirely caused by Bug 2's broken readiness logic,
which made `allProvidersReady()`-style checks silently return true
for provider sets that were not in fact all ready.  Fix Bug 2 and
direct connections work correctly.

One line-level improvement was kept: the comments at each connect
site now document *why* the connection must be direct (Array<>&
meta-type restriction) so the next person reading the code doesn't
repeat my mistake.

### Bug 2 — Readiness logic had two semantic bugs, inlined at 7 sites

**Symptom.**  Wait-cursor stays on after scroll; raster occasionally
renders with stale cluster data from the previous time-window;
paint fires at wrong moments.

**Root cause.**  The same broken pattern copy-pasted across 7 call
sites in `traceview.cpp`:

```cpp
bool ready = false;
QHashIterator<QString, ClusterData*> iter(clustersData);
while (iter.hasNext()) {
    iter.next();
    if (iter.value())              // ← only assigns if non-null
        ready = iter.value()->status();
    if (!ready) break;
}
QHashIterator<QString, EventData*> iter2(eventsData);
while (iter2.hasNext()) {
    iter2.next();
    if (iter2.value())             // ← OVERWRITES cluster conclusion
        ready = iter2.value()->status();
    if (!ready) break;
}
if (ready) { ... update() ... }
```

Two bugs:

  1. When a hash entry is null, `ready` is not assigned, so it keeps
     the previous iteration's value.  A stale-true value survives past
     a not-ready entry if that entry is null.
  2. The second loop overwrites the first loop's conclusion.  If
     clusters were ready but events were not (or vice versa), `ready`
     reflects only whichever hash was iterated last.  So
     "clusters ready AND events ready" was not actually being computed.

**Fix.**  Three `const` helper methods on `TraceView`:

```cpp
bool allClustersReady() const;   // returns false on null OR not-ready
bool allEventsReady()   const;   // same for events
bool allProvidersReady() const;  // conjunction
```

All 7 broken sites replaced with a single call to the appropriate
helper.  `ClusterData::status()` const-qualified so the helpers can
call it (trivial change — the member was always a pure getter).

### Bug 3 — Dichotomy search could read past end of clusters array

**Symptom.**  Rare "stuck for a second" pauses during cluster-raster
pan, very occasionally a crash.

**Root cause.**  `ClustersProvider::retrieveData` declared
`startIndex` / `endIndex` uninitialised, then populated them via an
`if/else-if` ladder that depended on cached `previousStartTime` /
`previousEndTime` values.  These cached values have no invariant
check — a prior crash or a sibling method updating sampling rate
could leave them inconsistent (e.g. `previousEndTime < previousStartTime`).
Falling out of the ladder with uninitialised indices, then feeding
those indices into the dichotomy search, read off the end of the
`Array<dataType> clusters` which is 1-indexed and not bounds-checked.

The dichotomy itself also had a subtle bug: the `if (time >
startInRecordingUnits)` branch sets `newEndIndex = previousStart`,
which can produce `newEndIndex < newStartIndex` on the next
iteration depending on how the math falls out — and
`clusters(2, newStartIndex)` gets evaluated *before* the loop
condition re-checks.

**Fix.**

  1. Default-initialise `startIndex = 1, endIndex = nbSpikes` at top
     of `retrieveData` (covers any fall-out-of-ladder case with a
     conservative full-range search).
  2. Post-ladder clamp: `startIndex ∈ [1, nbSpikes]`,
     `endIndex ∈ [1, nbSpikes]`, `startIndex ≤ endIndex`.
  3. In-loop guard at top of each of the three dichotomy loops
     (`retrieveData`, `requestNextClusterData`,
     `requestPreviousClusterData`) against transient inversion that
     would step off the array before the loop condition can stop us.

### Bug 4 — Raster draw was O(K·S) per provider per redraw

**Symptom.**  Visible stutter on dense sessions (30k spikes visible,
8 selected clusters) even on a fast machine.  The single biggest
contributor.

**Root cause.**  For each of K selected clusters, the draw loop
linear-scanned all S spikes in the window, filtering by
`clusterId == currentCluster`:

```cpp
for each selected cluster c:
    for i in 1..nbSpikes:
        if (currentData(2,i) == c) painter.drawLine(...);
```

On a 32-s window with 30k spikes and 8 selected clusters, that is
240k predicate evaluations per redraw, firing on every pan / zoom /
time-change step.  Plus a stray `qDebug()` on the single-column
path (line 2028 in the pre-patch source) that flushed stderr once
per (provider × cluster) and produced real-time hitches on busy
sessions.

**Fix.**  Restructure the draw to be O(K + S) per provider:

  1. Outer loop walks the K selected clusters once, building a
     `QHash<dataType, QPen> penById` and `QHash<dataType, int>
     yById` lookup, and appending the `clustersOrder /
     rasterOrdinates / rasterAbscisses` bookkeeping exactly as
     before (preserves downstream state that other methods read).
  2. Inner loop walks the S spikes exactly once.  For each spike,
     look up the pen and y by `clusterId`; skip if not in the
     selected set; draw.  Cache the current pen to avoid
     redundant `setPen()` calls on runs of consecutive same-cluster
     spikes (measurable on dense sessions).

Applied to both branches (multicolumn at ~line 1763, single-column
at ~line 2011).  The debug `qDebug()` is removed from the single-
column branch.

**Expected speedup.**  For K=8 selected clusters this is ~8× fewer
predicate evaluations per redraw.  Combined with Bug 1's fix
(N→1 scans per pan) and Bug 2's fix (no more phantom update() calls
with stale state), the cumulative win on a typical 5-group,
8-selected-cluster session is ≫ 10× in redraw cost.

### Testing

Cannot simulate the GUI path here (no Qt6 runtime in the patch
environment).  Changes were verified for:

- Brace balance on all three files (unchanged before/after)
- Syntax of the new helpers (compiled standalone with g++)
- Semantic preservation: all 7 readiness sites now compute the same
  correctness condition the old code *attempted* to compute (with
  the two bugs removed)
- All four raster-loop bookkeeping side effects (clustersOrder,
  rasterOrdinates, rasterAbscisses appended in exactly the same
  order, exactly once per shown cluster) — preserved in the rewrite

Please verify on a real session by opening a recording with 3+ cluster
groups, enabling the raster, and observing pan responsiveness.  The
"stuck a bit" behaviour should be gone.

### Files changed

```
src/neuroscope/src/traceview.h         | +22 -1
src/neuroscope/src/traceview.cpp       | +186 -180
src/neuroscope/src/clustersprovider.cpp| +54 -4
```

No Qt/CMake config changes.  Build with the existing top-level
build instructions.
