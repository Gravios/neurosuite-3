# PCA feature extraction

Commands that turn spike waveforms (`.spk`) into the feature vectors (`.fet`)
KlustaKwik and Klusters cluster on, and the PCA basis (`.pca`) used to
re-project spikes later.

**Contents:** [`ndm_pca` — per-channel PCA](#ndm_pca--per-channel-pca-spk--fet-pca) · [`ndm_refeaturize` — re-project onto the saved basis](#ndm_refeaturize--re-project-onto-the-saved-basis-fet) · [`ndm_pca_multichannel` — joint multi-channel PCA](#ndm_pca_multichannel--joint-multi-channel-pca-spk--fetm-pcam) · [`ndm_pca_stderiv` — deprecated alias](#ndm_pca_stderiv--deprecated-alias-for-ndm_pca---method-stderiv)

---

## `ndm_pca` — per-channel PCA (`.spk` → `.fet`, `.pca`)

### Synopsis

```bash
ndm_pca [session.yaml] [electrodeGroup] [--method M] [--methodOrder N] [-- process_pca flags]
```

**Input:** `session.spk.<method>.N`, `session.res.<method>.N` (shared spike times, any method tag)

**Output:** `session.fet.<method>.N`, `session.pca.<method>.N`; with a `_D<lag><dims>` token also `session.fet.<token>.N`, `session.pca.<token>.N`

### Description

Fits a PCA basis for each channel of an electrode group, projects every spike
onto it, and appends the spike time as the last column of the `.fet`. The
`.pca` file holds the basis in the PCAE layout, with the transform recorded in
its header, so downstream readers (Klusters, KiloKlustaKwik re-projection,
`ndm_refeaturize`) know which waveform domain it was fit on without consulting
the YAML. See [`.fet`](../formats/fet.md) and [`.pca`](../formats/pca.md).

Without `electrodeGroup`, all groups run in parallel, one `process_pca` per
group, each limited to `threadsPerGroup` OpenMP threads. Each group's output is
collected and printed after it finishes. With `electrodeGroup`, only that group
runs.

**Method.** `method` selects the waveform domain and stamps the outputs:

| Method | `.spk` read | Processing |
|---|---|---|
| `standard` | `.spk.standard.N` | PCA on the raw waveforms. |
| `sdiff` | `.spk.sdiff.N` | PCA on the stored waveforms; `methodOrder` must be 1–3. |
| `stderiv`, `stderiv_S<order>`, `stderiv_C<order>` | `.spk.<token>.N` | The `.spk` already holds the transformed waveform. `process_pca_stderiv -d 4` only drops the linearly dependent channel (see `dropLastChannel`), then `process_pca` runs on the reduced set. |

A suffixed token states the order itself (`stderiv_S3` is order 3,
`stderiv_C5` is order 5) and overrides `methodOrder`. For the bare `stderiv`
token `methodOrder` is used and must be 1–5. Tokens are described under
[method tokens](../formats/naming.md#method-tokens). The flattened
`(method, order)` is passed to `process_pca --pca-method` and written into the
basis header.

**Window.** PCA uses the samples `before` and `after` the group's
`peakSampleIndex`. If either is unset, or the group has no `peakSampleIndex`,
the whole waveform is used. `before` and `after` take one value for all groups
or a space-separated list with one value per group. The number of components
per channel is the group's `nFeatures` (mandatory, from
`spikeDetection/channelGroups`), as are `channels` and `nSamples`.

**Skip rules.** A group is skipped (exit 10) when its `.fet.<method>.N` exists,
and (exit 11) when its `.spk` or `.res` is missing. With all groups, skipped
groups do not fail the run; any other failure is reported per group and
returned.

**Extra flags.** Options after the positional arguments, or after `--`, go to
`process_pca` unchanged. `--method` and `--methodOrder` are handled by
`ndm_pca` and override the YAML.

```bash
ndm_pca session.yaml                          # all groups, in parallel
ndm_pca session.yaml 3                        # group 3 only
ndm_pca session.yaml 3 --method stderiv_C5    # override the YAML method
ndm_pca session.yaml 3 -- --varimax-max-iter 50
```

### Parameters

```yaml
- name: ndm_pca
  parameters:
    - name: before
      value: 12
      status: Mandatory
    - name: after
      value: 12
      status: Mandatory
    - name: extra
      value: 'false'
      status: Mandatory
    - name: excludeClusters
      value: ''
      status: Optional
    - name: dropLastChannel
      value: true
      status: Optional
    - name: varimax
      value: true
      status: Optional
    - name: method
      value: standard
      status: Optional
    - name: methodOrder
      value: 3
      status: Optional
```

| Name | Default | Meaning |
|---|---|---|
| `method` | `standard` | Method token. A trailing `_D<lag><dims>` also writes the lagged feature space (below). `--method` overrides it. |
| `methodOrder` | 3 | Spatial-derivative order for `sdiff` (1–3) and bare `stderiv` (1–5). Ignored for `standard` and when the token carries its own order. The deprecated name `sdiffOrder` is still read. `--methodOrder` overrides it. |
| `before` | unset (template: 12) | Samples before the peak in the PCA window. One value or one per group. |
| `after` | unset (template: 12) | Samples after the peak. Same format as `before`. |
| `extra` | `false` | `true` appends the spike's peak value on each channel as an extra feature (`process_pca -x`). |
| `varimax` | `true` | Varimax rotation of the retained components of each channel (`--varimax`). The explained variance is unchanged, but each component concentrates on fewer time samples, which makes the axes easier to read when hand-sorting. The rotation is stored in the `.pca`, so every consumer follows it. |
| `varimaxMaxIter` | 30 | Maximum rotation iterations; only passed when set. |
| `varimaxTol` | 1e-6 | Relative convergence tolerance; only passed when set. |
| `excludeClusters` | empty | Comma-separated cluster ids (e.g. `"5,12"`) left out of the PCA **fit**. All spikes are still projected, so the `.fet` keeps one row per spike; only the basis changes. Requires `session.clu.<method>.N` from an earlier clustering pass (error if missing). Use it on a second pass to stop high-rate units such as interneurons from dominating the basis. |
| `dropLastChannel` | `true` | Stderiv only. Drop the channel the spatial derivative leaves redundant (orders 1, 3, 4, 5), so the basis has `nChannels − 1` channels. `false` keeps every channel (`process_pca_stderiv -k`). Order 2 always keeps every channel. |
| `threadsPerGroup` | ⌊nCores / nGroups⌋, min 1 | OpenMP threads per `process_pca` when all groups run in parallel. Read only from the `ndm_pca` block (no `general` fallback). |

`varimax` is on by default. A session whose block omits the key gets the
rotation, so re-running `ndm_pca` on an older session produces a **rotated**
basis and `.fet`. Clusters will not sit where they did, and a `.clu` curated on
the old features refers to a feature space that no longer exists. Set
`varimax: false` for the unrotated, variance-ordered basis.

`dropLastChannel` in detail: for orders 1 and 3 the dropped channel is an exact
linear combination of the others. For the custom-pattern orders 4 and 5 the
drop is a convention, and the pattern may be full rank. Because PCA is fit per
channel and truncated to `nFeatures`, the dropped channel's features cannot in
general be rebuilt from the kept ones. `ndm_refeaturize` uses the same value
(it reads the `ndm_pca` block unless its own sets one). Changing the value
invalidates existing `.fet`/`.pca`: refit rather than mixing widths. Klusters,
KiloKlustaKwik and shadowcluster accept a basis with fewer channels than the
group and skip the dropped one.

See [parameters](../parameters.md) for how values are looked up.

### Lagged feature space (`method: <token>_D<lag><dims>`)

A trailing `_D<lag><dims>` on the method, e.g. `stderiv_C5_D34`, makes `ndm_pca`
also write the lagged feature space that fiber-kit uses (`fiber-session
--feat-lag`): per channel, PC1 at −lag, 0 and +lag samples, plus PC2 at 0 when
`dims` is 4. `dims` 3 gives the three lags only. `lag` must be 1–9 and `dims`
3 or 4; anything else is an error.

| Reads | Writes |
|---|---|
| `.spk.stderiv_C5.N`, `.res.*.N` | `.fet/.pca.stderiv_C5.N` (ordinary) |
| | `.fet/.pca.stderiv_C5_D34.N` (lagged, same fit) |

The `.spk`, `.res` and the ordinary `.fet`/`.pca` use the waveform token (the
token without `_D…`); only the lag pair carries the full token.
`excludeClusters` reads `.clu.<full token>.N`.

The lag basis is an ordinary PCAE file on the PCA window widened by `lag` on each
side (`recShift − lag`, `data2use + 2·lag`, `centered 0`, zero means), so the lag
`.fet` is exactly the projection of the `.spk` through it. It has no `extra`
columns. Column order per channel is `[PC1@−lag, PC1@0, PC1@+lag, PC2@0]`,
identical to fiber-kit's `fiber_pca.lag_project`. `before`/`after` must leave
`lag` samples on each side of the window inside the waveform, and dims 4 needs
`nFeatures ≥ 2`; otherwise the run is refused rather than writing a non-lagged
file under a `_D` name. To view the lag `.fet` in Klusters set the group's
`nFeatures` to `dims`.

Existing outputs are never overwritten; only the missing ones are written:

- both `.fet` files exist: the group is skipped;
- only the lag `.fet` exists: the ordinary pair is written;
- only the ordinary `.fet` exists: the basis is refit, the stored ordinary
  `.fet`/`.pca` are kept, and only the lag pair is written. A warning is printed
  if the refit basis differs from the kept `.pca` (settings changed since it was
  written); the lag space then follows the current settings.

No `.spk.<token>_D34` copy or link is needed: Klusters resolves the `.spk` of a
`_D` token to its waveform token's file (see
[naming](../formats/naming.md#resolution-rules)).

```yaml
- name: ndm_pca
  parameters:
    - {name: method, value: stderiv_C5_D34, status: Optional}
    - {name: before, value: 12,             status: Optional}
    - {name: after,  value: 12,             status: Optional}
```

Differences from `fiber-session --emit-fet`: `ndm_pca` projects the `.spk` as
stored, while fiber-session realigns each spike first; and the lag basis records
the transform's input channel count like every `ndm_pca` basis. Choose the lag
per session rather than copying 3.

---

## `ndm_refeaturize` — re-project onto the saved basis (`.fet`)

### Synopsis

```bash
ndm_refeaturize [session.yaml] [electrodeGroup] [--method M] [--methodOrder N]
```

**Input:** `session.spk.<method>.N`, `session.pca.<method>.N`, `session.res.<method>.N` (shared, any method tag)

**Output:** `session.fet.<method>.N` (overwritten)

### Description

Projects the spikes onto the eigenvectors already stored in
`.pca.<method>.N` and rewrites `.fet.<method>.N`, without refitting PCA. Use it
when spikes have moved but the basis is still valid, typically after
[`ndm_alignspikes_pca`](alignment.md#ndm_alignspikes_pca--per-cluster-realignment-after-clustering).
The feature columns stay in the same space as before, so an existing clustering
remains meaningful.

The input is prepared exactly as `ndm_pca` prepared it for the fit: for the
stderiv family the redundant channel is dropped with `process_pca_stderiv -d 4`
(subject to `dropLastChannel`) before `process_refeaturize` projects;
`standard` and `sdiff` are projected directly. The PCA window comes from the
basis header. The spike times are then appended with `process_mergefeatures`,
as in `ndm_pca`.

The output is always overwritten; there is no skip when it exists. Without
`electrodeGroup`, groups are processed one after another. A missing `.spk`,
`.pca` or `.res` ends the run with exit 11 at that group.

The `.spk` is looked up strictly as `.spk.<method>.N`, so a `_D<lag><dims>` token
finds no input. Re-run `ndm_pca` for a lag feature space instead.

### Parameters

```yaml
- name: ndm_refeaturize
  parameters:
    - name: method
      value: standard
      status: Optional
    - name: methodOrder
      value: 3
      status: Optional
    - name: extra
      value: 'false'
      status: Optional
```

| Name | Default | Meaning |
|---|---|---|
| `method` | `standard` | Method token, as in `ndm_pca`. `--method` overrides it. |
| `methodOrder` | 3 | Spatial-derivative order 1–3 for `sdiff` / bare `stderiv`; a suffixed token's own order wins. Deprecated name `sdiffOrder` is still read. `--methodOrder` overrides it. |
| `extra` | `false` | Append per-channel peak values. Must match the `extra` used by `ndm_pca` when it fit the basis, or the column count disagrees. |
| `dropLastChannel` | value of the `ndm_pca` block, else `true` | Stderiv channel drop. Read from this block first, then from `ndm_pca`. It must match the fit; a different width projects wrongly without an error. |

See [parameters](../parameters.md) for how values are looked up.

---

## `ndm_pca_multichannel` — joint multi-channel PCA (`.spk` → `.fetM`, `.pcaM`)

> **Not usable at present.** The script calls `read_xpath_int` and
> `read_xpath_count`, which are not defined in `ndm_functions`. The group's
> channel count and waveform length come back empty, and without an
> `electrodeGroup` argument no group is processed. Until the script is fixed,
> call `process_pca_multichannel` directly (below).

### Synopsis

```bash
ndm_pca_multichannel [session.yaml] [electrodeGroup] [-- process_pca_multichannel flags]
```

**Input:** `session.spk.N`, `session.res.N` (untagged names only)

**Output:** `session.fetM.N`, `session.pcaM.N`

### Description

An alternative to `ndm_pca` that treats each spike as one
`nChannels × nSamples` vector and fits a single basis, instead of one basis per
channel. It keeps the correlation between channels that per-channel PCA
discards. The probe geometry is stored in the `.pcaM` header so feature
directions can be mapped back to positions, and can optionally weight the
covariance by distance to focus large groups (Neuropixels-scale) on the
spike-bearing channels.

It has no YAML parameter block. The script reads the group's `channels`,
`nSamples` and `nFeatures` (default 6) from the session YAML and passes any
further options to `process_pca_multichannel`. A group whose `.fetM` exists is
skipped. The script does not use method-tagged names, `before`/`after`, or the
peak position.

Output layouts:

- `.fetM.N` — `int32 nDimensions`, then `nSpikes × nDimensions` `int64`:
  `nFeatures` components for the whole spike (not per channel), then the spike
  time. Klusters sees it as one virtual channel.
- `.pcaM.N` — little-endian: `int32` magic `PCAM` (0x4d414350), `int32` version
  (1), `int32 nChan, nSamp, nComp, centered, recShift`, `nChan × double` x and y
  positions (µm), `(nChan·nSamp) × double` mean, `(nChan·nSamp) × nComp × double`
  eigenvectors (column-major), `nComp × double` eigenvalues (descending).

### `process_pca_multichannel` command line

```bash
process_pca_multichannel -f session.fetM.N.tmp -n CHANNELS -w SAMPLES -d COMPONENTS [options] session.spk.N
process_mergefeatures session.fetM.N.tmp session.res.N session.fetM.N
```

`process_pca_multichannel` writes the features without spike times to the `-f`
file and the basis next to it (`.fetM.` → `.pcaM.`, a trailing `.tmp` removed).
`process_mergefeatures` appends the times. Use `-` as input to read stdin
(then `-s` is required).

| Option | Meaning |
|---|---|
| `-f FILE` | Output feature file (required). |
| `-n N` | Channels in the group (required). |
| `-w N` | Samples per waveform (required). |
| `-d N` | Components to keep, for the whole spike (required). |
| `-p N`, `-b N`, `-a N` | Peak position and samples before/after it. All three are needed to restrict the window; otherwise the whole waveform is used. |
| `-s BYTES` | Input size; required when reading stdin. |
| `-c` | Project centred data. |
| `-g N` | Group number for the progress label. |
| `-t N` | OpenMP threads. |
| `--probe-template NAME` | `tetrode` (4 ch, 25 µm square), `octrode-linear` (8 ch, 20 µm line), `octrode-2x4`, `neuropixels1`, `neuropixels2` (N-channel slices, 2–384), `flat-linear`. Without it the template is chosen from `-n`: 4 → tetrode, 8 → octrode-linear, ≤ 16 → flat-linear, otherwise neuropixels1. |
| `--flat-linear-pitch UM` | Pitch for `flat-linear` (default 20). |
| `--channel-positions FILE` | Two columns `x y` in µm, one row per channel; overrides the template. |
| `--spatial-weight R` | Gaussian down-weighting of channel pairs farther apart than about 2R µm (default 0 = off). |
| `--varimax`, `--varimax-max-iter N`, `--varimax-tol T` | Varimax rotation of the kept components (defaults 30 iterations, 1e-6). |
| `-v`, `-h` | Verbose; help. |

---

## `ndm_pca_stderiv` — deprecated alias for `ndm_pca --method stderiv`

```bash
ndm_pca_stderiv [args]     # = ndm_pca --method stderiv [args]
```

Prints a deprecation notice and runs `ndm_pca --method stderiv` with the same
arguments. Parameters are read from the `ndm_pca` block first, then from a
legacy `ndm_pca_stderiv` block, then from `general`. The forced `--method
stderiv` overrides any `method` in the YAML, so a session that uses a suffixed
token such as `stderiv_C5` must call `ndm_pca` instead. Outputs are
`.fet.stderiv.N` and `.pca.stderiv.N`.

## See also

- [Spike alignment](alignment.md) — `ndm_alignspikes` (before PCA), `ndm_alignspikes_pca` (after clustering)
- [Spike detection](spike-detection.md) — produces the `.spk` / `.res`
- [`ndm_klustakwik`](ndm_klustakwik.md) — clusters the `.fet`
- [`.fet`](../formats/fet.md), [`.pca`](../formats/pca.md), [`.spk`](../formats/spk.md) formats; [file naming](../formats/naming.md)

---

*Part of the [ndmanager-plugins](../README.md) reference.*
