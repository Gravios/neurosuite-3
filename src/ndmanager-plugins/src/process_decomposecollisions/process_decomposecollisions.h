// process_decomposecollisions.h
// Binary .col.N format constants and shared types.
#pragma once
#include <cstdint>
#include <vector>
#include <string>

// ── .col.N binary format (little-endian) ─────────────────────────────────
// Header  32 B: magic(4) n_spikes(4) n_records(4) n_templates(4)
//               group_idx(4) flags(4) pad(8)
// Params  32 B: corr_thresh(f32) resid_thresh(f32) max_shift(i32)
//               min_snr(f32) min_spikes_tmpl(i32) pad(12)
// Templates  n_templates × 24 B: uid(i32) n_spikes(i32) dom_ch(i32)
//               mean_ptp(f32) amp_pct01(f32) amp_pct99(f32)
// Records    n_records  × 60 B: ts(i64) idx(i32) bsu(i32) bsc(f32)
//               flags(u32) resnorm(f32)
//               u1(i32) sh1(i32) sf1(f32) a1(f32)
//               u2(i32) sh2(i32) sf2(f32) a2(f32)
//
// Shift convention (uniform for comp1 and comp2):
//     total_shift_samples = shift_samp + shift_frac
// where shift_samp is the best integer shift found by the coarse search
// and shift_frac is a pure fractional offset in [-0.5, 0.5] from
// parabolic interpolation around the three scores nearest shift_samp.
// Note: prior builds stored sf2 = sh2 + frac (integer included in the
// fractional field).  Consumers of .col files written before 2026-04-19
// should subtract sh2 from sf2 to recover the pure sub-sample offset,
// or re-run process_decomposecollisions.

static constexpr uint8_t  COL_MAGIC[4] = {'C','O','L','\x01'};
static constexpr uint32_t COL_FLAG_EXCLUDE_NOISE = 1u;
static constexpr uint32_t COL_FLAG_STDERIV       = 2u;
static constexpr uint32_t REC_FLAG_ACCEPTED = 1u;
static constexpr uint32_t REC_FLAG_AMP1OK   = 2u;
static constexpr uint32_t REC_FLAG_AMP2OK   = 4u;

#pragma pack(push, 1)
struct ColHeader {
    uint8_t  magic[4];
    uint32_t n_spikes;
    uint32_t n_records;
    uint32_t n_templates;
    uint32_t group_idx;
    uint32_t flags;
    uint8_t  pad[8];
};
struct ColParams {
    float    corr_threshold;
    float    residual_threshold;
    int32_t  max_shift_samp;
    float    min_snr_rms;
    int32_t  min_spikes_template;
    uint8_t  pad[12];
};
struct ColTemplate {
    int32_t  unit_id;
    int32_t  n_spikes;
    int32_t  dominant_ch;
    float    mean_ptp;
    float    amp_pct01;
    float    amp_pct99;
};
struct ColRecord {
    int64_t  ts;
    int32_t  spike_idx;
    int32_t  best_single_unit;
    float    best_single_corr;
    uint32_t flags;
    float    resid_norm;
    int32_t  u1;
    int32_t  sh1;
    float    sf1;
    float    a1;
    int32_t  u2;
    int32_t  sh2;
    float    sf2;
    float    a2;
};
#pragma pack(pop)
static_assert(sizeof(ColHeader)   == 32, "ColHeader must be 32 bytes");
static_assert(sizeof(ColParams)   == 32, "ColParams must be 32 bytes");
static_assert(sizeof(ColTemplate) == 24, "ColTemplate must be 24 bytes");
static_assert(sizeof(ColRecord)   == 60, "ColRecord must be 60 bytes");

// ── Runtime types ─────────────────────────────────────────────────────────
struct Template {
    int         uid;
    int         n_spikes;
    int         dominant_ch;
    float       amp_mean;
    float       amp_pct01;
    float       amp_pct99;
    float       norm2;          // ||mean_wf||²
    // mean_wf and mean_wf_norm stored flat: [n_samp * n_sites]
    std::vector<float> mean_wf;       // (n_samp, n_sites) row-major
    std::vector<float> mean_wf_norm;  // L2-normalised version of mean_wf
};

struct Component {
    int   unit_id  = -1;
    int   shift_samp = 0;
    float shift_frac = 0.f;
    float amplitude  = 0.f;
    bool  amp_in_range = false;
};

struct SpikeRecord {
    int64_t   ts;
    int32_t   spike_idx;
    int32_t   best_single_unit;
    float     best_single_corr;
    bool      accepted;
    float     resid_norm;
    Component comp1, comp2;
};

struct GroupParams {
    int              n_samp       = 52;
    int              peak_sample  = 26;
    std::vector<int> channels;
};

struct Args {
    std::string session;
    std::string param_file;
    std::string method      = "standard";  // chain-of-custody method tag
    int    n_groups         = 1;
    int    n_channels       = 4;
    int    max_shift        = 10;
    int    corr_window      = 0;
    float  corr_threshold   = 0.85f;
    float  resid_threshold  = 0.25f;
    float  min_snr_rms      = 4.0f;
    int    min_spk_tmpl     = 30;
    bool   exclude_noise    = true;
    bool   overwrite        = false;
    bool   use_gpu          = false;
    int    n_workers        = 0;   // 0 = all CPUs
};
