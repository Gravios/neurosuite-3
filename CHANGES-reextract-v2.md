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
