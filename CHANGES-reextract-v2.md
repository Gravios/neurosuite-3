## ndm_reextractspikes{,_stderiv} + ndmanager — drop-in patch (2026-04-21 rev 3)

Three related changes:

1. `ndm_reextractspikes` and `ndm_reextractspikes_stderiv` now
   recognise the stderiv-pipeline extensions (`.spkD`, `.fetD`,
   `.pcaD`) alongside the raw-pipeline extensions (`.spk`, `.fet`,
   `.pca`).  Note: `.clu` and `.res` have no D variant — they
   are always written under the canonical extensions regardless
   of which detection pipeline was used.
2. Groups that lack a cluster file (`.clu`) or feature file
   (`.fet` / `.fetD`) are now **skipped** with a warning rather
   than aborting the whole run.
3. `reextractMaskHalfWidth` defaults to the group's own `nSamples`
   from the YAML (covering the full primary-pass waveform window)
   instead of the refractory period.

Plus a small QoL change to the `ndmanager` CLI.

### Rev history

- **Rev 1**: initial v2 patch; handled `.spk/.spkD` and `.clu/.cluD`.
  Broke because `.fet` was still a hard requirement in the pre-scan.
- **Rev 2**: added `.fet/.fetD` and `.pca/.pcaD` handling; kept
  `.clu/.cluD` symlink shim.  Broke because `.cluD` doesn't
  actually exist as a distinct extension — sorting
  (klusters / klustakwik) always writes `.clu.N`.  On stderiv
  sessions, the rev 2 symlink shim tried to create a
  `.clu.N → .cluD.N` symlink over the existing real `.clu.N`
  and bailed with "exists and is not a symlink; refusing to
  clobber."
- **Rev 3** (this): only `.spk`, `.fet`, `.pca` have D variants.
  `.clu` and `.res` are canonical.

### The extension landscape

| Canonical | Stderiv variant | Written by |
|-----------|-----------------|------------|
| `.res.N`  | *(no variant)*  | `ndm_extractspikes*` |
| `.spk.N`  | `.spkD.N`       | `ndm_extractspikes` vs `ndm_extractspikes_stderiv` |
| `.fet.N`  | `.fetD.N`       | `ndm_pca` vs `ndm_pca_stderiv` |
| `.pca.N`  | `.pcaD.N`       | `ndm_pca` vs `ndm_pca_stderiv` |
| `.clu.N`  | *(no variant)*  | `ndm_klustakwik`, klusters |

`.res` and `.clu` are pipeline-neutral.  Only the intermediate
waveform/feature/PCA artefacts get the D suffix because they
carry pipeline-specific numerical content (stderiv-transformed
vs raw).

### Files

- `src/ndmanager-plugins/scripts/ndm_reextractspikes`
- `src/ndmanager-plugins/scripts/ndm_reextractspikes_stderiv`
- `src/ndmanager/src/main.cpp`

No C++ plugin rebuild required.  ndmanager needs a rebuild to
pick up the CLI change.

### Pre-scan design

Five bash associative arrays track per-group state:

```bash
declare -A groupSpkExt    # "spk" | "spkD"
declare -A groupFetExt    # "fet" | "fetD"
declare -A groupPcaExt    # "pca" | "pcaD" | "" (optional)
declare -A groupNSamples
declare -A groupActive    # "y" | "n"
```

For each group:
- `.res` is required unconditionally (used to mask existing spikes)
- `.fet` / `.fetD` required; at least one must exist
- `.spk` / `.spkD` required
- `.clu` required (the canonical extension — no D variant)
- `.pca` / `.pcaD` optional at pre-scan time (shadowcluster will
  emit a clearer error if actually needed)

Per-script preference order:
- `ndm_reextractspikes`         prefers raw (`.fet` > `.fetD`, etc.)
- `ndm_reextractspikes_stderiv` prefers stderiv (`.fetD` > `.fet`)

### Symlink shim

`process_shadowcluster` hard-codes `.spk` / `.fet` / `.pca` (and
`.clu`, but that one is already canonical — no shim needed).
The scripts create symlinks for stderiv-variant files used by
the group, run shadowcluster, then remove the symlinks:

```bash
make_shim() {
    # $1 = stderiv ext, $2 = raw ext
    local src="$session.$1.$group"
    local dst="$session.$2.$group"
    if [ -e "$dst" ] && [ ! -L "$dst" ]; then
        echo_error "  $dst exists and is not a symlink; refusing to clobber"
        exit $GENERAL_ERROR
    fi
    ln -sf "$(basename "$src")" "$dst"
}

if [ "$spkExt" = "spkD" ]; then make_shim spkD spk; linked_spk=1; fi
if [ "$fetExt" = "fetD" ]; then make_shim fetD fet; linked_fet=1; fi
if [ "$pcaExt" = "pcaD" ]; then make_shim pcaD pca; linked_pca=1; fi

process_shadowcluster ...
sc_status=$?

[ "$linked_spk" = "1" ] && [ -L "$session.spk.$group" ] && rm -f "$session.spk.$group"
[ "$linked_fet" = "1" ] && [ -L "$session.fet.$group" ] && rm -f "$session.fet.$group"
[ "$linked_pca" = "1" ] && [ -L "$session.pca.$group" ] && rm -f "$session.pca.$group"

check_command_status process_shadowcluster $sc_status
```

The `.clu` file is read and written in place by shadowcluster —
no shim needed.  `.res` is read-only and the re-extraction
process uses `-m $session` to find `.res.N` directly.

### Backups

`.bak` files are created once per session (first run never
clobbers an existing `.bak`).  The backup list uses the actual
extensions the group read:

```bash
# res and clu are always canonical; spkExt / fetExt are per-group
backupExts="res clu $spkExt $fetExt"
```

`.pca` is read-only for shadowcluster; no backup needed.

### Mask half-width default (unchanged across revs)

If the YAML supplies an explicit `reextractMaskHalfWidth`, that
value is used verbatim.  Otherwise the default is the **max
per-active-group `nSamples`** from the YAML.  This covers the
full primary-pass waveform window so the lowered re-extraction
threshold doesn't re-fire on existing spikes' flanks.

### ndmanager CLI auto-discovery (unchanged across revs)

When invoked with no positional argument, ndmanager searches the
current working directory for `*.yaml` / `*.yml` parameter files:

- 0 candidates: open empty GUI (unchanged from legacy behaviour)
- 1 candidate: open it, log the path
- N candidates: open empty, print the list so the user picks

### Testing

- `bash -n` clean on both scripts
- Zero remaining references to `cluD` / `groupCluExt` / `linked_clu`
- 10 `linked_spk` / `linked_fet` / `linked_pca` / `make_shim`
  markers per script (3 `linked_*` declarations, 3 shim calls,
  3 teardowns, 1 `make_shim` definition)

Full end-to-end test not possible in the patch environment
(no `process_shadowcluster` binary).  On the user's machine,
after applying this tarball and restarting the session, the
expected log for a stderiv-sorted jg05-20120316 session is:

```
Active groups: 13  (skipped: 0)
Resolved reextractMaskHalfWidth = 32 samples (max group nSamples)
  [... Pass 1 detection log ...]
Shadow-clustering group 1  (32 samples × 8 channels)
  reading .spkD / .fetD / .clu / .pcaD
  [... merge log ...]
```

---

## Addendum — probe file copy-into-session (2026-04-21 rev 5)

Additional change to `src/ndmanager/src/probepage.{cpp,h}`.

**Behaviour change in the Probes tab:**

Before:
  1. Click `+` → inserts an empty row
  2. Click `Browse` → opens file picker
  3. Selected probe file is referenced by absolute path (or by path
     relative to the shared probe library if inside it)

After:
  1. Click `+` → opens file picker immediately
  2. On accept, the selected `.probe` file is **copied into the session
     directory** (the current working directory, since ndmanager is
     launched from the session dir)
  3. The probe row references the local copy by bare filename only

Sessions are now self-contained — moving a session directory to another
machine moves the probe file with it.

The separate `Browse` button on the Probes tab is retained for
re-pointing an existing row at a different probe file; it now uses the
same copy-into-session helper so the policy is uniform.

### Collision handling

If a different file with the same basename already exists in the session
directory, the user is prompted with three options:

- **Yes**: overwrite the local copy with the selected file
- **No**: reuse the existing local copy (treat the picker selection as a
  pointer, not a replacement)
- **Cancel**: abort

Same canonical path (user re-picks the already-local file) skips the
copy entirely and falls through to "use existing."

### Failure handling

If the copy fails (permissions, disk full, etc.), a `QMessageBox::warning`
is shown and no row is added / the existing row is left unchanged.

### Files touched

- `src/ndmanager/src/probepage.cpp` — merged addProbe + browseProbeFile;
  added `copyProbeIntoSession()` helper used by both.
- `src/ndmanager/src/probepage.h` — declared the helper.

No schema or YAML changes.  ndmanager rebuild required to pick up.

---

## Addendum — sdiff rank-deficiency support + decomposecollisions stderiv (2026-04-21 rev 6)

Two related changes addressing stderiv pipeline support:

### 1. process_shadowcluster: relax PCA channel-count check

`src/ndmanager-plugins/src/process_shadowcluster/process_shadowcluster.cpp`

The previous strict equality check at line 490 rejected any PCA basis
whose `nChannels` differed from the group's raw channel count:

```cpp
if (pca.nChannels != nChanGroup) {
    cerr << "error: PCA nChannels=" << pca.nChannels
         << " != group nChan=" << nChanGroup << endl;
    return 1;
}
```

This blocked the entire stderiv pipeline for `sdiffOrder ∈ {1, 3}`,
where `process_pca_stderiv` correctly drops one linearly-dependent
channel (order 1: `s[i] = x[i] - x[i+1]` makes the last channel
redundant; order 3: `sum_i s[i] = 0` constrains the sum to zero).

For an 8-channel group with `sdiffOrder=3`, `.pcaD.N` is written with
`nChannels=7` while `.spkD.N` retains the raw 8-channel layout (the
spatial derivative is computed in-place at extraction time without
dimensionality reduction — see `process_extractspikes_stderiv.cpp`
line 1469 + 1529).

The projection loop in `projectSpike()` already handles this
asymmetry correctly:

```cpp
for (int ch = 0; ch < m.nChannels; ++ch) {       // bound: PCA effective (7)
    ...
    const double raw = (double)wav[sIdx * nChan + ch];   // stride: raw (8)
}
```

The outer loop iterates `0..pca.nChannels-1`, reading channels
`0..6` of each sample.  This is exactly the set
`process_pca_stderiv` trained on (SDIFF_PASS with `dropLast` keeps
the leading `nChan-1` channels — see `process_pca_stderiv.cpp`
line 161).  The math was already correct; only the rigid equality
check at the gate blocked the call.

The fix relaxes the check to only reject `pca.nChannels >
nChanGroup` (the only geometrically impossible case — the PCA basis
cannot have more channels than the waveform layout provides), and
emits an `info:` line when `pca.nChannels < nChanGroup` so the
operator sees that stderiv reduction is being honored:

```cpp
if (pca.nChannels > nChanGroup) {
    cerr << "error: PCA nChannels=" << pca.nChannels
         << " > group nChan=" << nChanGroup
         << " (PCA basis cannot have more channels than the waveform layout)"
         << endl;
    return 1;
}
if (pca.nChannels < nChanGroup) {
    cerr << "info: PCA nChannels=" << pca.nChannels
         << " < group nChan=" << nChanGroup
         << " (stderiv rank-reduced basis; reading first "
         << pca.nChannels << " of " << nChanGroup << " channels)"
         << endl;
}
```

The downstream `extraFeat` auto-detection at line 517 already uses
`pca.nChannels` (effective), and the size formulas for `.fetD.N`
read at line 425 are already self-consistent with what
`process_pca_stderiv` writes.  No other changes needed.

**Requires C++ rebuild** of `process_shadowcluster`.

### 2. ndm_reextractspikes_stderiv: revert nChEffective

The script briefly tried to compute and pass an effective channel
count as `--nChan`, but that was the wrong fix: `--nChan` is also
used as the `.spkD` row stride, which must be the raw count
regardless of sdiff order.  Now reverted to passing the raw
`nChInGroup` consistently; the C++ change above makes that work
end-to-end for both raw and stderiv pipelines.

### 3. ndm_decomposecollisions: accept .spkD

`src/ndmanager-plugins/scripts/ndm_decomposecollisions`

The bash wrapper had a hard `[ -f "$session.spk.$g" ]` gate at the
"check at least one .spk file exists" step:

```bash
spk_count=0
for (( g=1; g<=nSpikeGroups; g++ )); do
    [ -f "$session.spk.$g" ] && (( spk_count++ ))
done
[ "$spk_count" -eq 0 ] && {
    echo_error "No .spk files found. Waveform data is required for collision decomposition."
    exit $INPUTS_MISSING; }
```

On a stderiv-only session (only `.spkD.N` present, no `.spk.N`),
this aborted the script before reaching the C++ — even though
`process_decomposecollisions.cpp` already does its own per-group
auto-detection at line 983:

```cpp
std::string spkD_path = args.session + ".spkD." + std::to_string(g);
std::string spk_path  = args.session + ".spk."  + std::to_string(g);
bool is_stderiv = false;
std::string active_spk = spk_path;
if (std::ifstream(spkD_path).good()) { active_spk = spkD_path; is_stderiv = true; }
```

The fix updates both gates (the existence check and the per-group
output-skip check) to accept either extension:

```bash
spk_count=0
for (( g=1; g<=nSpikeGroups; g++ )); do
    { [ -f "$session.spk.$g" ] || [ -f "$session.spkD.$g" ]; } && (( spk_count++ ))
done
```

**No `sdiffOrder` parameter is needed.**  `.spkD` stores the raw
channel count (sdiff is applied in-place without dimensionality
reduction in extractspikes_stderiv), so the C++ reads it with
`channels.size()` from the YAML group exactly as it does for `.spk`.
The collision-decomposition algorithm operates on waveforms only —
no PCA basis involved — so the rank-deficiency issue that affects
shadowcluster does not apply here.

No C++ rebuild required.

### Files

- `src/ndmanager-plugins/src/process_shadowcluster/process_shadowcluster.cpp`
- `src/ndmanager-plugins/scripts/ndm_reextractspikes_stderiv`
- `src/ndmanager-plugins/scripts/ndm_decomposecollisions`

### Testing

- `g++ -fsyntax-only` clean on `process_shadowcluster.cpp`
- `bash -n` clean on all three scripts
- Cross-checked projection loop bounds against `process_pca_stderiv`
  channel-drop semantics (`SDIFF_PASS` + `dropLast` → keeps leading
  nChan-1 channels)
- Verified `.fetD` dimensionality consistent: `nFeatures=3`,
  effective nCh=7 → `nFeatDim = 21`, `baseDim = 21` → `extraFeat`
  auto-detects to false (matches your YAML's `extra: false`)

---

## Addendum — Pass 2 commit counter + clearer summary (2026-04-21 rev 7)

Both re-extract scripts now print a richer summary line distinguishing
three distinct per-group outcomes:

- **committed**: Pass 2 produced non-empty merged outputs and the
  four `mv -f` renames committed successfully.  This is the count
  of groups whose live `.res / .spk(D) / .fet(D) / .clu` files were
  actually updated.
- **no new spikes**: Pass 2 saw a 0-byte `$tmpStem.res.$group` (Pass
  1 accepted no spikes for this group) and took the fast-path —
  originals unchanged.
- **pre-filtered**: pre-scan rejected this group before Pass 1 (no
  `.clu.N`, no `.fetD`/`.fet`, no `.spkD`/`.spk`, etc.) and the
  group was never a candidate for merging.

Before (rev 6):

```
Done — new spikes merged in-place (1 group(s))
```

After:

```
Done — 1/1 eligible group(s) had new spikes merged in-place
       (0 had no new spikes, 12 pre-filtered in pre-scan)
```

On a partially-sorted session (like jg05-20120316 with only group 7
curated), the `12 pre-filtered` is informative — the operator sees
that 12 groups were skipped at the pre-scan gate and that the one
eligible group was successfully merged.

### Additional Pass 2 diagnostics

For each active group, Pass 2 now reports the exact new-spike count
from Pass 1's output:

```
Shadow-clustering group 7  (41 samples × 8 channels, 194159 new spikes)
```

(previously only the group dimensions were shown).  The new-spike
count comes from `wc -c < $tmpStem.res.$group` divided by 8 (int64
timestamps).

The `no new spikes` fast-path now distinguishes two failure modes:

- Pass 1 produced no file at all (an upstream failure, flagged as
  `echo_warning`):
  ```
    group 3: Pass 1 produced no jg05-20120316-reextr-stderiv-tmp.res.3 — skipping
  ```
- Pass 1 produced a 0-byte file (expected outcome when detection
  accepted no new spikes, flagged as `echo_info`):
  ```
    group 3: Pass 1 accepted 0 new spikes — originals unchanged
  ```

### Leak fix

The Pass 2 fast-path and commit cleanup now also remove
`$tmpStem.spkD.$group` (was only removing `$tmpStem.spk.$group`
before; Pass 1 writes `.spkD` initially, and only the rename loop
between Pass 1 and Pass 2 moves it to `.spk`).  On runs where the
rename didn't happen (e.g. inactive groups), the leftover `.spkD`
files would have accumulated between runs.

### Files touched

- `src/ndmanager-plugins/scripts/ndm_reextractspikes`
- `src/ndmanager-plugins/scripts/ndm_reextractspikes_stderiv`

No C++ changes; no rebuild required.

---

## Addendum — sub-cluster the unmatched bin (2026-04-21 rev 8)

New script: `src/ndmanager-plugins/scripts/ndm_subcluster_unmatched`.

After `ndm_reextractspikes{,_stderiv}` commits its shadow-cluster
merge, each group's `.clu.N` contains a large "unmatched" bin
holding every new spike whose robust Mahalanobis distance exceeded
the χ² threshold from every eligible parent.  In your rev-6 run on
group 7, this was **186,981 spikes** — 96% of the new spike
population — sitting in a single cluster ID.

That bin is heterogeneous: it mixes genuinely novel low-amplitude
units, collision artefacts, mis-masked flanks of existing spikes,
and noise.  Inspecting it as one cluster in Klusters is unusable.

### What the script does

For each group in a session (or a specific one if passed):

1. **Locate**: read `.clu.N`, find the single highest-ID bin.  That's
   the unmatched bin by `process_shadowcluster`'s naming convention
   (`unmatched_id = 2 * maxExistingId + 1`).
2. **Extract**: copy the `.fet.N` rows whose `.clu.N` value equals
   `unmatched_id` into a sandbox file
   (`$session-unmatched-$g.fet.$g`), with its own int32 header.
3. **Re-cluster**: invoke `KlustaKwik $sandboxStem $g -UseFeatures
   all` — it reads the sandbox `.fet.N`, writes a sandbox `.clu.N`.
   Uses the real session YAML's per-group KK parameters
   (`maxClusters`, `maxPossibleClusters`, `minClusters`,
   `mergeThresh`) via the standard three-tier resolver.
4. **Remap & merge**: rewrite the live `.clu.N` with sub-cluster
   IDs `(unmatched_id + 1) .. (unmatched_id + K)` where K is the
   number of non-noise sub-clusters KlustaKwik found.  Rows that
   KlustaKwik labelled as noise (cluster 1 in its convention) stay
   tagged with `unmatched_id` so the residual bin is still visible
   in Klusters.
5. **Backup**: the pre-subcluster `.clu.N` is saved to
   `.clu.N.bak-sub` (first-run-wins, same policy as `.bak` in
   reextract).  To re-run, remove `.bak-sub` first.

Waveforms (`.spk / .spkD`), features (`.fet / .fetD`), PCA bases
(`.pca / .pcaD`), and timestamps (`.res`) are **not touched**.  The
Mahalanobis subspace that defined "unmatched" is the reference
`.fet` space, so re-clustering happens in that same space — no
waveform reprocessing required.

### ID convention (after sub-clustering)

```
1..maxExistingId              original, pre-shadowcluster clusters
maxExistingId+1..unmatched-1  shadow clusters (from reextractspikes
                              Mahalanobis gate)
unmatched_id                  residual: spikes KlustaKwik labelled
                              as noise during sub-clustering
unmatched_id+1..unmatched+K   new sub-clusters extracted from the
                              previously monolithic unmatched bin
```

For your group 7 (post-rev-6 state: `shadow offset=17`,
`unmatched_id=34`), sub-clustering would yield IDs `35..35+K-1`.
The operator then triages in Klusters alongside existing shadow
clusters (19, 33) and parents.

### Why a separate script

1. Re-extract itself is already transactional and slow.  Letting
   the user inspect the shadow-cluster result in Klusters first,
   then decide whether to sub-cluster, keeps each atomic operation
   small.
2. The same script can be run on any `.clu.N` that happens to have
   a high-cardinality "junk" cluster — not just the one emitted by
   shadowcluster.

### Implementation notes

- Uses Python 3 inline (via heredocs) for the int32/int64 binary
  file parsing.  No build dependency — Python 3 is already required
  by the neurosuite_compare tooling.
- `UseFeatures all` is passed to `KlustaKwik` rather than a
  hand-computed `1111...` string because the feature-dim arithmetic
  differs between raw and stderiv pipelines (extra peak feat vs
  rank-reduced).  `all` just reads the `.fet` header at runtime.
- Minimal KK parameter surface exposed: `minClusters`,
  `maxClusters`, `maxPossibleClusters`, `mergeThresh`.  Others
  inherit KK's built-in defaults, which are fine for the relatively
  clean task of splitting a known-residual bin.
- Sandbox stem is `$session-unmatched-$g` (matches the pattern of
  `$session-reextr-*-merge` from the reextract scripts).  All
  sandbox files cleaned up on success or failure.

### Usage

```bash
# All groups
cd /path/to/session
ndm_subcluster_unmatched

# Just one group (recommended workflow: sort → re-extract → inspect → sub-cluster)
ndm_subcluster_unmatched jg05-20120316.yaml 7
```

### Testing

Round-trip tested with synthetic 10-spike / 5-dim data in
`/tmp/subcluster_test`: extraction, KK-simulated output, merge-back
all produce the expected results.  End-to-end KK invocation needs
your real jg05-20120316 data to verify; cannot run here.

### Files

- `src/ndmanager-plugins/scripts/ndm_subcluster_unmatched` (new)

No C++ changes.

---

## Addendum — Klusters focus & autoscale (2026-04-21 rev 9)

Three related UX improvements in Klusters.

### 1. Arrow keys no longer steal focus for tab-switching

Previously Left/Right arrows inside a tab page (such as ClusterView)
were intercepted by the app-level event filter at `klusters.cpp:1081`
and used to cycle display tabs.  Useful at the tab-bar strip itself,
wrong inside a page where arrows are needed for cluster navigation.

The gate now checks specifically for a `QTabBar` ancestor via
`qobject_cast<QTabBar*>` rather than walking up to the `QTabWidget`
(which includes tab pages).  Behaviour:

- Focus **inside** a tab page + Left/Right → falls through to the
  page's own handler (no tab switch)
- Focus **on the tab bar strip** + Left/Right → cycles tabs as
  before
- `Ctrl+Left/Right` anywhere → jumps to Overview first, then cycles
  (unchanged)

Added `#include <QTabBar>`.

### 2. Focus returns to ClusterView after every curation op

The existing `focusClusterView()` helper in `klustersview.cpp:1702`
was already called from 4 sites: nudge ±, recluster completion,
realign completion.  Six more call sites added — covering every
remaining curation operation:

- `slotGroupClusters` (after `doc->groupClusters`)
- `slotMoveClustersToNoise` (after `doc->deleteClusters(...,1)`)
- `slotMoveClustersToArtefact` (after `doc->deleteClusters(...,0)`)
- `slotSpikesDeleted` (end-of-polygon-op completion)
- `slotUndo`, `slotRedo`

After any of these, the cluster view regains focus so arrow keys
(combined with fix 1) drive cluster navigation directly instead of
going to the cluster palette or getting lost on a transient widget.

### 3. 'F' key — autoscale toggle for the feature scatter plot

`ClusterView` now toggles an autoscale-to-visible-clusters mode when
the user presses plain `F` (no modifier).  `S` was considered but is
already bound to Split Clusters.  Plain `F` is otherwise unbound
(Shift+F = Apply Drift Siblings, Ctrl+Shift+F = Next Spike — both
preserved).

When autoscale is ON:
- A status-bar message confirms `Autoscale: on (press F to disable)`
- Immediately re-fits the `(dimensionX, dimensionY)` window to the
  currently-shown clusters
- On every subsequent REDRAW (dimension change, cluster show/hide,
  post-op repaint), re-fits automatically
- UPDATE paths (incremental redraw of just-changed clusters) are
  skipped to avoid jittering the window on every tweak

When autoscale is OFF:
- Status bar shows `Autoscale: off`
- View reverts to manual-zoom behaviour; existing bounds persist
  until the user zooms or changes dimensions

The fit geometry matches `updatedDimensions()`:
- 5 % margin on each side
- Origin axes always kept in view (so the 0-cross stays visible)
- Clamped to ±1,000,000 to stay inside Qt's safe coordinate range

Implementation (`clusterview.cpp` / `clusterview.h`):

```cpp
void ClusterView::autoscaleToVisibleClusters()
{
    const QList<int> shown = view.clusters();
    if (shown.isEmpty()) return;

    Data& clusteringData = doc.data();
    bool haveAny = false;
    long xMin = 0, xMax = 0, yMin = 0, yMax = 0;

    for (int clustId : shown) {
        Data::Iterator it = clusteringData.iterator(
            static_cast<dataType>(clustId));
        for (; it.hasNext(); it.next()) {
            const QPoint p = it(dimensionX, dimensionY);
            if (!haveAny) {
                xMin = xMax = p.x(); yMin = yMax = p.y(); haveAny = true;
            } else {
                if (p.x() < xMin) xMin = p.x();
                if (p.x() > xMax) xMax = p.x();
                if (p.y() < yMin) yMin = p.y();
                if (p.y() > yMax) yMax = p.y();
            }
        }
    }
    if (!haveAny) return;
    // … 5% margin + origin inclusion, matching updatedDimensions() …
}
```

Note: `Data::Iterator::operator()(dx, dy)` returns a QPoint with the
ordinate already negated (Qt graphical orientation, data.h:344),
so the accumulated bounds are in screen-orientation space — no
additional flip needed when writing `ordinateMin/Max`.

Cost: O(total shown spikes) per REDRAW, same order as a single
paint pass, so no performance concern even at 10⁶-spike groups.

### Files touched

- `src/klusters/src/klusters.cpp`      — arrow gate + `#include <QTabBar>`
                                          + 6 `focusClusterView()` calls
- `src/klusters/src/clusterview.h`     — `autoscaleEnabled` field +
                                          `autoscaleToVisibleClusters()` decl
- `src/klusters/src/clusterview.cpp`   — F-key binding in `keyPressEvent`,
                                          method implementation,
                                          REDRAW hook in `paintEvent`

Rebuild `klusters` to pick up.

### Testing

Brace balance verified on all three files.  Cannot `g++
-fsyntax-only` offline (needs Qt headers + klusters internals); rely
on your build.  Behaviour test plan:

1. Open a session, click into ClusterView, press Right arrow →
   cluster palette cycles to next cluster (no tab switch).
2. Click on the tab bar strip directly, press Right arrow → tabs
   cycle (preserved).
3. Select a polygon, delete-noise, observe → cluster view retains
   focus; Left/Right arrow cycles clusters immediately.
4. Press F → status bar says "Autoscale: on"; view refits to the
   current shownClusters.  Change dimensionX via combo box → view
   re-fits automatically.  Press F again → status bar says "off";
   view stops auto-fitting on subsequent redraws.

---

## Addendum — autoscale margin preference (2026-04-21 rev 10)

The F-key autoscale introduced in rev 9 used a hard-coded 5 % margin
per side.  Rev 10 exposes that value as a new general preference so
the operator can tune how tight the fit is.

### New preference

Preferences → General → "Autoscale margin"

A `QDoubleSpinBox` (suffix " %", range 0.0 – 50.0, step 0.5, default
5.0).  Placed inline with Marker size and Selection line width —
all three are scatter-plot rendering knobs and belong together.

### Semantics

The value is a fraction applied per side.  At 5 %, the fit adds
5 % of the data extent on the left AND right of the abscissa range
(and top/bottom of the ordinate range), for 10 % total.  At 0 % the
fit is tight against the spike cloud.  At 50 % the fit is dominated
by margin and data sits in the middle third — useful for sessions
with very compact clusters where you want whitespace around them
for polygon-drawing room.

### Implementation plumbing

Mirrors the existing preference pattern for markerSize /
selectionLineWidth / templateThresholdMin etc.  Seven file touches:

1. `src/klusters/src/configuration.h` — three public methods
   (`getAutoscaleMarginPercent`, `getAutoscaleMarginPercentDefault`,
   `setAutoscaleMarginPercent` with 0–50 clamp), one private field
   + static default.
2. `src/klusters/src/configuration.cpp` — `const double
   autoscaleMarginPercentDefault = 5.0`, one `settings.value()` entry
   in `read()`, one `settings.setValue()` entry in `write()`.
3. `src/klusters/src/prefgenerallayout.ui` — new
   `QDoubleSpinBox autoscaleMarginSpinBox` + matching `QLabel
   autoscaleMarginLabel` + spacer, all inside the existing
   row-1 QHBoxLayout.  Tabstop entry added after
   `selectionLineWidthSpinBox`.
4. `src/klusters/src/prefgeneral.h` / `prefgeneral.cpp` — two
   methods delegating to `autoscaleMarginSpinBox->{value,setValue}`.
5. `src/klusters/src/prefdialog.cpp` — four additions: enableApply
   connection, `updateDialog()` push, `updateConfiguration()` pull,
   `slotDefault()` reset.
6. `src/klusters/src/clusterview.cpp` — `#include "configuration.h"`
   + replace hard-coded `0.05` with
   `configuration().getAutoscaleMarginPercent() / 100.0` in
   `autoscaleToVisibleClusters()`.

### Persistence

Stored in the QSettings `General` group under key
`autoscaleMarginPercent`.  Survives across Klusters sessions like
every other preference.

### Backward compatibility

Existing users pick up the default 5.0, matching rev-9 behaviour
exactly.  No data-file format change; QSettings migrations not
needed.

### Testing

- Brace balance verified on all 6 C++ files (clusterview.cpp,
  clusterview.h, configuration.h, configuration.cpp, prefgeneral.h,
  prefgeneral.cpp, prefdialog.cpp, klusters.cpp).
- `prefgenerallayout.ui` parses as well-formed XML.
- Identifier cross-ref: the three new names appear in 3–5 files each,
  matching the expected wiring topology.

Behaviour test (once built):

1. Preferences → General → find "Autoscale margin" in the Marker
   size / Selection line width row.  Adjust to e.g. 10.0 % → Apply.
2. Open a session, click into ClusterView, press F → autoscale
   enabled; margin around the cluster cloud is now wider than the
   rev-9 default.  Set to 0 → tight fit, no whitespace.
3. Close and reopen Klusters → the value persists.
4. Preferences → Defaults → value resets to 5.0.

### Files

- `src/klusters/src/configuration.h`
- `src/klusters/src/configuration.cpp`
- `src/klusters/src/prefgenerallayout.ui`
- `src/klusters/src/prefgeneral.h`
- `src/klusters/src/prefgeneral.cpp`
- `src/klusters/src/prefdialog.cpp`
- `src/klusters/src/clusterview.cpp`

---

## Addendum — CMake install list (2026-04-21 rev 11)

`make install` now copies the new bash scripts alongside the rest of
the ndmanager-plugins tooling.

### The problem

Three scripts shipped across earlier revs were never registered with
CMake, so a fresh `cmake --build . --target install` would leave
them sitting in the source tree — usable only by running them from
`src/ndmanager-plugins/scripts/` directly, not from `$PATH`:

- `ndm_reextractspikes`            (rev 1 onward)
- `ndm_reextractspikes_stderiv`    (rev 1 onward)
- `ndm_subcluster_unmatched`       (rev 8)

The user hit this first with `ndm_subcluster_unmatched` since the
other two had been dropped into `$PATH` manually.  Fixing the third
one the same way would just repeat the bug; so all three are
registered at once.

### Fix

`src/ndmanager-plugins/scripts/CMakeLists.txt` — three new entries
added to the `install(PROGRAMS … DESTINATION "${CMAKE_INSTALL_BINDIR}")`
block with brief doc comments explaining what each does.  The
existing entries are untouched.

No changes to the man-page `foreach` loop — none of the three
scripts ship a `.docbook` source yet.  If a man-page is desired,
create the `.docbook` file in the same directory and add the script
name to the `foreach(_page IN ITEMS …)` list; `add_manpage()` is a
no-op when the docbook is missing, so listing a name without a
docbook is harmless but also builds nothing.

### Not addressed

The parent `src/ndmanager-plugins/src/CMakeLists.txt` was NOT
touched in this rev.  The user reached a working
`process_shadowcluster` binary in rev 7, which means their local
tree already has the three missing `add_subdirectory()` entries
(for `process_reextractspikes`, `process_reextractspikes_stderiv`,
`process_shadowcluster`) plus each subdirectory's own
`CMakeLists.txt`.  If you need those re-shipped, say so and I'll
include them.

### Install recipe

```bash
# Re-apply the tarball (CMakeLists is picked up), re-configure if needed
tar xzf ns3-reextract-v2-2026-04-21.tar.gz -C /path/to/neurosuite-3/
cd /path/to/neurosuite-3/build
cmake ..                                  # re-run because the install list changed
cmake --build . --target install          # copies the three new scripts to $BINDIR
which ndm_subcluster_unmatched            # sanity check
```

### Files

- `src/ndmanager-plugins/scripts/CMakeLists.txt`
