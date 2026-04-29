# neuroscope — full audit (2026-04-29)

Full audit of neuroscope at HEAD `fccc7d3` ("klusters done ... maybe").
Scope: `src/neuroscope/src/` only.  35,548 lines across 120 .cpp/.h
files; deepest dive on the four largest hot-path files (`traceview.cpp`,
`neuroscope.cpp`, `neuroscopedoc.cpp`, `clustersprovider.cpp`) plus
`tracesprovider.cpp`, `eventsprovider.cpp`.

This drop-in tarball fixes the bugs marked **P0** and **P1** below and
applies the **P2** optimisations on the dirtiest hot paths.  **P3**
items are documented as recommendations only — they are larger refactors
and would warrant their own focused passes.

Touches five files:

```
src/neuroscope/src/tracesprovider.cpp     | +24 -10
src/neuroscope/src/clustersprovider.cpp   | +51 -16
src/neuroscope/src/eventsprovider.cpp     | +33  -8
src/neuroscope/src/traceview.cpp          | +98 -55
src/neuroscope/src/traceview.h            |  +1  -1
```

No schema, build, or signal-API changes.  Drop-in over an existing
neurosuite-3 checkout.

---

## P0 — Real bugs (data corruption / wrong results)

### 1. `tracesprovider.cpp` — 32-bit resolution branch is fundamentally broken

**File / function.** `TracesProvider::retrieveData`, the
`else if(resolution == 32)` branch.

**Symptom.** Any session opened at `resolution = 32` reads garbage
into the trace buffer — every other byte from the file plus 6 bytes
of uninitialised memory per sample.  Traces would render as noise
(or a uniform line, depending on the offset and the residual stack
contents).

**Root cause.** The branch:

```cpp
Array<dataType> retrieveData(nbSamples, nbChannels);   // dataType = long → 8 bytes
qint64 nbValues = nbSamples * nbChannels;
qint64 position = static_cast<qint64>(...) * static_cast<qint64>(nbChannels);

dataFile.seek(position * sizeof(short));               // (a) seeks at 2× offset, lands mid-record
qint64 nbRead = dataFile.read(reinterpret_cast<char*>(&retrieveData[0]),
                              sizeof(short) * nbValues);  // (b) reads 2 bytes into 8-byte slot

if (nbRead != qint64(nbValues * sizeof(short))) { ... }  // (c) verifies wrong size
```

Every reference to `sizeof(short)` should be `sizeof(int32_t)`, and
the read buffer should be `Array<int32_t>` (mirroring the 16-bit
branch's `Array<short>` intermediate).  `computeRecordingLength()` in
the same file already uses `dataSize = 4` for `resolution == 32`, so
the file layout *is* 4-byte-per-sample — only the read path was wrong.

**Fix.**  Read into `Array<int32_t>`, seek/read with `sizeof(int32_t)`,
then convert to `dataType` (with offset subtraction if needed).
Pattern identical to the existing 16-bit branch.

### 2. `clustersprovider.cpp` — units-mismatch bug, two locations

**Files / functions.**

- `ClustersProvider::requestNextClusterData` (around line 612 in original).
- `ClustersProvider::requestPreviousClusterData` (around line 877 in original).

**Symptom.** Spike-browse-forward / browse-backward at exact-boundary
times occasionally lands on the wrong spike or no-ops when it should
advance.  Most visible when the user repeatedly hits next-cluster on
files with large `dataCurrentRatio`.

**Root cause.**  Both methods have an `else { ... if(startTime ==
previousEndTime) { ... } }` branch that compares a recording-unit
quantity against a millisecond quantity:

```cpp
if(clusters(2, previousEndIndex) < startTime) {        // ← startTime is ms
    startIndex = previousEndIndex + 1;
}
```

`clusters(2, n)` is in recording units (sample-clock).  `startTime` is
in milliseconds.  The earlier `retrieveData` does this correctly (it
uses `startInRecordingUnits` for the comparison).  Mirror the fix in
the two browse methods.

### 3. `clustersprovider.cpp` & `eventsprovider.cpp` — missing `else` in `if(startTime == 0)` block

**Files / functions.**

- `ClustersProvider::retrieveData` (line ~298)
- `ClustersProvider::requestNextClusterData` (line ~518)
- `ClustersProvider::requestPreviousClusterData` (line ~787)
- `EventsProvider::retrieveData` (line ~197)

**Symptom.** When `startTime == 0` and `previousStartTime > 0` (i.e.
the user navigated forward then jumped back to 0), the start-of-file
fast path runs first to set `startIndex = 1`, then *also* falls
through to `if(startTime < previousStartTime)` which clobbers
`startIndex` with `previousStartIndex / 2`.  Wasted work and
occasional off-by-some-spikes in the linear-search refinement.

**Root cause.**  The four-branch ladder is written without `else`
between the first two:

```cpp
if (startTime == 0) { ... startIndex = 1; ... }
if (startTime < previousStartTime) { ... startIndex = previousStartIndex/2; ... }
else if (startTime < previousEndTime && startTime > previousStartTime) { ... }
else if (startTime > previousEndTime) { ... }
```

The fix is one keyword: change the second `if` to `else if`.

### 4. `traceview.cpp` — multi-column "first non-skipped channel" lookup uses wrong index

**File / function.** `TraceView::mousePressEvent`, multi-column branch
(line ~2654 in original).

**Symptom.**  When the topmost channel of a group is in the skip list
and the user clicks in that group with mode == SELECT, the wrong
channel highlights and the wrong y-ordinate is used to choose the
nearest-trace.  The single-column equivalent at line ~2832 in the
same file is correct — they have drifted.

**Root cause.**  Two bugs in this 8-line loop:

```cpp
if (skippedChannels.contains(channelId)) {
    for (int i = 1; i < currentNbChannels; ++i) {
        if (!skippedChannels.contains(i)) {       // (a) tests i, not channelIds[i]
            channelId = channelIds[i];
            channelIndex = i + 1;
            break;
        }
        y -= Yshift;                              // (b) only runs when not breaking
    }
}
```

(a) The skipped list contains *channel IDs*, not loop-counter
positions.  The single-column variant correctly tests
`!skippedChannels.contains(channelIds[i])`.

(b) `y -= Yshift` runs at the *bottom* of the loop body, only when the
loop did *not* break.  So when the loop breaks at iteration `i`, `y`
is still at channel `channelIds[i-1]`'s ordinate, but `channelId` has
been advanced to `channelIds[i]`.  The subsequent `position = -y +
... - data(sampleIndex, channelId+1) * channelFactors[channelId]`
mixes y-from-channel-`i-1` with data-from-channel-`i`.  One-channel
ordinate offset error.

**Fix.**  Move `y -= Yshift` to the top of the loop body and replace
`skippedChannels.contains(i)` with `skippedChannels.contains(channelIds[i])`.

---

## P1 — Real bugs (functional / robustness)

### 5. `eventsprovider.cpp` — dichotomy lacks the bounds clamp added to `clustersprovider.cpp`

The 2026-04-20 raster fix added an in-loop bounds clamp on
`newStartIndex` / `newEndIndex` to the dichotomy in
`clustersprovider.cpp`; the parallel loop in `eventsprovider.cpp` did
not get the same treatment.  Same crash class (read past end of
`timeStamps` array) is theoretically reachable.  Mirror the clamp.

### 6. `traceview.h` — typo in `paintEvent` parameter name

`void paintEvent ( QPaintEvent*ainter) override;` — the parameter
name is `ainter` (missing 'p').  Harmless to behaviour (parameter
name is irrelevant in declaration) but obviously a slip.  The
implementation in `traceview.cpp` already has an unnamed parameter,
so just match: `void paintEvent ( QPaintEvent*) override;`.

### 7. `traceview.cpp` — operator-precedence ambiguity in mode-dispatch

`mousePressEvent`, line ~2533:

```cpp
if (mode == SELECT && !shownChannels.isEmpty() || mode == MEASURE || ... ) {
```

C++ correctly parses `&&` tighter than `||`, so behaviour is what was
intended (SELECT requires shownChannels non-empty; other modes don't).
But `-Wparentheses` flags this and a hurried reader can misread it.
Wrap explicitly: `(mode == SELECT && !shownChannels.isEmpty()) || ...`.

---

## P2 — Hot-path optimisations applied

### 8. `traceview.cpp` — verticalLines drawing was O(K·S) per provider per redraw

**Multi-column branch (line ~1633 in original) and single-column branch
(line ~1908).**  Both had the same shape: outer loop over the K
selected clusters, inner full scan of S spikes filtering by
`clusterId == *iterator`, with a fresh `QPen` constructed and
`painter.setPen()` called *per spike drawn*.

For a typical 32-s window with 30k visible spikes and 8 selected
clusters, this was ~240k integer compares plus ~30k pen
construction/setPen calls per provider per pan.  Visible stutter on
busy sessions.

**Refactored to O(K + S)**, mirroring the raster refactor that landed
in the 2026-04-20 fix:

  1. One pass over the K selected clusters builds a
     `QHash<dataType, QPen> penById`.
  2. One pass over the S spikes does `penById.contains(clusterId)`
     (O(1) amortised), with hot-pen tracking — `setPen` is called
     only when the cluster id changes between consecutive spikes.

The single-column branch additionally had the *worst* form of this
(constructing a `QPen` and calling `setPen` per drawn spike, even
inside a single cluster's run).  Same fix; the win there is larger
because the inner-loop `clusterList.contains(clusterId)` was an O(K)
linear scan on a `QList<int>`.

Same refactor applied to: multicolumn verticalLines, single-column
verticalLines.  The raster equivalents were already fixed in the
2026-04-20 patch.

### 9. `traceview.cpp` — replaced `QHash::operator[]` side-effect insert with `value()`

Multicolumn verticalLines, single-column verticalLines: lookups of
`clustersData[providerName]` and `selectedClusters[key]` use Qt's
operator[] which inserts a default-constructed (null) value when the
key is missing.  Over time this grows the hash with stale null
entries; the iterator paths then have to skip them.  Replaced with
`hash.value(key)` (returns default but does not insert) and added an
explicit null check.  No behavioural change; eliminates a slow leak.

---

## P3 — Recommendations not applied (would benefit from focused passes)

### R1 — `clustersprovider.cpp` / `eventsprovider.cpp`: replace the dichotomy with `std::lower_bound`

The current dichotomy has an unconventional shape:

```cpp
while ((newEndIndex - newStartIndex + 1) > dicotomyBreak) {
    time = clusters(2, newStartIndex);
    if (time > startInRecordingUnits) {
        long previousStart = newStartIndex;
        newStartIndex = previousStart - ((newEndIndex - previousStart + 1) / 2);
        newEndIndex = previousStart;
    }
    else { newStartIndex += ((newEndIndex - newStartIndex + 1) / 2); }
}
```

It does not probe the midpoint — it probes `newStartIndex` and jumps
asymmetrically.  It still converges (the existing bounds clamps
prevent the historical crashes) but takes more iterations than a
classical binary search and the off-by-one corrections at the loop
exit are fragile.

The timestamps in `clusters` row 2 are sorted (monotonic).  A clean
replacement is roughly:

```cpp
const dataType* row2 = &clusters(2, 1);   // 1-based, contiguous
const dataType* lo = std::lower_bound(row2, row2 + nbSpikes,
                                      startInRecordingUnits);
startIndex = (lo - row2) + 1;
if (startIndex > nbSpikes) startIndex = nbSpikes;
```

…plus equivalent for the `endIndex` upper bound.  This eliminates
~80 lines of fragile code per file and is provably correct.  Not
applied here because the change interacts with `previousStartIndex` /
`previousEndIndex` caching and warrants its own test pass.

### R2 — `traceview.cpp`: cache `tracesProvider.getNbSamples(...)` per redraw

`getNbSamples` is called 16+ times within a single redraw, all with
the same `(startTime, endTime, startTimeInRecordingUnits)` triple.
Each call is a couple of double-precision multiplications — minor
individually, but it sits inside per-channel and per-spike loops.
Easiest fix: compute `const int nbSamples = ...; const int
nbSamplesToDraw = ...` once at the top of `paintEvent` /
`drawTraces` and thread them through.

### R3 — `traceview.cpp`: stop using non-const `QList::operator[]` for read-only access

`channelOffsets[channelId]`, `channelFactors[channelId]`,
`gains[channelId]` — every one of these is a non-const operator[]
which can detach a shared list.  In hot drawing paths this matters.
Use `.at(channelId)` (const-correct, no detach) for reads.  Many
hundreds of call sites; mechanical sed-style fix.

### R4 — Hash side-effect insertions throughout

Beyond the two sites I touched, there are ~20 occurrences of
`clustersData[name]`, `eventsData[name]`, `selectedClusters[id]`,
etc. used as readers.  Each one will silently insert a null entry
when the key is missing, causing slow growth and surprising the
iterator-based `allClustersReady` / `allEventsReady` checks (the
current ones already accept nulls — see Bug 2 fix from 2026-04-20 —
but this is double-defensive coding to paper over a smell, not a
fix).  Convert reads to `.value(key)` and inserts to `.insert(...)`.

### R5 — `traceview.cpp::paintEvent` is 220 lines; `drawTraces` is 470

These should be split into helpers (`paintEvent_init`,
`paintEvent_redraw`, `paintEvent_overlay`, etc.; `drawTraces_events`,
`drawTraces_verticalLines`, `drawTraces_channels`,
`drawTraces_raster`).  After this audit, the multicolumn vs
single-column raster bodies are nearly identical — they could share
a single helper templated on the `X` offset and provider iteration.

### R6 — `paintEvent` recursive `updateWindow()` calls do redundant work

`updateWindow()` self-recurses when `columnDisplayChanged ||
groupsChanged` and again when `multiColumns && oldXshift != Xshift`.
After each recursive call, the outer call re-runs
`computeChannelDisplayGain()` and sets `drawContentsMode = REDRAW`,
which the recursive call already did.  Convert to a `do { ... } while
(needsAnotherPass)` loop with a single trailing
`computeChannelDisplayGain()`.

---

## Testing

Cannot exercise the GUI here.  Verified:

  - All five files compile with Qt5/g++ syntax-check (Qt6-specific
    `event->position()` calls error pre-existingly — unchanged in
    count between the unmodified and modified `traceview.cpp`).
  - Patches apply against `fccc7d3` cleanly.
  - The refactored verticalLines code preserves the same painter
    state (pen, no transform changes), draws into the same window
    rectangle, and emits the same `painter.drawLine(abscissa, top,
    abscissa, bottom)` calls in the same `(provider, cluster, spike)`
    iteration order — so any downstream code depending on draw order
    sees no change.
  - The `else if (clusters(2, previousEndIndex) < startInRecordingUnits)`
    units fix matches the same comparison in `retrieveData` exactly —
    no new code path introduced, just bringing the two browse methods
    into agreement with the read path.

Please verify on the `jg05-20120316` reference session at group 7
with raster + verticalLines enabled, several selected cluster
groups, and the spike-browse forward/backward shortcuts.  The
"channel select picks wrong channel when the topmost is skipped"
behaviour is the easiest to confirm — open a session, mark the
top-of-group channel as skipped, click in that group, observe that
the click lands on the second (not third) visible channel.

---

## Files changed

```
src/neuroscope/src/tracesprovider.cpp     | +24 -10
src/neuroscope/src/clustersprovider.cpp   | +51 -16
src/neuroscope/src/eventsprovider.cpp     | +33  -8
src/neuroscope/src/traceview.cpp          | +98 -55
src/neuroscope/src/traceview.h            |  +1  -1
```

No Qt/CMake config changes.  Build with the existing top-level
build instructions.
