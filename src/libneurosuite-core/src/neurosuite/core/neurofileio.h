/***************************************************************************
 * neurofileio.h
 *
 * Centralised low-level readers/writers for the NeuroSuite on-disk formats
 * (.clu, .res, .fet, .evt, and the interleaved int16 .dat/.lfp signal files).
 *
 * Historically each application re-implemented these parsers: neuroscope's
 * providers, klusters' Data loader, and KlustaKwik's C-style readers each
 * carried their own copy, which is how format drift between the clusterer and
 * the viewers creeps in.  This module is the single source of truth.
 *
 * It is intentionally dependency-light — standard C++ only, no Qt — so every
 * consumer can use it: Qt callers bridge a QString path with toStdString(),
 * KlustaKwik uses std::string directly.  The functions are pure (a path in, a
 * value out); stateful concerns (time-window queries, display buffers, signals)
 * stay in each application's provider/loader on top of these primitives.
 *
 * Format notes (NeuroSuite conventions):
 *   .clu.N  first line is the cluster count, then one cluster id per spike.
 *   .res.N  one sample-index timestamp per spike, no header.
 *   .fet.N  first line is the feature count, then one row of ints per spike.
 *   .evt    one "<time_ms> <label>" per line; time is a float in milliseconds.
 *   .dat    interleaved int16, nbChannels values per sample, no header.
 ***************************************************************************/
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <utility>   // std::pair (eapSpikeClasses)

#include "neurosuite_core_export.h"

namespace neurofileio {

// ── .clu.N ────────────────────────────────────────────────────────────────
struct CluFile {
    int              nClusters = 0;  ///< value on the header line
    std::vector<int> ids;            ///< one cluster id per spike (header excluded)
    bool             ok = false;     ///< false on open/parse failure
    int64_t          nInFile = -1;   ///< binary .clu only: ids the file actually holds
                                     ///< ((bytes-4)/4), or -1 if it was never determined.  Set even
                                     ///< when ok is false, so a caller can report "expected N,
                                     ///< file holds M" instead of a bare read failure.
};
NEUROSUITE_CORE_EXPORT CluFile readClu(const std::string& path);
NEUROSUITE_CORE_EXPORT bool    writeClu(const std::string& path, int nClusters,
                 const std::vector<int>& ids);

// Binary .clu: int32_t cluster-count header, then nSpikes × int32_t ids.
// nSpikes must be known up front (from the matching .res — see below).
// The file length is validated: a .clu holding a different number of ids than nSpikes belongs to
// another .res (a different run, group or variant) and is REJECTED (ok = false, nInFile = the count
// found) rather than silently truncated to the first nSpikes.
NEUROSUITE_CORE_EXPORT CluFile readCluBinary(const std::string& path, int64_t nSpikes);

// Write a binary .clu: int32_t cluster-count header, then one int32_t id per
// spike (the inverse of readCluBinary).
NEUROSUITE_CORE_EXPORT bool writeCluBinary(const std::string& path, int nClusters,
                 const std::vector<int>& ids);

// ── .res.N ────────────────────────────────────────────────────────────────
NEUROSUITE_CORE_EXPORT std::vector<int64_t> readRes(const std::string& path, bool* ok = nullptr);
NEUROSUITE_CORE_EXPORT bool                 writeRes(const std::string& path,
                              const std::vector<int64_t>& times);

// Binary .res: nSpikes × int64_t timestamps, no header (nSpikes = size/8).
NEUROSUITE_CORE_EXPORT std::vector<int64_t> readResBinary(const std::string& path, bool* ok = nullptr);

// Write a binary .res: one int64_t timestamp per spike, no header.
NEUROSUITE_CORE_EXPORT bool writeResBinary(const std::string& path, const std::vector<int64_t>& times);

// ── matched .clu + .res pair (auto-detecting binary vs text) ────────────────
// NeuroSuite cluster data is a .clu/.res pair read together. Some tools write a
// binary variant for fast loading of large datasets; this detects which and
// returns the unified result so no consumer re-implements the detection.
//
// Detection (matches NeuroScope): probe the .res file — binary iff its size is
// a non-zero multiple of 8 AND its first byte is not an ASCII digit; text
// otherwise. The .clu is then read in the same format.
NEUROSUITE_CORE_EXPORT bool isBinaryClusterRes(const std::string& resPath);

struct ClusterResData {
    int                  nClusters = 0;
    std::vector<int>     ids;     ///< one cluster id per spike (.clu body)
    std::vector<int64_t> times;   ///< one timestamp per spike (.res)
    bool                 binary = false;  ///< detected format
    bool                 ok = false;      ///< false on open/parse/size mismatch
};
NEUROSUITE_CORE_EXPORT ClusterResData readClusterRes(const std::string& cluPath,
                              const std::string& resPath);

// ── .fet.N ────────────────────────────────────────────────────────────────
struct FetFile {
    int                           nFeatures = 0;
    std::vector<std::vector<int>> rows;   ///< nSpikes × nFeatures
    bool                          ok = false;
};
NEUROSUITE_CORE_EXPORT FetFile readFet(const std::string& path);

// Binary .fet: int32_t feature-count header, then nSpikes × nFeatures int64
// values, row-major. This is the layout written by the process_pca plugin and
// read by BOTH klusters (Data::loadFeatures) and KlustaKwik (KK::LoadData), so
// it is genuine cross-app duplication. nSpikes is derived from the file size.
struct FetBinaryFile {
    int                  nFeatures = 0;
    int64_t              nSpikes   = 0;
    std::vector<int64_t> values;    ///< row-major, nSpikes × nFeatures
    bool                 ok = false;
};
NEUROSUITE_CORE_EXPORT FetBinaryFile readFetBinary(const std::string& path);

// Write a binary .fet: int32_t feature-count header, then values row-major
// (nSpikes × nFeatures int64) — the inverse of readFetBinary, the layout
// process_pca writes and klusters/KlustaKwik read.  `values.size()` must be a
// whole multiple of nFeatures, else nothing is written and it returns false.
NEUROSUITE_CORE_EXPORT bool writeFetBinary(const std::string& path, int nFeatures,
                 const std::vector<int64_t>& values);

// ── .spk.N — windowed int16 waveforms (NeuroSuite) ──────────────────────────
// A .spk holds nSpikes records back to back, each record nSamples × nChannels
// int16, NO header.  Within a record the order is sample-major with channel
// varying fastest, i.e. value(spike s, sample t, channel c) sits at flat index
//     ((s * nSamples) + t) * nChannels + c
// This is the layout process_extractspikes writes and both klusters' Data and
// fiber-kit read; nSpikes is derived from the file size.  (A .wtf template file
// is byte-identical to a .spk slice, so these also read/write it — one record
// per template-series row; the companion .wti index below names each row.)
struct SpkFile {
    int                  nSamples  = 0;
    int                  nChannels = 0;
    int64_t              nSpikes   = 0;
    std::vector<int16_t> samples;   ///< nSpikes * nSamples * nChannels, see order above
    bool                 ok = false;
};
// nSamples and nChannels must be known up front (the record geometry is not in
// the file).  readSpk rejects a file whose size is not a whole number of records
// (ok = false).
NEUROSUITE_CORE_EXPORT SpkFile readSpk(const std::string& path, int nSamples, int nChannels);
// writeSpk requires samples.size() to be a whole number of nSamples*nChannels
// records, else it writes nothing and returns false.
NEUROSUITE_CORE_EXPORT bool    writeSpk(const std::string& path, int nSamples, int nChannels,
                 const std::vector<int16_t>& samples);

// ── .col.N — collision-decomposition sidecar (process_decomposecollisions) ──
// Binary, little-endian; the format is owned by process_decomposecollisions.h:
//   Header 32B: magic {'C','O','L',0x01}, n_spikes(u32), n_records(u32),
//               n_templates(u32), group(u32), flags(u32), pad[8].
//   Params 32B, then n_templates × 24B template rows, then n_records × 60B records
//   (ts i64, spike_idx i32, best_single_unit i32, best_single_corr f32, flags u32,
//    resid_norm f32, u1 i32, sh1 i32, sf1 f32, a1 f32, u2 i32, sh2 i32, sf2 f32, a2 f32).
// A record with REC_FLAG_ACCEPTED (flags bit 0) is an accepted two-component
// decomposition.  readColAccepted returns ONLY those, as (spikeIndex, the two
// components with their integer shift + amplitude) — the input the decollide
// engine applies.  Returns empty on a bad magic / short file.
struct ColDecomp { int64_t spikeIndex = -1; int u1 = 0; int sh1 = 0; double a1 = 0.0;
                                             int u2 = 0; int sh2 = 0; double a2 = 0.0; };
NEUROSUITE_CORE_EXPORT std::vector<ColDecomp> readColAccepted(const std::string& path);

// ── .wti — waveform-template index (companion to .wtf) ──────────────────────
// The template library stores a marked unit's waveform as a LINKED SERIES: not
// one global mean but a median per drift chunk (link "drift") and a median per
// spike-energy bin (link "adapt").  The waveforms themselves live in a .wtf,
// which is a headerless .spk-layout int16 stack (read with readSpk) — one record
// per series row.  The .wtf carries no structure, so this .wti names each row:
// which unit, which link, which bin, the bin's coordinates, and how many spikes
// its median was built from.  One .wti is SHARED across waveform methods (the
// row layout is identical for every method's .wtf), so it lives at a method-less
// path (<base>.wti.<group>[.<stage>]); each method keeps its own .wtf.
//
// Canonical text format (little friction for a human and for fiber-template to
// emit), version-tagged so a reader rejects anything it does not understand:
//
//     wti 1
//     nSamples 42
//     nChannels 8
//     peakSample 21            # -1 if unknown
//     sr 32552                 # 0 if unknown
//     nRows 6
//     # row unit link bin a b nSpikes
//     row 0 31 drift 0 0.000 120.000 540
//     row 1 31 drift 1 120.000 240.000 613
//     row 2 31 adapt 0 1.00 2.00 410
//     ...
//
// `link` is drift|adapt (unknown tokens tolerated and kept verbatim); `bin` is
// the 0-based ordinal within that (unit,link) series; a/b are the bin's
// coordinates (drift: chunk start/end seconds; adapt: energy lo/hi); nSpikes is
// the spike count behind that median (0 = an empty placeholder row).  Row i maps
// 1:1 to .wtf record i, so readSpk(wtf).nSpikes must equal rows.size().
struct WtiRow {
    int         row    = 0;    ///< 0-based; equals the .wtf record index
    int         unitId = 0;    ///< cluster/unit this template belongs to
    std::string link;          ///< "drift" | "adapt" (verbatim; extensible)
    int         bin    = 0;    ///< 0-based ordinal within this (unit,link) series
    double      a      = 0.0;  ///< bin coordinate lo (drift: start s; adapt: energy lo)
    double      b      = 0.0;  ///< bin coordinate hi (drift: end s;   adapt: energy hi)
    int64_t     nSpikes = 0;   ///< spikes behind this median (0 = empty placeholder)
};
struct WtiIndex {
    int                 version    = 1;
    int                 nSamples   = 0;
    int                 nChannels  = 0;
    int                 peakSample = -1;   ///< -1 if unknown
    double              sr         = 0.0;  ///< 0 if unknown
    std::vector<WtiRow> rows;
    bool                ok = false;        ///< false on open / parse / bad version
};
// Parse a .wti.  Rejects a missing file, a bad "wti <version>" line, or a version
// the reader does not implement (ok = false).  `nRows`, when present, is checked
// against the rows actually parsed.
NEUROSUITE_CORE_EXPORT WtiIndex readWti(const std::string& path);
// Write a .wti from `idx` (its version field is honoured; rows written in order).
// Returns false if the file cannot be opened.
NEUROSUITE_CORE_EXPORT bool     writeWti(const std::string& path, const WtiIndex& idx);

// ── .wti view-model helpers (one template unit's series) ────────────────────
// Trivial filters over WtiIndex.rows, factored out so a viewer and a test share
// one definition.  The row order of the result is the scrub order; each row's
// .row field is the matching .wtf record index.
//
// Distinct unit ids present in `idx`, ascending — the set of units that HAVE a
// template (fiber-template writes a series only for marked units).
NEUROSUITE_CORE_EXPORT std::vector<int> wtiUnits(const WtiIndex& idx);
// Distinct link names for `unitId`, in first-seen order (e.g. "drift", "adapt").
NEUROSUITE_CORE_EXPORT std::vector<std::string> wtiLinks(const WtiIndex& idx, int unitId);
// Rows of `idx` for (unitId, link), ascending by bin — one unit's scrubbable
// series along that link.  Empty if the unit/link is absent.
NEUROSUITE_CORE_EXPORT std::vector<WtiRow> wtiSeries(const WtiIndex& idx, int unitId,
                                                     const std::string& link);

// ── .wtl — manual template-linkage sidecar (see claude/template-curation-plan.md) ─
// Where the .wti/.wtf are the RENDERED template library (one median per row), the
// .wtl is the CURATOR'S manual lineage that the native generator re-medians as the
// refinement.  During curation the curator hand-builds each template class's
// internal structure — select spikes → median them → link medians into a per-class
// TREE — so the .wtl is a FOREST (one tree per class).  Per-group and per-stage,
// like .eap: <base>.wtl.<group>[.<stage>].
//
// One entry per NODE.  A node carries its class, its KIND, its PARENT (another
// node, or -1 for a tree root), the bin WINDOW it covers, and — the source of
// truth — the explicit SET of spike indices medianed to build it (the generator
// re-medians exactly these; the median is always the median of this set):
//   "drift-root"     a top-row median over a time region's higher-amplitude spikes
//                    (a tree root, no parent);
//   "adapt-leaf"     a lower-amplitude (within-burst adaptation) median, child of
//                    its region's drift root;
//   "collision-leaf" a collision-variant median, child of its drift root.
// Kind tokens are kept verbatim and are extensible (as with the .wti `link`).
//
// Canonical text format, version-tagged so a reader rejects anything it does not
// understand:
//
//     wtl 1
//     nNodes 3
//     # node class kind parent a b nSpikes spikes...
//     node 0 31 drift-root -1 0.000 120.000 540 12 37 59 ...
//     node 1 31 adapt-leaf 0 1.0 2.0 210 12 59 ...
//     node 2 31 collision-leaf 0 0.0 0.0 8 88 91 ...
//
// `node` is the 0-based node id (its own handle, referenced by children's
// `parent`); `class` is the .eap column / template-class id; `parent` is another
// node's id or -1; a/b are the window coordinates (drift: chunk start/end seconds;
// adapt: energy/amplitude lo/hi); `nSpikes` is how many indices follow, and those
// indices are the remainder of the line.  A node whose index count disagrees with
// its nSpikes is skipped (not fatal); `nNodes`, when present, is checked.
struct WtlNode {
    int                  node    = 0;    ///< 0-based node id (referenced by children's `parent`)
    int                  classId = 0;    ///< .eap column / template-class id this node belongs to
    std::string          kind;           ///< "drift-root" | "adapt-leaf" | "collision-leaf" (verbatim, extensible)
    int                  parent  = -1;   ///< parent node id, or -1 for a tree root
    double               a       = 0.0;  ///< window lo (drift: start s; adapt: energy/amp lo)
    double               b       = 0.0;  ///< window hi (drift: end s;   adapt: energy/amp hi)
    std::vector<int64_t> spikes;         ///< the spike indices medianed (source of truth)
};
struct WtlForest {
    int                  version = 1;
    std::vector<WtlNode> nodes;          ///< file order (a tree root precedes its leaves by convention)
    bool                 ok = false;     ///< false on open / parse / bad version
};
// Parse a .wtl.  Rejects a missing file, a bad "wtl <version>" line, or a version
// the reader does not implement (ok = false).
NEUROSUITE_CORE_EXPORT WtlForest readWtl(const std::string& path);
// Write a .wtl from `f` (its version field is honoured; nodes written in order).
// Returns false if the file cannot be opened.
NEUROSUITE_CORE_EXPORT bool       writeWtl(const std::string& path, const WtlForest& f);

// ── .wtl view-model helpers (trivial filters, shared by a viewer and its test) ──
// Distinct class ids present in `f`, ascending.
NEUROSUITE_CORE_EXPORT std::vector<int> wtlClasses(const WtlForest& f);
// Nodes of `classId` in file order (one class's tree: its roots and their leaves).
NEUROSUITE_CORE_EXPORT std::vector<WtlNode> wtlClassNodes(const WtlForest& f, int classId);
// The children of node `nodeId` (parent == nodeId), in file order.
NEUROSUITE_CORE_EXPORT std::vector<WtlNode> wtlChildren(const WtlForest& f, int nodeId);

// ── .eap — EAP membership + offset matrix (see claude/eap-template-class-design) ─
// A multi-label membership layer that augments (never appends to) the .spk: one
// ROW per spike, one COLUMN per template CLASS.  A column index IS the stable
// template-class id (pre-allocated from the spike group's nCells), so identity
// survives cluster-id churn.  Coexists with .clu (which stays the dominant-label
// partition); a collision waveform is one spk row with >= 2 classes set here.
//
// Binary, little-endian:
//   Header 32B: magic {'E','A','P',0x01}, nSpikes u32, nClasses u32 (= T),
//               group u32, flags u32, pad[12]
//   Body:       nSpikes × nClasses int8, ROW-MAJOR (cell[i][j] at i*T + j)
// Cell = the integer-sample temporal offset of class j's EAP within spike i's
// window, relative to spike i's .res; EAP_ABSENT (-128) means class j is NOT in
// spike i, so offset 0 stays valid (the dominant class's own cell is ~0).  Method
// -less + per stage.  Amplitude / fractional shift live in the .col companion,
// not here.
static constexpr int8_t EAP_ABSENT = -128;   // sentinel: class not present in this spike
struct EapFile {
    int64_t              nSpikes  = 0;
    int                  nClasses = 0;   ///< T (columns); a column index is the class id
    int                  group    = 0;
    uint32_t             flags    = 0;
    std::vector<int8_t>  cells;          ///< nSpikes * nClasses, row-major; EAP_ABSENT = none
    bool                 ok = false;
};
inline bool eapPresent(int8_t cell) { return cell != EAP_ABSENT; }
// Read / write the matrix.  writeEap requires cells.size()==nSpikes*nClasses
// (else writes nothing, returns false).  readEap rejects a bad magic / short file.
NEUROSUITE_CORE_EXPORT EapFile readEap(const std::string& path);
NEUROSUITE_CORE_EXPORT bool    writeEap(const std::string& path, int64_t nSpikes,
                 int nClasses, int group, uint32_t flags, const std::vector<int8_t>& cells);
// Pre-construct an all-absent matrix at T=nClasses (session setup), nSpikes rows.
NEUROSUITE_CORE_EXPORT bool    initEap(const std::string& path, int64_t nSpikes,
                 int nClasses, int group);
// Widen to newT columns IN MEMORY (overflow past the pre-allocation); the new
// columns are EAP_ABSENT and existing cells keep their (spike,class).  No-op if
// newT <= nClasses.  Caller writes the result back.
NEUROSUITE_CORE_EXPORT EapFile growEap(const EapFile& in, int newT);
// Query helpers (the "which spikes <-> which classes" matrix views):
//   spike indices where class j is present, ascending;
//   (class, offset) pairs present in spike i, by ascending class.
NEUROSUITE_CORE_EXPORT std::vector<int64_t> eapClassSpikes(const EapFile& e, int classId);
NEUROSUITE_CORE_EXPORT std::vector<std::pair<int,int8_t>> eapSpikeClasses(const EapFile& e,
                                                                          int64_t spike);

// ── .tcl — template-class registry (see claude/eap-template-class-design) ────
// Session+group, STAGE-INDEPENDENT (a class id means the same .eap column across
// a session's stages).  One entry per column: its lifecycle status and the
// provenance it was generated from.  Version-tagged text; the col rows are TAB
// -separated so a label may contain spaces; an empty field is written "-".
//   tcl 1
//   nClasses 128
//   # col <TAB> status <TAB> label <TAB> provenance_clu <TAB> provenance_stage <TAB> created
//   0 <TAB> active <TAB> CA1 pyr a <TAB> 23 <TAB> gt <TAB> 2026-10-02
// status: free (available) | active (live class) | tomb (deleted, id never reused)
//       | merged (folded into mergedInto; its spikes moved there).
enum class TclStatus { Free, Active, Tomb, Merged };
struct TclEntry {
    int          col           = 0;
    TclStatus    status        = TclStatus::Free;
    std::string  label;                 ///< "" if none
    int          provenanceClu = -1;    ///< -1 if none
    std::string  provenanceStage;       ///< "" if none
    std::string  created;               ///< "" if none
    int          mergedInto    = -1;    ///< survivor column when status==Merged, else -1
};
struct TclRegistry {
    int                   version  = 1;
    int                   nClasses = 0;
    std::vector<TclEntry> entries;      ///< size nClasses, indexed by col
    bool                  ok = false;
};
NEUROSUITE_CORE_EXPORT TclRegistry readTcl(const std::string& path);
NEUROSUITE_CORE_EXPORT bool        writeTcl(const std::string& path, const TclRegistry& reg);
// A fresh registry of nClasses Free slots (col i set, everything else default).
NEUROSUITE_CORE_EXPORT TclRegistry initTcl(int nClasses);

// ── .evt ──────────────────────────────────────────────────────────────────
struct EvtEntry {
    double      timeMs = 0.0;
    std::string label;
};
NEUROSUITE_CORE_EXPORT std::vector<EvtEntry> readEvt(const std::string& path, bool* ok = nullptr);
NEUROSUITE_CORE_EXPORT bool                  writeEvt(const std::string& path,
                               const std::vector<EvtEntry>& events);

// ── .dat / .lfp (interleaved int16) ─────────────────────────────────────────
// Number of samples in the file = fileSize / (nbChannels * 2). Returns -1 if
// the file cannot be opened or nbChannels <= 0.
NEUROSUITE_CORE_EXPORT int64_t datSampleCount(const std::string& path, int nbChannels);

// Read nSamples samples (each nbChannels int16) starting at sample startSample.
// `out` must hold at least nSamples*nbChannels int16. Returns the number of
// SAMPLES actually read (may be short at end-of-file), or -1 on open error.
NEUROSUITE_CORE_EXPORT int64_t readDatWindow(const std::string& path, int nbChannels,
                      int64_t startSample, int64_t nSamples, int16_t* out);

// ── Variant-aware input resolution ──────────────────────────────────────────
//
// A per-group typed input file (fet/spk/pca/res/clu …) may exist in several
// representation "variants" — most notably the stderiv-derived features.  The
// group number is ALWAYS the trailing token; the variant, when present, sits
// between the type and the group:
//
//     <base>.<type>.<group>              canonical (no variant)
//     <base>.<type>.<variant>.<group>    dotted variant      (preferred form)
//     <base>.<type><variant>.<group>     legacy glued form    (e.g. .fetD.N)
//
// The legacy glued form (a single letter glued onto the type token, as the
// stderiv pipeline historically wrote .fetD/.spkD/.pcaD) is recognised on READ
// only, for backward compatibility; new writers should emit the dotted form.
//
// resolveInput() walks `preferVariants` in order and returns the first file
// that exists.  The empty string "" denotes the canonical (no-variant) form,
// so callers express their own preference, e.g.:
//     {"", "stderiv", "D"}  → prefer canonical, else a derived representation
//     {"stderiv", "D", ""}  → prefer derived, else canonical
// For each non-empty variant the dotted form is probed first, then the legacy
// glued form.  If nothing exists, `found` is false and `path` is the canonical
// path so the caller can emit a sensible "missing input" error.
struct ResolvedInput {
    std::string path;            ///< resolved path (canonical path if !found)
    std::string variant;         ///< matched variant ("" = canonical)
    bool        dotted = false;  ///< matched the dotted (vs glued/canonical) form
    bool        found  = false;
};

NEUROSUITE_CORE_EXPORT ResolvedInput resolveInput(
    const std::string& base, const std::string& type, int group,
    const std::vector<std::string>& preferVariants);

// Convenience preference orders for the common cases.
NEUROSUITE_CORE_EXPORT std::vector<std::string> preferDerived();    ///< {"stderiv","D",""}
NEUROSUITE_CORE_EXPORT std::vector<std::string> preferCanonical();  ///< {"","stderiv","D"}

// ── Mandatory-method resolution (chain-of-custody naming) ───────────────────
//
// Under the chain-of-custody scheme every per-group artifact carries its
// extraction method as an explicit token:
//
//     <base>.<type>.<method>.<group>
//
// for type in {res, spk, clu, fet, pca} and method in
// {standard, stderiv, sdiff, ...} (the method set is open — any token is
// valid). There is no untagged, canonical, or legacy-glued form: the method
// is always known (read off the .clu anchor, or supplied by the caller) and
// the path is composed directly. Resolution is therefore deterministic — the
// file exists or it is an error — so these supersede resolveInput()/
// preferDerived()/preferCanonical(), which remain only until every caller is
// converted.

// Compose <base>.<type>.<method>.<group>.
NEUROSUITE_CORE_EXPORT std::string methodPath(
    const std::string& base, const std::string& type,
    const std::string& method, int group);

// Compose a stage-tagged path <base>.<type>.<method>.<group>[.<stage>].  `stage`
// is the trailing stage/tag (no leading dot), as written by a "Save As stage"
// and read back by parseAnchor as the suffix; an empty stage == methodPath.
NEUROSUITE_CORE_EXPORT std::string stagePath(
    const std::string& base, const std::string& type,
    const std::string& method, int group, const std::string& stage);

// Resolve a method-pinned input. `found` reflects existence; `path` is always
// the composed method path, so callers can emit a precise missing-input error.
NEUROSUITE_CORE_EXPORT ResolvedInput resolveInputForMethod(
    const std::string& base, const std::string& type, int group,
    const std::string& method);

// Parse the method token out of a per-group filename
// <base>.<type>.<method>.<group>. The base may itself contain dots, so the
// name is parsed from the right: the last field must be an all-digit group,
// and the method is the second-to-last field. Returns "" if the name does not
// match the tagged per-group shape.
NEUROSUITE_CORE_EXPORT std::string methodFromPath(const std::string& path);

}  // namespace neurofileio
