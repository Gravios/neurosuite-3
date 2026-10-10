# NeuroSuite on-disk file formats (`neurofileio`)

A developer reference for every on-disk format neurosuite-3 reads and writes. Every structural claim below — byte order, integer width, header layout, 0- vs 1-based indexing — cites `file:line` in the current working tree, overwhelmingly `src/libneurosuite-core/src/neurosuite/core/neurofileio.{h,cpp}`, the single source of truth for format parsing. Anything not read straight out of the code is marked **(unverified)**. Because this is a format spec, exact types matter: where a prior note and the code disagree, the **code wins** and the discrepancy is called out inline and collected in §6.

This is the sibling of the cluster/spike document reference, [DOCUMENT_MODEL.md](../../klusters/docs/DOCUMENT_MODEL.md); cross-references to `STANDARDIZATION.md` use its section numbers (§3.1, §3.2, §3.3, §6.4, §6.6).

> **Source note.** Written against `neurofileio.h` and `neurofileio.cpp` (the authoritative readers/writers), with cross-checks against `spike_extract.hpp`, `template_generate.hpp` and `custody.hpp` (same directory); the on-disk format docs under `doc/ndmanager-plugins/formats/` (`clu.md`, `res.md`, `fet.md`, `spk.md`, `col.md`, `eap.md`, `tcl.md`, `wti.md`, `wtf.md`, `naming.md`, `dat.md`); the NeuroScope handbook `doc/site/docs/handbook/04-file-formats.md`; the consumers `src/klusters/src/{data,klustersdoc_realign,templatelineagestore,clusterview}.cpp`, `src/libklustersshared/src/klustersshared/parameteryaml{reader,writer}.cpp`, `src/neuroscope/src/neuroscope.cpp`, and `src/ndmanager-plugins/src/process_decomposecollisions/process_decomposecollisions.cpp`; and the design notes `claude/eap-template-class-design.md` and `claude/template-curation-plan.md`. `.pca`, `.mti`/`.mtf` and the signal-file *extension recognition* live outside `neurofileio` and are flagged as such. fiber-kit is a separate repo (`github.com/Gravios/fiber-kit`) and is **not** checked out here; its `neuro_io` specifics are marked **(unverified)** (§5).

---

## 1. Formats at a glance

`neurofileio` is deliberately Qt-free, pure-function (path in, value out); stateful concerns stay in each app's provider/loader on top of it (`neurofileio.h:12-17`). The format summary in the header comment (`neurofileio.h:18-24`) covers only the classic four (`.clu`/`.res`/`.fet`/`.dat`); the full set the module actually implements is:

| Format | Text/Binary | Element type | Reader / Writer (`neurofileio`) | Purpose |
|---|---|---|---|---|
| `.dat` | binary | `int16`, channel-interleaved | `datSampleCount` / `readDatWindow` | raw wideband acquisition signal |
| `.lfp` / `.eeg` | binary | `int16`, interleaved | same as `.dat` | downsampled LFP (`.eeg` = legacy name for `.lfp`) |
| `.fil` | binary | `int16`, interleaved | same as `.dat` | high-pass-filtered signal `.spk` is cut from |
| `.res(.N)` | text **or** binary | `int64` timestamps | `readRes`/`readResBinary` · `writeRes`/`writeResBinary` | one sample-index timestamp per spike |
| `.clu(.N)` | text **or** binary | `int32` ids (+`int32` count) | `readClu`/`readCluBinary` · `writeClu`/`writeCluBinary` | per-spike cluster id; first value = cluster count |
| `.fet(.N)` | text **or** binary | **binary `int64`** / text ASCII ints | `readFet`/`readFetBinary` · `writeFetBinary` | PCA feature vectors, row-major |
| `.spk(.N)` | binary | `int16` | `readSpk`/`readSpkRecords` · `writeSpk` | windowed spike waveforms |
| `.evt` | text | ASCII (`double` ms + string) | `readEvt` · `writeEvt` | timestamped event labels |
| `.col(.N)` | **binary** | mixed `i64/i32/f32` | `readColAccepted` *(read-only)* | accepted collision decompositions |
| `.eap(.N)` | binary | `int8` | `readEap` · `writeEap`/`initEap`/`growEap` | multi-label membership + integer-offset matrix |
| `.tcl(.N)` | text | ASCII, TAB-separated rows | `readTcl` · `writeTcl`/`initTcl` | template-class lifecycle/provenance registry |
| `.wti(.N)` | text | ASCII, version-tagged | `readWti` · `writeWti` | template index; row *i* ↔ `.wtf` record *i* |
| `.wtf(.N)` | binary | `int16` | **`readSpk`/`readSpkRecords`** (no dedicated fn) | median template-waveform stack (`.spk` layout) |
| `.wtl(.N)` | text | ASCII, version-tagged | `readWtl` · `writeWtl` | manual template-lineage forest |
| `.pos` | text | ASCII, whitespace-separated | NeuroScope `PositionsProvider` (**not** `neurofileio`) | video tracking positions (adjacent) |
| `.pca(.N)` | binary | float basis | `neurosuite::core::loadPca` (**not** `neurofileio`) | PCA eigenvector basis (adjacent) |
| `.mti` / `.mtf` | text / binary | reuse `.wti` / `.spk` | `writeWti`/`writeSpk` via klusters (**not** `neurofileio`) | final committed template model (adjacent) |

**`.wtfinfo` does not exist.** A repo-wide search (`*.cpp`/`*.h`/`*.hpp`/`*.md`, excluding `build/`) returns zero hits; there is no struct, function, or doc for it. Do not document or invent one.

### 1.1 Per-group naming and chain-of-custody

Per-group artifacts carry an explicit **method** (variant) token; the group is always the trailing all-digit field (`neurofileio.h:432-468`, `naming.md`). Three shapes are recognized, newest first:

```cpp
// neurofileio.h:437-439  (resolveInput, neurofileio.cpp:895-927)
//   <base>.<type>.<group>              canonical (no variant)
//   <base>.<type>.<variant>.<group>    dotted variant   (preferred)
//   <base>.<type><variant>.<group>     legacy glued      (.fetD.N — READ only)
```

The method-pinned helpers `methodPath` → `<base>.<type>.<method>.<group>` and `stagePath` (adds a trailing `.<stage>`) compose paths directly and supersede `resolveInput`/`preferDerived`/`preferCanonical` (`neurofileio.cpp:941-951`, `neurofileio.h:470-496`); `methodFromPath` parses from the right — last field is the group, method is second-to-last — delegating to `custody::methodOf` (`neurofileio.cpp:964-970`). `custody.hpp` fixes the type classes: per-group `{res,spk,clu,clc,fet,pca,col,model,klg}` vs session-wide `{fil,dat,xml,yaml,nrs,par,eeg,lfp}` (`custody.hpp:140-152`). The EAP template family (`.eap`/`.tcl`/`.wti`) is *not* registered in `custody.hpp` and is composed by hand per-group/per-stage; only `.wtf` is method-tagged (`naming.md`, "EAP template-library family").

---

## 2. Signal files — `.dat` / `.lfp` / `.fil` / `.eeg`

All four are the same contract: headerless, little-endian `int16`, `nbChannels` values per sample interleaved (`dat.md`; `neurofileio.h:421-430`). `neurofileio` exposes two path-agnostic primitives, so the extension is irrelevant to the reader — any interleaved `int16` file works:

```cpp
// neurofileio.cpp:854-863  — samples = fileSize / (nbChannels * 2)
int64_t datSampleCount(const std::string& path, int nbChannels) {
    ...
    return static_cast<int64_t>(bytes) / (static_cast<int64_t>(nbChannels) * 2);
}
// neurofileio.cpp:872-874  — byte offset of sample `startSample`
const std::streamoff byteOff =
    static_cast<std::streamoff>(startSample) * nbChannels * 2;
```

`readDatWindow` reads `nSamples × nbChannels` `int16` from `startSample`, returning the sample count actually read (short at EOF) or −1 on open error (`neurofileio.cpp:865-885`).

- **`.eeg` is the legacy name for `.lfp`** (the downsampled LFP file). It has no `neurofileio` special-casing; recognition lives in the apps — `custody.hpp:149-152` groups `eeg`/`lfp`/`fil`/`dat` as session-wide, and NeuroScope's open filter lists `*.dat *.lfp *.eeg *.fil` (`neuroscope.cpp:1557`). The handbook treats `base.eeg` as the LFP file (`04-file-formats.md`, "raw data … `base.eeg` for local field potentials").
- **`.fil`** is the high-pass-filtered signal `.spk` snippets are extracted from; `spk.md` names `.fil` as the extraction source. It shares the `datSampleCount`/`readDatWindow` path.
- Element width is fixed at `int16` in `neurofileio`. Raw-signal **16- vs 32-bit** support (via the session's `nBits`) is a *different* feature that lives in the signal-processing plugins, not here (`doc/ndmanager-plugins/commands/signal-processing.md:276-277`); it does not apply to `.dat`/`.spk` as read by `neurofileio`.

---

## 3. The spike-sorting quintet — `.res`, `.clu`, `.fet`, `.spk`, `.evt`

All binary forms are little-endian (§3.3). Each per-group file shares one spike order: index *i* in `.res`, `.clu`, `.fet` and `.spk` is the same spike (`res.md`).

### 3.1 `.res` — timestamps (`int64`)

One sample-index timestamp per spike, **no header** (`neurofileio.h:62-71`). Text is one decimal `int64` per line (`readRes`, `neurofileio.cpp:100-115`); binary is a bare `int64` array with `nSpikes = size/8` (`readResBinary`, `neurofileio.cpp:135-156`, size must be a non-zero multiple of 8 at line 141).

Format auto-detection probes the `.res`: **binary iff its size is a non-zero multiple of 8 and its first byte is not an ASCII digit** (`isBinaryClusterRes`, `neurofileio.cpp:158-169`, verdict at line 168). `readClusterRes` runs that probe, reads both files in the detected format, and requires `clu.ids.size() == nSpikes` (`neurofileio.cpp:171-192`, guard at line 186).

### 3.2 `.clu` — cluster ids (`int32`)

First value is the **cluster count**, then one id per spike (`neurofileio.h:18-24`, `clu.md`). Text: first line = count, then one id per line (`readClu`, `neurofileio.cpp:15-35`). Binary: `int32` count header, then `nSpikes × int32` ids (`writeCluBinary`, `neurofileio.cpp:47-58`).

```cpp
// neurofileio.cpp:51-56  — int32 header, then int32 ids
const int32_t hdr = static_cast<int32_t>(nClusters);
os.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
for (int id : ids) { const int32_t v = static_cast<int32_t>(id);
    os.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
```

`readCluBinary` **validates the length before trusting the file**: a well-formed binary `.clu` is exactly `int32 header + nSpikes × int32`, and a file holding a different id count belongs to another `.res` and is **rejected** (`ok=false`, `nInFile` set to the count found) rather than silently truncated (`neurofileio.cpp:60-97`; `payload = bytes-4` at line 78, `% 4` at line 79, reject `nInFile != nSpikes` at line 81). The `nInFile` field lets a caller report "expected N, file holds M" (`neurofileio.h:41-44`). By convention `0` = noise, `1` = unsorted MUA, `≥2` = candidate units (`clu.md`), but `neurofileio` does not enforce that.

### 3.3 `.fet` — feature vectors (binary `int64`)

First line/word is the feature count, then one row per spike (`neurofileio.h:93-118`). **The binary body is `int64`, not `int32`:**

```cpp
// neurofileio.cpp:237  — row stride is int64-wide
const int64_t rowBytes = static_cast<int64_t>(sizeof(int64_t)) * out.nFeatures;
// neurofileio.cpp:245-246  — read nSpikes*nFeatures values, 8 bytes each
in.read(reinterpret_cast<char*>(out.values.data()),
        static_cast<std::streamsize>(total) * 8);
```

The header is an `int32` feature count (`readFetBinary`, `neurofileio.cpp:226-230`); `nSpikes` is derived from file size; values are `std::vector<int64_t>` (`neurofileio.h:108`). `writeFetBinary` is the exact inverse — `int32` header then `int64` values, refusing a buffer that is not a whole number of rows (`neurofileio.cpp:256-269`, 8-byte writes at line 266-267). The **text** reader parses values into `int` (32-bit) `std::vector<std::vector<int>>` (`readFet`, `neurofileio.cpp:207-214`; `FetFile::rows` at `neurofileio.h:96`) — text on disk is arbitrary-width ASCII decimal; the 32-bit width is only the in-memory text parse. The last column is the spike timestamp (`fet.md`).

> **Discrepancy — `.fet` body width.** `STANDARDIZATION.md` §3.3 (`STANDARDIZATION.md:191`) lists the `.fet.N` body as "int32 features, row-major." The authoritative binary reader/writer use **`int64`** (`neurofileio.cpp:237,245-246,266-267`; `neurofileio.h:108`), and `fet.md` agrees ("`nSpikes × nDimensions × int64_t`"). §3.3's "int32" is **stale** for the binary form.

### 3.4 `.spk` — waveforms (`int16`)

`nSpikes` records back-to-back, each `nSamples × nChannels` `int16`, **no header**; the record geometry is supplied by the caller, not stored (`neurofileio.h:120-149`). Within a record the order is **sample-major, channel fastest**:

```
// neurofileio.h:123-127
value(spike s, sample t, channel c) at flat index
    ((s * nSamples) + t) * nChannels + c
```

`readSpk` rejects a file whose size is not a whole number of `nSamples × nChannels` `int16` records, deriving `nSpikes` from the size (`neurofileio.cpp:271-303`; `recBytes` uses `sizeof(int16_t)` at line 282, whole-record check at line 285, `int16` reads at line 293-294). `readSpkRecords` seeks per record for a **0-based** index selection, O(selection) — an out-of-range index makes `ok=false` (`neurofileio.cpp:305-334`; range check at line 324-325; `neurofileio.h:140-145` states "0-based"). `writeSpk` refuses a non-whole-record buffer (`neurofileio.cpp:336-350`).

> **The `.spk` element is `int16`, not 32-bit.** The struct is `std::vector<int16_t> samples` (`neurofileio.h:133`); `spk.md` and `STANDARDIZATION.md` §3.3 (`STANDARDIZATION.md:188`) both say `int16`. No current doc claims a 32-bit `.spk`; the only 32-bit references are to raw *signal* resolution (§2).

### 3.5 `.evt` — events (text)

One `"<time_ms>\t<label>"` per line; **time is a `double` in milliseconds** and the label is the remainder of the line after the time token (`neurofileio.h:22,412-419`; `readEvt`, `neurofileio.cpp:820-842`, `timeMs` parse at line 831, label-rest at line 834-837). `writeEvt` emits `timeMs '\t' label '\n'` (`neurofileio.cpp:844-851`). The handbook corroborates floating-point-millisecond timestamps with arbitrary string labels (`04-file-formats.md`, event-file section). There is **no** `doc/ndmanager-plugins/formats/evt.md`; `.evt` is documented only in the handbook and command docs (`format-conversion.md`, `channel-manipulation.md`) — so it is a **code format with no entry in the `formats/` set** (§6).

### 3.6 On base-indexing — peak sample position

`STANDARDIZATION.md` §3.2/§6.4 (`STANDARDIZATION.md:174-181,385-389`) assert `peakPositionInWaveform` is "1-based internal, 0-based YAML," converted at the I/O boundary (reader −1, writer +1). The current file-I/O does **not** perform that conversion, and two *different* peak fields exist:

- **Session `peakSampleIndex`** (the `peakPositionInWaveform` of §3.2): read and written **verbatim**, no ±1 — `parameteryamlreader.cpp:310,461` (`nodeAs<int>(...,0)`) and `parameteryamlwriter.cpp:231,298` (`grp["peakSampleIndex"] = peakSample`). The klusters loader copies it straight through (`data.cpp:399`) and treats **`0` as "missing/invalid"** (`data.cpp:458-459`), which means the on-disk YAML value is itself used as **1-based** (there is no "0-based YAML" + `−1` step in the code).
- **`.wti peakSample`** (`neurofileio`): an independent field, a **0-based window index** ("index within the window the timestamp refers to", `spike_extract.hpp:29`, used as `timestampAt(i) - spec.peakSample` at `spike_extract.hpp:86`), stored **verbatim** with **`-1` = unknown** (`neurofileio.cpp:424,460`; default at `neurofileio.h:226`). The native template generator writes `peakSample = -1` (`template_generate.hpp:517,681`).

So the "1-based in one place, 0-based in another" tension is real but not an I/O ±1 bug: the session peak is effectively **1-based** (0 = missing) and round-trips unchanged, while the `.wti`/extraction peak is **0-based** with a `-1` sentinel. §3.2/§6.4's claimed boundary conversion is **not present** in the current reader/writer (§6).

---

## 4. The template-library family — `.eap`, `.tcl`, `.wti`, `.wtf`, `.wtl`

These implement the EAP template-class model: a stable template-class id space that coexists with `.clu` and survives cluster-id churn. The design is in `claude/eap-template-class-design.md` and `claude/template-curation-plan.md`, but the **code is authoritative**; the design notes agree with it on every point checked below (including the 32 B `.eap` header, which the notes record as "fixed at 0559 — the 0558 writer emitted 28 B"). A template-class id **is its `.eap` column index** (`neurofileio.h:335-350`).

### 4.1 `.eap` — membership + offset matrix (binary `int8`)

Little-endian; 32-byte header then an `nSpikes × nClasses` `int8` row-major body:

```
// neurofileio.h:343-349  (readEap neurofileio.cpp:628-656; writeEap :658-678)
Header 32 B: magic {'E','A','P',0x01}, nSpikes u32, nClasses u32 (= T),
             group u32, flags u32, pad[12]
Body:        nSpikes × nClasses int8, ROW-MAJOR   (cell[i][j] at i*T + j)
```

`readEap` validates the magic `{'E','A','P',0x01}`, reads four `u32` fields, then **seeks 12 pad bytes to complete the 32 B header** (`neurofileio.cpp:636-641`); `writeEap` writes the 4-byte magic + four `u32` + `pad[12]` = 32 B (`neurofileio.cpp:666-673`). A cell is the **integer-sample temporal offset** of class *j*'s EAP within spike *i*'s window relative to spike *i*'s `.res` time; **`EAP_ABSENT = -128`** means class *j* is not present, so offset `0` stays valid (`neurofileio.h:351,360`; `eap.md` gives the live range `[-127,+127]`). `initEap` pre-builds an all-`EAP_ABSENT` matrix at `T = nClasses` (`neurofileio.cpp:680-685`); `growEap` widens `T` in memory, new columns absent, no-op if already wide enough (`neurofileio.cpp:687-698`). The matrix **augments** the `.spk` (one row per existing waveform) and is per-stage, method-less (`eap.md`). Amplitude and sub-sample shift are **not** here — they live in `.col` (`neurofileio.h:349-350`).

### 4.2 `.tcl` — template-class registry (version-tagged text)

Session+group, **stage-independent** (a class id means the same `.eap` column across stages). Header `tcl 1` (only v1 accepted), an `nClasses` line, then one **TAB-separated** row per column (`neurofileio.cpp:734-788`; header check at line 750, TAB split at line 762):

```
// neurofileio.cpp:796  — written header
# col<TAB>status<TAB>label<TAB>provenance_clu<TAB>provenance_stage<TAB>created
```

`status` is `free | active | tomb | merged:<col>` (`neurofileio.cpp:723-732,768-774`); a tombed id is never reused; `merged:<col>` names the survivor. Empty fields are written `-` (`neurofileio.cpp:775-801`). `initTcl` creates `nClasses` `Free` slots (`neurofileio.cpp:806-818`). `readTcl`/`writeTcl` are exercised via `templateclassstore.cpp:50` and `process_decomposecollisions.cpp:1031`.

### 4.3 `.wti` — template index (version-tagged text)

One row per `.wtf` record; **method-less** (shared across waveform methods) because the row layout is identical for every method's `.wtf` (`neurofileio.h:166-211`). First non-blank line must be `wti <version>` with **version 1 or 2** (`readWti`, `neurofileio.cpp:395-446`; version gate at line 417). Key/value lines set `nSamples`, `nChannels`, `peakSample`, `sr`, `nRows`; each `row` line is:

```
// neurofileio.h:204-206 ; parse at neurofileio.cpp:431-433
row <row> <unit> <link> <bin> <a> <b> <nSpikes> [<parent>]   # <parent> = v2 only
```

`row` is **0-based and equals the matching `.wtf` record index** (`neurofileio.h:213`). `link` is `drift|adapt|collision`, unknown tokens kept verbatim (`neurofileio.h:215`). **v2 appends a `parent` column** (lineage parent's `.wti` row, `-1` = root); `writeWti` emits v2 **only** when some row sets `parent ≥ 0`, else v1 byte-identical to pre-v2 output (`neurofileio.cpp:448-473`, version decision at line 455-456). If present, a declared `nRows` is checked against the rows parsed (`neurofileio.cpp:442-443`). Unknown keys are ignored so the format can gain fields (`neurofileio.cpp:437-438`).

### 4.4 `.wtf` — template waveform stack (binary `int16`)

**There is no `readWtf`.** A `.wtf` is byte-identical to a `.spk` slice — one record per `.wti` row, same sample-major `int16` layout — so it is read with the `.spk` reader (`neurofileio.h:127-128`; `wtf.md`). In klusters the lineage store reads `.wtf`/`.spk` records through `neurofileio::readSpkRecords` (`templatelineagestore.cpp:288`). The headerless `.wtf` relies on the companion `.wti` header for geometry (`wtf.md`); `readSpk(wtf).nSpikes` must equal `wti.rows.size()` (`neurofileio.h:196`).

### 4.5 `.wtl` — manual template-lineage forest (version-tagged text)

The curator's hand-built lineage (a **forest**, one tree per class) that the generator re-medians (`neurofileio.h:254-301`). Header `wtl <version>`, **version 1 or 2** (`readWtl`, `neurofileio.cpp:506-576`; gate at line 528); then `nSamples`/`nChannels`/`nNodes`, and per node:

```
// neurofileio.h:280-286  (node then its running-summary lines)
node <node> <class> <kind> <parent> <a> <b> <count>
mean <N> ...        # N = nSamples*nChannels floats (0 = placeholder)
std  <N> ...
```

`kind` is `drift-root | adapt-leaf | collision-leaf`, extensible/verbatim (`neurofileio.h:305`). **v2 stores running `(mean, std, count)` summaries, not spike indices** — a node folds a new selection in by exact parallel-variance combine, so originating spikes are never kept (`neurofileio.h:296-301`). **v1 (spike-list) files still read**: the reader consumes and discards the trailing spike indices, mapping the node to `count = nSpikes` with empty mean/std (`neurofileio.cpp:545-551`). `writeWtl` **always emits `wtl 2`** (`neurofileio.cpp:582`). A declared `nNodes` is checked (`neurofileio.cpp:571-573`). Consumed by klusters at `templatelineagestore.cpp:22`. There is **no `formats/wtl.md`** — `.wtl` is documented in the `neurofileio.h` header and `claude/template-curation-plan.md` only (§6).

### 4.6 `.col` — collision-decomposition sidecar (**binary**, read-only here)

`readColAccepted` reads a little-endian binary file owned by `process_decomposecollisions` (`neurofileio.h:151-164`):

```cpp
// neurofileio.cpp:363-371
unsigned char magic[4]; in.read(...magic, 4);
if (!in || magic[0]!='C'||magic[1]!='O'||magic[2]!='L'||magic[3]!=0x01) return out;
rdU32(nSpikes); rdU32(nRecords); rdU32(nTemplates); rdU32(group); rdU32(flags);
in.seekg(8,  std::ios::cur);   // header pad[8]  -> 32 B header
in.seekg(32, std::ios::cur);   // ColParams (32 B)
in.seekg(nTemplates * 24, std::ios::cur);  // ColTemplate[] (24 B each)
```

Then `nRecords` × 60 B records (`ts i64, spikeIdx i32, best_single_unit i32, best_single_corr f32, flags u32, resid_norm f32`, then two components each `u i32, sh i32, sf f32, a i32→f32`), parsed at `neurofileio.cpp:376-391`. **Only records with `REC_FLAG_ACCEPTED` (flags bit 0)** are returned, as `(spikeIndex, u1/sh1/a1, u2/sh2/a2)` — the decollide engine's input (`neurofileio.cpp:374,383-389`). Bad magic / short file → empty. The binary writer is `process_decomposecollisions.cpp` (it emits the `COL\x01` magic).

> **`.col` is binary, not YAML.** The authoritative reader parses a **binary** format with a `{'C','O','L',0x01}` magic (`neurofileio.cpp:363-391`); `STANDARDIZATION.md` §3.3 ("32B + 32B params | template table + record table") and — since the 2026-10-10 reconciliation — `col.md` both describe this layout. An earlier `col.md` draft sketched a `collisions:`/`spikes:` YAML document that no in-repo reader consumed; it has been rewritten to the binary layout (§6).

### 4.7 Adjacent: `.pca`, `.mti`/`.mtf`, `.pos`

- **`.pca`** (binary eigenvector basis, `pca.md`) is **not** parsed by `neurofileio`; it is read by `neurosuite::core::loadPca` (`klustersdoc_realign.cpp:386`). Its validity/geometry bound is discussed in §6 (the §6.6 item).
- **`.mti` / `.mtf`** are the curator's *final committed model* files. They **reuse the `.wti` v2 and `.spk` on-disk schemas** (`writeWti`/`writeSpk`) and are written by klusters' `renderLineageToFiles`, not by a dedicated `neurofileio` function (`claude/template-curation-plan.md` §10: "`.mti`/`.mtf` reuse the `.wti` v2 / `.spk` on-disk schema … the distinction is role/lifecycle, not format"). Downstream consumers of the *final* templates should read `.mti`/`.mtf`; the distinction from the auto-generated `.wti`/`.wtf` library is lifecycle, not byte layout. **(unverified)** — no `.mti`/`.mtf` path is in `neurofileio`.
- **`.pos`** (ASCII video-tracking positions) is **not** a `neurofileio` format; NeuroScope reads it with `PositionsProvider` (`positionsprovider.cpp:48-124`) via File ▸ "Load Position File" (`neuroscopedoc.cpp:2055`). Format spec in [`pos.md`](../../../doc/ndmanager-plugins/formats/pos.md); user-facing description in the handbook §Position File.

---

## 5. fiber-kit mirroring

fiber-kit is a **separate repository** (`github.com/Gravios/fiber-kit`) and is **not checked out in this workspace**, so no fiber-kit `file:line` is cited here. What is verifiable from the in-repo code is the **contract** fiber-kit's `neuro_io` is required to mirror:

- The `.spk` layout is shared verbatim: "the layout `process_extractspikes` writes and **both klusters' `Data` and fiber-kit read**" (`neurofileio.h:126-128`).
- The `.eap` and `.tcl` formats are explicitly dual-reader contracts: the `.eap` header comment and `eap.md` state the matrix is "owned by `neurofileio` … with a fiber-kit reader/writer for the same contract," filled by "fiber-kit's `fiber-decollide`" and read by "fiber-kit's `fiber-template --eap`" (`claude/eap-template-class-design.md` §3; `eap.md`).
- `.wti`/`.wtf` are "written by fiber-kit's `fiber-template`" (`wti.md`, `wtf.md`), and `.wti`'s v2 bump was made deliberately additive so fiber-kit's trailing-column reader tolerates it (`neurofileio.h:208-210`).

**Known fiber-kit divergence (mark as living in that repo):** the design notes record that fiber-kit's `neuro_io.read_wtl`/`write_wtl` **still speak `.wtl` v1 (spike lists)** and need the v2 running-summary payload before fiber-kit can read ns3's v2 files — "a **separate fiber-kit patch**, never cross-applied from ns3" (`claude/template-curation-plan.md` §4.1). Treat this, and any other fiber-kit-side specifics, as **(unverified)** here: confirm them against the fiber-kit repo, not this one. The rule of thumb is that `neurofileio.{h,cpp}` is the on-disk contract of record; fiber-kit's job is to match it byte-for-byte, and the one place the two are currently known to differ is `.wtl` v1-vs-v2.

---

## 6. Doc-vs-code discrepancies (consolidated)

| # | Item | What the doc says | What the code does | `file:line` (code) | Recommended fix |
|---|---|---|---|---|---|
| 1 | `.fet` binary body width | §3.3 table: "int32 features, row-major" (`STANDARDIZATION.md:191`) | `int64` values; `int32` header only | `neurofileio.cpp:237,245-246,266-267`; `neurofileio.h:108` | Correct §3.3 to `int64` body (matches `fet.md`). |
| 2 | `.col` representation | `col.md`: a **YAML** `collisions:`/`spikes:` document | **binary** LE, magic `{'C','O','L',0x01}`, 32 B header + 32 B params + 24 B templates + 60 B records | `neurofileio.cpp:363-391`; `neurofileio.h:151-164` | Rewrite `col.md` to the binary layout (§3.3 already matches); or retire the YAML writer if truly unused. |
| 3 | Peak-sample base / conversion | §3.2/§6.4: `peakPositionInWaveform` 1-based internal, **0-based YAML**, reader `−1` / writer `+1` | No ±1 anywhere: session `peakSampleIndex` round-trips verbatim and `0` means "missing" (so effectively 1-based); `.wti peakSample` is a distinct **0-based** window index, `-1`=unknown | YAML: `parameteryamlreader.cpp:310,461`, `parameteryamlwriter.cpp:231,298`, `data.cpp:399,458-459`; `.wti`: `neurofileio.cpp:424,460`, `spike_extract.hpp:29,86` | Drop the "0-based YAML / ±1 boundary" claim; document the two peak fields separately (session = 1-based, `.wti`/extraction = 0-based with `-1` sentinel). |
| 4 | `pca.nCh ≤ 64` reader bound | §6.6: klusters' PCA reader silently invalidates when `nCh > 64`, "not yet fixed" (`STANDARDIZATION.md:398-402`) | **Lifted/reshaped.** The realign path bounds `nCh` against the group's own `nChan` (relative, not a fixed 64) and caps only `nComp > 64`. `neurofileio`'s `.fet` reader has **no** such bound. | `klustersdoc_realign.cpp:382-396` (check at line 388); `neurofileio.cpp:220-254` | Mark §6.6 resolved for the channel dimension; note the residual cap is on `nComp`, and `.pca` is read by `core::loadPca`, not `neurofileio`. |
| 5 | `.spk` sample width | (prior concern: "doc may say 32-bit") | `int16` everywhere | `neurofileio.cpp:282,293-294`; `neurofileio.h:133` | None — no doc claims 32-bit; `spk.md` and §3.3 already say `int16`. |
| 6 | `.wtfinfo` | — | does not exist (0 hits repo-wide) | n/a | Do not create or document it. |
| 7 | `.wtl` / `.evt` doc home | — | real `neurofileio` formats with **no `formats/` entry** (`wtl.md`, `evt.md` absent) | `neurofileio.cpp:506-600` (wtl), `:820-851` (evt) | Add `formats/wtl.md` and `formats/evt.md`, or cross-link the header/handbook. |
| 8 | `.pos` | handbook describes an ASCII x/y position file | **matches** — read by NeuroScope's `PositionsProvider` (not `neurofileio`); the handbook is accurate | `positionsprovider.cpp:48-124`, `neuroscopedoc.cpp:2055`; handbook `04-file-formats.md` §Position File | **Not a discrepancy** (the original audit searched only `neurofileio` + the `QPointF pos` GUI symbol). Documented in `formats/pos.md`. |

**Status (reconciled 2026-10-10).** The doc-side fixes above have been applied in the companion patches — §3.3 `.fet`/`.fetD` body corrected to `int64` (item 1); `col.md` rewritten to the binary `COL\x01` layout (item 2); §3.2 rewritten and §6.4 corrected to the two-peak-field, no-±1 reality (item 3); §6.6 marked resolved (item 4); and `formats/wtl.md` + `formats/evt.md` added and indexed (item 7). Items 5–6 needed no change. **Item 8 was a false flag** — `.pos` *is* read, by NeuroScope's `PositionsProvider` (`positionsprovider.cpp`); the original audit searched only `neurofileio` and the `QPointF pos` GUI symbol and missed it. The handbook is accurate and the format is now documented in `formats/pos.md`. **No residual remains.** The table above records the as-found audit, including that correction.

---

## 7. Cross-check against `STANDARDIZATION.md`

| Claim | Verdict | Evidence |
|---|---|---|
| §3.3 `.res.N` = `int64` timestamps, no header | **Holds** | `neurofileio.cpp:135-156` (size %8, `n=bytes/8`) |
| §3.3 `.spk.N` = `int16` sample-major, no header | **Holds** | `neurofileio.cpp:282,293-294`; order `neurofileio.h:123-127` |
| §3.3 `.clu.N` = `int32` count + `int32` ids | **Holds** | `neurofileio.cpp:51-56,83-93` |
| §3.3 `.fet.N` body width | **Holds** — corrected 2026-10-10 to `int64` | `neurofileio.cpp:237,266-267`; `neurofileio.h:108` |
| §3.3 `.col.N` = binary (32 B hdr + 32 B params + tables) | **Holds** (`col.md` rewritten to match, 2026-10-10) | `neurofileio.cpp:363-391` |
| §3.3 little-endian throughout | **Holds** (all binary readers use native `reinterpret_cast` reads on LE targets) | e.g. `neurofileio.cpp:84,130,245,358-361` |
| §3.1 spike invariant `.spk[i]` peak ≡ `.fil` at `.res[i]` | **Consistent** with the window-offset model (`spike_extract.hpp` derives the timestamp as `sample − peakSample`) | `spike_extract.hpp:86`; (realign preservation is a klusters concern, see [DOCUMENT_MODEL.md](../../klusters/docs/DOCUMENT_MODEL.md) §7.4) |
| §3.2/§6.4 peak position | **Holds** — §3.2/§6.4 rewritten 2026-10-10: verbatim round-trip, two distinct peak fields | item 3, §6 |
| §6.6 `nCh` PCA bound | **Holds** — §6.6 marked resolved 2026-10-10 (bound now relative to `nChan`) | `klustersdoc_realign.cpp:382-396` |

### Cautions for future editors

1. **The code is the format spec.** A binary `.col` with a `COL\x01` magic and an `int64`-bodied `.fet` are what ship; the former `col.md` YAML sketch and §3.3's `.fet` "int32" row were reconciled to the code on 2026-10-10. When a doc and the code drift again, fix the doc toward the code, never the reverse.
2. **`.wtf` has no reader of its own** — it is a `.spk` by another name, read with `readSpk`/`readSpkRecords`. Do not add a parallel `readWtf`; keep `.wti.rows.size() == readSpk(wtf).nSpikes` as the invariant.
3. **Two peak fields, two bases.** Session `peakSampleIndex`/`peakPositionInWaveform` is 1-based (`0` = missing) and must round-trip unchanged; `.wti peakSample` is a 0-based window index with `-1` = unknown. Do not "fix" one into the other, and do not reintroduce a ±1 at the YAML boundary.
4. **`growEap`/`initEap` own the 32 B header.** `readEap` seeks exactly `pad[12]` after four `u32`s; a writer that emits a 28 B header (as the original 0558 writer did) silently corrupts every consumer. Keep the 32 B assertion.
5. **`writeWti` chooses v1/v2 by content, `writeWtl` always writes v2.** Preserve `writeWti`'s "v2 only if some `parent ≥ 0`" rule so clu-keyed libraries stay byte-identical to pre-v2; and remember fiber-kit's `.wtl` reader still expects v1 (§5) — coordinate any `.wtl` change with the separate fiber-kit patch.
6. **`.pos` is read by NeuroScope, not `neurofileio`; `.eeg` is `.lfp`.** The position file is parsed by NeuroScope's `PositionsProvider` (`positionsprovider.cpp`) — see `formats/pos.md` — so it is adjacent (§4.7), not absent. `.eeg`/`.lfp`/`.fil` all go through `datSampleCount`/`readDatWindow`, with extension recognition in the apps (`custody.hpp:149-152`, `neuroscope.cpp:1557`), not in `neurofileio`.
7. **`.pca` is not a `neurofileio` format.** Its reader and the (now relative) channel-count sanity bound live in `klustersdoc_realign.cpp`/`core::loadPca`; `neurofileio`'s `.fet` reader intentionally imposes no channel cap.
