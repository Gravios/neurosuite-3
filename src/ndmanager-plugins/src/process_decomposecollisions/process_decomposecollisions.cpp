/*
 * process_decomposecollisions.cpp
 * ================================
 * Template-matching collision decomposition for spike-sorted data.
 *
 * For each spikeDetection group, reads the curated .clu.N / .res.N /
 * .spk.<method>.N files, builds per-unit mean waveform templates,
 * screens for collision candidates (spikes not well explained by any
 * single template), and fits two-component matching-pursuit decompositions.
 *
 * Algorithm
 * ---------
 * 1. Build templates: mean waveform per unit (uid >= 2 when excludeNoise).
 * 2. Screening: for each spike, compute the best all-channel Pearson r
 *    between the spike and every circularly-shifted template (correlation
 *    window optional).  Spikes with best_corr >= corrThreshold pass as
 *    single-unit events.  Remaining spikes are collision candidates.
 * 3. Matching pursuit (two passes):
 *      Pass 1: find (uid, shift) that minimises ||W - a·T(τ)||².
 *      Pass 2: find a DIFFERENT uid that minimises ||R1 - a·T(τ)||².
 *              Same-unit pairs are bursts, not collisions — excluded.
 * 4. Write binary SESSION.col.N sidecar.
 *
 * GPU acceleration (--gpu, requires CUDA):
 *   Phase 1 (screening): one cuBLAS SGEMM over all spikes.
 *   Phase 2 pass-1:      one cuBLAS SGEMM over all candidates.
 *   Phase 2 pass-2:      small per-candidate loop (CPU), different unit only.
 *
 * CPU fallback: OpenMP over spikes; automatic if CUDA unavailable.
 *
 * Usage
 * -----
 *   process_decomposecollisions  --session S  --param-file P
 *       --n-groups G  --n-channels C
 *       [--max-shift-samp N]  [--corr-window W]
 *       [--corr-threshold T]  [--residual-threshold R]
 *       [--min-snr-rms V]     [--min-spikes-template M]
 *       [--exclude-noise 0|1] [--overwrite 0|1]
 *       [--gpu 0|1]           [--n-workers N]
 *
 * Binary .col.N format: see process_decomposecollisions.h
 *
 * Copyright (C) 2025 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include "process_decomposecollisions.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#endif

#ifdef USE_CUDA
#  include <cuda_runtime.h>
#  include <cublas_v2.h>
#  define CUDA_CHECK(x)  do { cudaError_t _e=(x); if(_e!=cudaSuccess){ \
       fprintf(stderr,"CUDA error %s:%d: %s\n",__FILE__,__LINE__,\
               cudaGetErrorString(_e)); std::exit(1); } } while(0)
#  define CUBLAS_CHECK(x) do { cublasStatus_t _s=(x); if(_s!=CUBLAS_STATUS_SUCCESS){ \
       fprintf(stderr,"cuBLAS error %s:%d: %d\n",__FILE__,__LINE__,(int)_s); \
       std::exit(1); } } while(0)
#endif

// ── YAML mini-reader (no dependency on yaml-cpp in plugin) ────────────────
// Reads nSamples, peakSampleIndex, and channels list for a given group.
// Only handles the specific paths used in neurosuite-3 session files.
static GroupParams read_group_params(const std::string &yaml_path, int group_idx)
{
    GroupParams gp;
    std::ifstream f(yaml_path);
    if (!f.is_open()) return gp;

    // Minimal line-by-line YAML parser for the spikeDetection block.
    // Finds the Nth channelGroups entry (1-based) and extracts fields.
    std::string line;
    int  grp_count   = 0;
    bool in_channels = false;
    bool found_grp   = false;
    while (std::getline(f, line)) {
        // Detect start of a new channelGroup entry
        if (line.find("- channels:") != std::string::npos ||
            (line.find("channels:") != std::string::npos &&
             line.find("channelGroups") == std::string::npos)) {
            // A line like "    - channels:" marks a new group
            if (line.find("- channels:") != std::string::npos) {
                ++grp_count;
                in_channels = (grp_count == group_idx);
                found_grp   = (grp_count == group_idx);
            } else if (found_grp && in_channels) {
                // inline channel list on same line: "channels: [1,2,3]"
                auto lb = line.find('[');
                auto rb = line.find(']');
                if (lb != std::string::npos && rb != std::string::npos) {
                    std::string inner = line.substr(lb+1, rb-lb-1);
                    char *tok = std::strtok(inner.data(), ", ");
                    while (tok) { gp.channels.push_back(std::atoi(tok)); tok = std::strtok(nullptr, ", "); }
                    in_channels = false;
                }
            }
        } else if (found_grp) {
            // Parse fields within the current group
            auto extract_int = [&](const char *key) -> int {
                auto p = line.find(key);
                if (p == std::string::npos) return -1;
                auto colon = line.find(':', p);
                if (colon == std::string::npos) return -1;
                return std::atoi(line.c_str() + colon + 1);
            };
            if (line.find("nSamples:") != std::string::npos) {
                int v = extract_int("nSamples:"); if (v > 0) gp.n_samp = v;
            } else if (line.find("peakSampleIndex:") != std::string::npos) {
                int v = extract_int("peakSampleIndex:"); if (v >= 0) gp.peak_sample = v;
            } else if (in_channels) {
                // Multi-line channel list continuation: "  - 3"
                auto dash = line.find('-');
                if (dash != std::string::npos) {
                    int ch = std::atoi(line.c_str() + dash + 1);
                    gp.channels.push_back(ch);
                } else {
                    in_channels = false;
                }
            }
            // Stop when we hit the next group or leave spikeDetection
            if (grp_count > group_idx) break;
        }
    }
    return gp;
}

// ── File I/O ──────────────────────────────────────────────────────────────

#include <neurosuite/core/custody.hpp>   // shared chain-of-custody resolver

static std::vector<int64_t> read_res(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseeko(f, 0, SEEK_END);
    size_t n = static_cast<size_t>(ftello(f)) / sizeof(int64_t);
    fseeko(f, 0, SEEK_SET);
    std::vector<int64_t> v(n);
    if (n) { size_t _r = fread(v.data(), sizeof(int64_t), n, f); (void)_r; }
    fclose(f);
    return v;
}

static std::vector<int32_t> read_clu(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseeko(f, 0, SEEK_END);
    size_t total = static_cast<size_t>(ftello(f)) / sizeof(int32_t);
    fseeko(f, 0, SEEK_SET);
    if (total < 2) { fclose(f); return {}; }
    std::vector<int32_t> v(total);
    { size_t _r = fread(v.data(), sizeof(int32_t), total, f); (void)_r; }
    fclose(f);
    // v[0] = nClusters header — skip it
    return std::vector<int32_t>(v.begin() + 1, v.end());
}

// Returns flat float32 array: [n_spk][n_samp][n_sites], row-major.
static std::vector<float> read_spk(const std::string &path,
                                    int n_sites, int n_samp,
                                    size_t &n_spk_out)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) { n_spk_out = 0; return {}; }
    fseeko(f, 0, SEEK_END);
    size_t total_shorts = static_cast<size_t>(ftello(f)) / sizeof(int16_t);
    fclose(f);

    size_t stride = (size_t)n_samp * n_sites;
    n_spk_out = total_shorts / stride;
    if (n_spk_out == 0) return {};

    std::vector<int16_t> raw(n_spk_out * stride);
    FILE *f2 = fopen(path.c_str(), "rb");
    { size_t _r = fread(raw.data(), sizeof(int16_t), n_spk_out * stride, f2); (void)_r; }
    fclose(f2);

    std::vector<float> wf(n_spk_out * stride);
    for (size_t i = 0; i < n_spk_out * stride; ++i)
        wf[i] = static_cast<float>(raw[i]);
    return wf;
}

// ── Template building ─────────────────────────────────────────────────────
static std::vector<Template> build_templates(
    const std::vector<float>  &wf_all,   // (n, n_samp, n_sites)
    const std::vector<int32_t>&clu,
    int n_samp, int n_sites,
    const std::unordered_set<int> &noise_clusters,
    int min_spikes)
{
    size_t n = std::min(wf_all.size() / ((size_t)n_samp * n_sites), clu.size());
    int stride = n_samp * n_sites;

    // Gather unique units
    std::vector<int> uids;
    for (size_t i = 0; i < n; ++i) {
        int uid = clu[i];
        if (noise_clusters.count(uid)) continue;
        if (std::find(uids.begin(), uids.end(), uid) == uids.end())
            uids.push_back(uid);
    }
    std::sort(uids.begin(), uids.end());

    std::vector<Template> tmpls;
    for (int uid : uids) {
        std::vector<size_t> idx;
        for (size_t i = 0; i < n; ++i)
            if (clu[i] == uid) idx.push_back(i);
        if ((int)idx.size() < min_spikes) continue;

        Template t;
        t.uid      = uid;
        t.n_spikes = (int)idx.size();
        t.mean_wf.assign(stride, 0.f);

        for (size_t si : idx)
            for (int k = 0; k < stride; ++k)
                t.mean_wf[k] += wf_all[si * stride + k];
        for (float &v : t.mean_wf) v /= (float)idx.size();

        // L2 normalise
        float norm2 = 0.f;
        for (float v : t.mean_wf) norm2 += v * v;
        t.norm2 = norm2;
        float inv = (norm2 > 1e-12f) ? (1.f / std::sqrt(norm2)) : 0.f;
        t.mean_wf_norm.resize(stride);
        for (int k = 0; k < stride; ++k)
            t.mean_wf_norm[k] = t.mean_wf[k] * inv;

        // Dominant channel: highest mean PTP
        std::vector<float> mean_ptp(n_sites, 0.f);
        for (size_t si : idx) {
            for (int ch = 0; ch < n_sites; ++ch) {
                float mn =  1e18f, mx = -1e18f;
                for (int s = 0; s < n_samp; ++s) {
                    float v = wf_all[si * stride + s * n_sites + ch];
                    mn = std::min(mn, v); mx = std::max(mx, v);
                }
                mean_ptp[ch] += (mx - mn);
            }
        }
        for (float &v : mean_ptp) v /= (float)idx.size();
        t.dominant_ch = (int)(std::max_element(mean_ptp.begin(), mean_ptp.end())
                              - mean_ptp.begin());

        // Projection coefficient distribution: a_i = <wf_i, tmpl> / ||tmpl||²
        // This is the exact same quantity as the fitted amplitude in fit_amplitude
        // at τ=0, so the clamp is directly calibrated to what a will look like.
        std::vector<float> projs(idx.size());
        for (size_t ii = 0; ii < idx.size(); ++ii) {
            size_t si = idx[ii];
            float dot = 0.f;
            for (int k = 0; k < stride; ++k)
                dot += wf_all[si*stride + k] * t.mean_wf[k];
            projs[ii] = dot / norm2;
        }
        std::sort(projs.begin(), projs.end());
        t.amp_mean  = std::accumulate(projs.begin(), projs.end(), 0.f) / (float)projs.size();
        size_t i01  = std::max<size_t>(0, (size_t)(0.01f * projs.size()));
        size_t i99  = std::min(projs.size()-1, (size_t)(0.99f * projs.size()));
        t.amp_pct01 = projs[i01];
        t.amp_pct99 = projs[i99];

        tmpls.push_back(std::move(t));
    }
    return tmpls;
}

// ── Math helpers ──────────────────────────────────────────────────────────
static float pearson_r(const float *a, const float *b, int L)
{
    float ma = 0.f, mb = 0.f;
    for (int i = 0; i < L; ++i) { ma += a[i]; mb += b[i]; }
    ma /= L; mb /= L;
    float num = 0.f, da2 = 0.f, db2 = 0.f;
    for (int i = 0; i < L; ++i) {
        float ac = a[i] - ma, bc = b[i] - mb;
        num += ac * bc; da2 += ac * ac; db2 += bc * bc;
    }
    float denom = std::sqrt(da2 * db2);
    return (denom > 1e-12f) ? num / denom : 0.f;
}

static float parabolic_peak(const std::vector<float> &scores, int pk)
{
    if (pk <= 0 || pk >= (int)scores.size() - 1) return 0.f;
    float y0 = scores[pk-1], y1 = scores[pk], y2 = scores[pk+1];
    float denom = 2.f * (2.f*y1 - y0 - y2);
    return (std::fabs(denom) > 1e-12f) ? (y0 - y2) / denom : 0.f;
}

// ── Per-template amplitude clamp ────────────────────────────────────────────
// Returns (a_min, a_max) from the cluster's PTP distribution,
// extended 50% beyond pct01/pct99 to accommodate partial collision fits.
static std::pair<float,float> amplitude_clamp(const Template &t)
{
    if (t.amp_mean < 1e-6f) return {0.f, 3.f};
    float lo       = t.amp_pct01 / t.amp_mean;
    float hi       = t.amp_pct99 / t.amp_mean;
    float half_rng = (hi - lo) * 0.5f;
    return {std::max(0.f, lo - half_rng), hi + half_rng};
}

// Linear-shifted amplitude fit: a = <wf_overlap, tmpl_overlap> / norm2
// Returns amplitude and fills residual (same size as wf = n_samp*n_sites).
static float fit_amplitude(
    const float *wf, const float *tmpl,
    float *residual,
    int n_samp, int n_sites, float norm2, int tau,
    float a_min = 0.f, float a_max = 3.f)
{
    int sl_w_start, sl_w_end, sl_t_start;
    if (tau >= 0) {
        sl_w_start = tau; sl_w_end = n_samp; sl_t_start = 0;
    } else {
        sl_w_start = 0; sl_w_end = n_samp + tau; sl_t_start = -tau;
    }
    int length = sl_w_end - sl_w_start;
    if (length < 4 || norm2 < 1e-12f) {
        memcpy(residual, wf, (size_t)n_samp * n_sites * sizeof(float));
        return 0.f;
    }

    float dot = 0.f;
    for (int s = 0; s < length; ++s)
        for (int c = 0; c < n_sites; ++c)
            dot += wf[(sl_w_start+s)*n_sites+c] * tmpl[(sl_t_start+s)*n_sites+c];
    float a = dot / norm2;
    if (a < a_min) a = a_min;
    if (a > a_max) a = a_max;

    memcpy(residual, wf, (size_t)n_samp * n_sites * sizeof(float));
    for (int s = 0; s < length; ++s)
        for (int c = 0; c < n_sites; ++c)
            residual[(sl_w_start+s)*n_sites+c] -= a * tmpl[(sl_t_start+s)*n_sites+c];
    return a;
}

static float vector_norm2(const float *v, int n)
{
    float s = 0.f;
    for (int i = 0; i < n; ++i) s += v[i]*v[i];
    return s;
}


// ── CPU single-spike screening ────────────────────────────────────────────
// Returns Pearson r of spike vs its ASSIGNED template at τ=0 (correlation
// window applied).  The spike was detected at its peak and sorted into
// assigned_uid, so τ=0 is the natural alignment.
static float corr_vs_assigned(
    const float *wf,
    const std::vector<Template> &tmpls,
    int assigned_uid,
    int n_samp, int n_sites,
    int corr_window, int peak_idx)
{
    // Find the assigned template
    const Template *td = nullptr;
    for (const auto &t : tmpls)
        if (t.uid == assigned_uid) { td = &t; break; }
    if (!td) return -2.f;   // no template for this unit

    int c_lo, c_hi;
    if (corr_window > 0) {
        int pk = (peak_idx >= 0) ? peak_idx : n_samp / 2;
        c_lo = std::max(0, pk - corr_window);
        c_hi = std::min(n_samp, pk + corr_window + 1);
    } else {
        c_lo = 0; c_hi = n_samp;
    }
    int win_len = c_hi - c_lo;
    int L = win_len * n_sites;

    std::vector<float> a(L), b(L);
    for (int s = 0; s < win_len; ++s)
        for (int c = 0; c < n_sites; ++c) {
            a[s*n_sites+c] = wf[(c_lo+s)*n_sites+c];
            b[s*n_sites+c] = td->mean_wf_norm[(c_lo+s)*n_sites+c];  // τ=0: no roll
        }
    return pearson_r(a.data(), b.data(), L);
}

// ── CPU two-component pursuit (assigned-first) ────────────────────────────
// u1 = assigned cluster, fixed at τ=0.  Only a1 is fitted (no shift search).
// u2 = best OTHER template searched over all shifts.
static SpikeRecord decompose_one_cpu(
    int spike_i, int64_t ts,
    const float *wf,
    int assigned_uid,
    const std::vector<Template> &tmpls,
    int n_samp, int n_sites,
    int max_shift, int corr_window, int peak_idx,
    float resid_thresh)
{
    SpikeRecord rec{};
    rec.spike_idx        = spike_i;
    rec.ts               = ts;
    rec.best_single_unit = assigned_uid;
    int stride = n_samp * n_sites;
    float wf_norm = std::sqrt(vector_norm2(wf, stride));

    // BSC = correlation vs assigned template at τ=0
    rec.best_single_corr = corr_vs_assigned(
        wf, tmpls, assigned_uid, n_samp, n_sites, corr_window, peak_idx);

    if (wf_norm < 1e-12f) return rec;

    // Find the assigned template
    const Template *td1 = nullptr;
    for (const auto &t : tmpls)
        if (t.uid == assigned_uid) { td1 = &t; break; }
    if (!td1) return rec;   // assigned unit not in templates

    // ── Pass 1: fit u1 at τ=0 ─────────────────────────────────────────────
    auto [_a1_min, _a1_max] = amplitude_clamp(*td1);
    std::vector<float> R1(stride), tmp_R(stride);
    float a1 = fit_amplitude(wf, td1->mean_wf.data(), R1.data(),
                              n_samp, n_sites, td1->norm2, 0, _a1_min, _a1_max);

    // Parabolic sub-sample refinement for u1 (compute ±max_shift for interpolation)
    {
        std::vector<float> scores;
        scores.reserve(2*max_shift+1);
        for (int tau = -max_shift; tau <= max_shift; ++tau) {
            float a = fit_amplitude(wf, td1->mean_wf.data(), tmp_R.data(),
                                    n_samp, n_sites, td1->norm2, tau, _a1_min, _a1_max);
            scores.push_back(std::sqrt(vector_norm2(tmp_R.data(), stride)));
            (void)a;
        }
        rec.comp1.shift_frac = parabolic_peak(scores, max_shift); // τ=0 is at index max_shift
    }
    rec.comp1.unit_id    = assigned_uid;
    rec.comp1.shift_samp = 0;
    rec.comp1.amplitude  = a1;
    rec.comp1.amp_in_range = true;

    // ── Pass 2: search all OTHER templates with shifts ─────────────────────
    const float *R1ptr = R1.data();
    float R1_norm = std::sqrt(vector_norm2(R1ptr, stride));
    int   best2_uid = -1, best2_tau = 0;
    float best2_res = R1_norm * 2.f, best2_a = 0.f;
    std::vector<float> best2_R(stride), tmp_R2(stride);
    memcpy(best2_R.data(), R1ptr, stride * sizeof(float));

    for (const auto &t : tmpls) {
        if (t.uid == assigned_uid) continue;  // u2 must differ from u1
        auto [_a2_min, _a2_max] = amplitude_clamp(t);
        std::vector<float> scores;
        scores.reserve(2*max_shift+1);
        for (int tau = -max_shift; tau <= max_shift; ++tau) {
            float a = fit_amplitude(R1ptr, t.mean_wf.data(), tmp_R2.data(),
                                    n_samp, n_sites, t.norm2, tau, _a2_min, _a2_max);
            float res = std::sqrt(vector_norm2(tmp_R2.data(), stride));
            scores.push_back(res);
            if (res < best2_res && a > 0.f) {
                best2_res = res; best2_uid = t.uid;
                best2_tau = tau; best2_a   = a;
                memcpy(best2_R.data(), tmp_R2.data(), stride*sizeof(float));
            }
        }
        if (best2_uid == t.uid) {
            // Pure sub-sample refinement around the winning integer shift.
            // Convention (shared with comp1): total_shift = shift_samp + shift_frac.
            float frac = parabolic_peak(scores, best2_tau + max_shift);
            rec.comp2.shift_frac = frac;
        }
    }
    rec.comp2.unit_id    = best2_uid;
    rec.comp2.shift_samp = best2_tau;
    rec.comp2.amplitude  = best2_a;
    rec.comp2.amp_in_range = (best2_uid >= 0);

    float R2_norm = std::sqrt(vector_norm2(best2_R.data(), stride));
    rec.resid_norm = (wf_norm > 1e-12f) ? R2_norm / wf_norm : 1.f;
    rec.accepted   = (rec.resid_norm < resid_thresh) && (best2_uid >= 0);
    return rec;
}

// ── CPU screening + decomposition (OpenMP) ───────────────────────────────
static std::vector<SpikeRecord> process_group_cpu(
    const std::vector<float>   &wf_all,
    const std::vector<int64_t> &res,
    const std::vector<int32_t> &clu,
    const std::vector<Template>&tmpls,
    int n_samp, int n_sites, size_t n_spk,
    int max_shift, int corr_window, int peak_idx,
    float corr_thresh, float resid_thresh,
    float min_amp_abs, int n_workers)
{
    int stride = n_samp * n_sites;
    (void)n_workers;  // suppress unused-parameter warning when OpenMP absent
    // Compute RMS noise (Quiroga MAD)
    std::vector<float> absvals(n_spk * stride);
    for (size_t i = 0; i < n_spk * stride; ++i) absvals[i] = std::fabs(wf_all[i]);
    std::nth_element(absvals.begin(), absvals.begin() + absvals.size()/2, absvals.end());
    float rms = absvals[absvals.size()/2] / 0.6745f;
    // min_amp_abs here is interpreted as a MULTIPLIER on the RMS noise
    // (matches the YAML parameter name minSnrRms = "minimum SNR in units of RMS").
    // Values ≤ 0 fall back to the 4× RMS default.
    float snr_mult = (min_amp_abs > 0.f) ? min_amp_abs : 4.f;
    float min_amp = rms * snr_mult;
    fprintf(stderr, "  RMS noise ≈ %.1f  min_amp ≈ %.1f  (SNR gate = %.2f × RMS)\n",
            rms, min_amp, snr_mult);

    // Phase 1: screen — check each spike vs its assigned cluster at τ=0
    std::vector<int>   candidates;
    std::vector<float> bsc_all(n_spk, 1.f);

#ifdef _OPENMP
    int nw = (n_workers > 0) ? n_workers : omp_get_max_threads();
    omp_set_num_threads(nw);
#endif

#pragma omp parallel for schedule(dynamic, 64)
    for (int i = 0; i < (int)n_spk; ++i) {
        const float *wf = wf_all.data() + i * stride;
        float ptp = *std::max_element(wf, wf+stride) - *std::min_element(wf, wf+stride);
        if (ptp < min_amp) continue;
        int uid = (i < (int)clu.size()) ? (int)clu[i] : -1;
        float r = corr_vs_assigned(wf, tmpls, uid, n_samp, n_sites,
                                    corr_window, peak_idx);
        bsc_all[i] = r;
    }
    for (int i = 0; i < (int)n_spk; ++i) {
        int uid = (i < (int)clu.size()) ? (int)clu[i] : -1;
        if (bsc_all[i] < corr_thresh && uid >= 0) candidates.push_back(i);
    }

    fprintf(stderr, "  %zu collision candidates (%.1f %%)\n",
            candidates.size(), 100.f * candidates.size() / (float)n_spk);

    // Phase 2: decomposition (OpenMP over candidates)
    std::vector<SpikeRecord> records(candidates.size());
#pragma omp parallel for schedule(dynamic, 16)
    for (int ci = 0; ci < (int)candidates.size(); ++ci) {
        int i = candidates[ci];
        int assigned_uid = (i < (int)clu.size()) ? (int)clu[i] : -1;
        records[ci] = decompose_one_cpu(i, res[i],
            wf_all.data() + i * stride, assigned_uid,
            tmpls, n_samp, n_sites, max_shift,
            corr_window, peak_idx, resid_thresh);
    }
    return records;
}

// ── CUDA GPU path ─────────────────────────────────────────────────────────
#ifdef USE_CUDA
// Build the (K × L) matrix of all (template × shift) correlation vectors:
// circularly rolled, window-sliced, centred, L2-normalised float32 rows.

// Build (K × stride) matrix of linearly-shifted template vectors for fitting.
static void build_fit_matrix_cpu(
    const std::vector<Template> &tmpls,
    int n_samp, int n_sites, int max_shift,
    std::vector<float> &mat_out,
    std::vector<int>   &uid_out,
    std::vector<int>   &tau_out,
    std::vector<float> &norm2_out)
{
    int stride = n_samp * n_sites;
    int n_tau  = 2 * max_shift + 1;
    int K = (int)tmpls.size() * n_tau;
    mat_out.assign((size_t)K * stride, 0.f);
    uid_out.resize(K); tau_out.resize(K); norm2_out.resize(K);

    int row = 0;
    for (const auto &t : tmpls) {
        for (int tau = -max_shift; tau <= max_shift; ++tau, ++row) {
            uid_out[row] = t.uid; tau_out[row] = tau;
            norm2_out[row] = t.norm2;
            float *dst = mat_out.data() + (size_t)row * stride;
            int sl_w_start, sl_w_end, sl_t_start;
            if (tau >= 0) { sl_w_start = tau; sl_w_end = n_samp; sl_t_start = 0; }
            else          { sl_w_start = 0; sl_w_end = n_samp+tau; sl_t_start = -tau; }
            int length = sl_w_end - sl_w_start;
            if (length > 0) {
                for (int s = 0; s < length; ++s)
                    for (int c = 0; c < n_sites; ++c)
                        dst[(sl_w_start+s)*n_sites+c] =
                            t.mean_wf[(sl_t_start+s)*n_sites+c];
            }
        }
    }
}

static std::vector<SpikeRecord> process_group_gpu(
    const std::vector<float>   &wf_all,
    const std::vector<int64_t> &res,
    const std::vector<int32_t> &clu,
    const std::vector<Template>&tmpls,
    int n_samp, int n_sites, size_t n_spk,
    int max_shift, int corr_window, int peak_idx,
    float corr_thresh, float resid_thresh,
    float min_amp_abs, int n_workers)
{
    int stride = n_samp * n_sites;

    // RMS noise
    std::vector<float> absvals(n_spk * stride);
    for (size_t i = 0; i < n_spk * stride; ++i) absvals[i] = std::fabs(wf_all[i]);
    std::nth_element(absvals.begin(), absvals.begin() + absvals.size()/2, absvals.end());
    float rms = absvals[absvals.size()/2] / 0.6745f;
    float snr_mult = (min_amp_abs > 0.f) ? min_amp_abs : 4.f;
    float min_amp = rms * snr_mult;
    fprintf(stderr, "  RMS noise ≈ %.1f  min_amp ≈ %.1f  (SNR gate = %.2f × RMS)  [GPU]\n",
            rms, min_amp, snr_mult);

    // Correlation window bounds
    int c_lo = 0, c_hi = n_samp;
    if (corr_window > 0) {
        int pk = (peak_idx >= 0) ? peak_idx : n_samp / 2;
        c_lo = std::max(0, pk - corr_window);
        c_hi = std::min(n_samp, pk + corr_window + 1);
    }
    int win_len = c_hi - c_lo;
    int L = win_len * n_sites;

    // Build fitting matrix: K = n_templates * n_shifts rows, one per (template, τ)
    std::vector<float> T_fit; std::vector<int> uid_fit, tau_fit;
    std::vector<float> norm2_fit;
    build_fit_matrix_cpu(tmpls, n_samp, n_sites, max_shift,
                          T_fit, uid_fit, tau_fit, norm2_fit);
    int K = (int)uid_fit.size();

    // cuBLAS handle
    cublasHandle_t handle;
    CUBLAS_CHECK(cublasCreate(&handle));

    // ── Phase 1: screen each spike vs its assigned template at τ=0 ─────────
    // T0: n_tmpl rows, one per template at τ=0, centred + L2-normalised. (n_tmpl × L)
    int n_tmpl = (int)tmpls.size();
    std::vector<float> T0((size_t)n_tmpl * L, 0.f);
    std::vector<int>   tmpl_uid_vec(n_tmpl);
    for (int ti = 0; ti < n_tmpl; ++ti) {
        tmpl_uid_vec[ti] = tmpls[ti].uid;
        float *dst = T0.data() + (size_t)ti * L;
        for (int s = 0; s < win_len; ++s)
            for (int c = 0; c < n_sites; ++c)
                dst[s*n_sites+c] = tmpls[ti].mean_wf_norm[(c_lo+s)*n_sites+c];
        float mean = 0.f;
        for (int j = 0; j < L; ++j) mean += dst[j];
        mean /= L;
        float n2 = 0.f;
        for (int j = 0; j < L; ++j) { dst[j] -= mean; n2 += dst[j]*dst[j]; }
        float inv = (n2 > 1e-12f) ? 1.f/std::sqrt(n2) : 0.f;
        for (int j = 0; j < L; ++j) dst[j] *= inv;
    }

    // W_win: centred + L2-normalised spike windows (n_spk × L); SNR gate on CPU
    std::vector<float> W_win((size_t)n_spk * L);
    std::vector<bool>  snr_ok(n_spk, false);
    for (size_t i = 0; i < n_spk; ++i) {
        const float *wf = wf_all.data() + i * stride;
        float mx = *std::max_element(wf, wf+stride);
        float mn = *std::min_element(wf, wf+stride);
        snr_ok[i] = (mx - mn) >= min_amp;
        float *dst = W_win.data() + i * L;
        for (int s = 0; s < win_len; ++s)
            for (int c = 0; c < n_sites; ++c)
                dst[s*n_sites+c] = wf[(c_lo+s)*n_sites+c];
        float mean = 0.f;
        for (int j = 0; j < L; ++j) mean += dst[j];
        mean /= L;
        float n2 = 0.f;
        for (int j = 0; j < L; ++j) { dst[j] -= mean; n2 += dst[j]*dst[j]; }
        float inv = (n2 > 1e-12f) ? 1.f/std::sqrt(n2) : 0.f;
        for (int j = 0; j < L; ++j) dst[j] *= inv;
    }

    // GPU: corr_mat = W_win (n_spk × L) @ T0.T (L × n_tmpl) → (n_spk × n_tmpl)
    float *d_W_win = nullptr, *d_T0 = nullptr, *d_corr = nullptr;
    CUDA_CHECK(cudaMalloc(&d_W_win, (size_t)n_spk  * L      * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_T0,   (size_t)n_tmpl  * L      * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_corr, (size_t)n_spk * n_tmpl   * sizeof(float)));
    CUDA_CHECK(cudaMemcpy(d_W_win, W_win.data(), (size_t)n_spk*L*sizeof(float),   cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_T0,    T0.data(),    (size_t)n_tmpl*L*sizeof(float),  cudaMemcpyHostToDevice));

    const float alpha = 1.f, beta = 0.f;
    // col-major: C(n_tmpl × n_spk) = T0(n_tmpl×L) @ W_win.T(L×n_spk)
    CUBLAS_CHECK(cublasSgemm(handle, CUBLAS_OP_T, CUBLAS_OP_N,
        n_tmpl, (int)n_spk, L,
        &alpha, d_T0,   L,
                d_W_win, L,
        &beta,  d_corr, n_tmpl));

    std::vector<float> corr_host((size_t)n_spk * n_tmpl);
    CUDA_CHECK(cudaMemcpy(corr_host.data(), d_corr,
                           (size_t)n_spk*n_tmpl*sizeof(float), cudaMemcpyDeviceToHost));
    cudaFree(d_W_win); cudaFree(d_T0); cudaFree(d_corr);

    // Per-spike: look up corr for the assigned template only
    std::vector<float> bsc_all(n_spk, -2.f);
    for (size_t i = 0; i < n_spk; ++i) {
        int assigned = (i < clu.size()) ? (int)clu[i] : -1;
        const float *col = corr_host.data() + i * n_tmpl;
        for (int ti = 0; ti < n_tmpl; ++ti)
            if (tmpl_uid_vec[ti] == assigned) { bsc_all[i] = col[ti]; break; }
    }

    std::vector<int> candidates;
    for (size_t i = 0; i < n_spk; ++i) {
        int assigned = (i < clu.size()) ? (int)clu[i] : -1;
        if (snr_ok[i] && assigned >= 0 && bsc_all[i] < corr_thresh)
            candidates.push_back((int)i);
    }
    fprintf(stderr, "  %zu collision candidates (%.1f %%)\n",
            candidates.size(), 100.f * candidates.size() / (float)n_spk);

    if (candidates.empty()) {
        cublasDestroy(handle);
        return {};
    }

    // ── Phase 2: batch pass-1 amplitude fitting ───────────────────────────
    int n_cand = (int)candidates.size();
    int P = stride;   // n_samp * n_sites

    // Extract candidate waveforms: (n_cand, P)
    std::vector<float> wf_cand((size_t)n_cand * P);
    for (int ci = 0; ci < n_cand; ++ci)
        memcpy(wf_cand.data() + (size_t)ci*P,
               wf_all.data()  + (size_t)candidates[ci]*P,
               P*sizeof(float));

    // a1_all = wf_cand (n_cand×P) @ T_fit.T (P×K) / norm2  → (n_cand×K)
    float *d_wf_cand = nullptr, *d_T_fit = nullptr, *d_dots1 = nullptr;
    CUDA_CHECK(cudaMalloc(&d_wf_cand, (size_t)n_cand * P * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_T_fit,   (size_t)K * P * sizeof(float)));
    CUDA_CHECK(cudaMalloc(&d_dots1,   (size_t)n_cand * K * sizeof(float)));
    CUDA_CHECK(cudaMemcpy(d_wf_cand, wf_cand.data(), (size_t)n_cand*P*sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_T_fit,   T_fit.data(),   (size_t)K*P*sizeof(float),      cudaMemcpyHostToDevice));

    // Column-major: dots1(K × n_cand) = T_fit(K×P) @ wf_cand.T(P×n_cand)
    CUBLAS_CHECK(cublasSgemm(handle, CUBLAS_OP_T, CUBLAS_OP_N,
        K, n_cand, P,
        &alpha,
        d_T_fit,   P,
        d_wf_cand, P,
        &beta,
        d_dots1,   K));

    std::vector<float> dots1_host((size_t)n_cand * K);
    CUDA_CHECK(cudaMemcpy(dots1_host.data(), d_dots1,
                           (size_t)n_cand*K*sizeof(float), cudaMemcpyDeviceToHost));
    cudaFree(d_wf_cand); cudaFree(d_T_fit); cudaFree(d_dots1);
    cublasDestroy(handle);

    // Compute pass-1 residual norms: ||R1||² = ||wf||² - a1² * norm2
    // For each candidate, pick best k1 (different units excluded in pass 2)
    std::vector<SpikeRecord> records(n_cand);

#ifdef _OPENMP
    int nw = (n_workers > 0) ? n_workers : omp_get_max_threads();
    omp_set_num_threads(nw);
#endif

#pragma omp parallel for schedule(dynamic, 8)
    for (int ci = 0; ci < n_cand; ++ci) {
        int spike_i = candidates[ci];
        const float *wf = wf_all.data() + (size_t)spike_i * P;
        float wf_norm2 = vector_norm2(wf, P);
        float wf_norm  = std::sqrt(wf_norm2);

        // Pass 1: u1 = assigned cluster, τ=0 fixed
        int uid1 = (spike_i < (int)clu.size()) ? (int)clu[spike_i] : -1;
        const Template *t1 = nullptr;
        for (const auto &t : tmpls) if (t.uid == uid1) { t1 = &t; break; }

        std::vector<float> R1(P), tmp_R(P);
        float a1_actual = 0.f;
        float _ga1_min = 0.f, _ga1_max = 3.f;
        if (t1) {
            auto [_lo, _hi] = amplitude_clamp(*t1);
            _ga1_min = _lo; _ga1_max = _hi;
            a1_actual = fit_amplitude(wf, t1->mean_wf.data(), R1.data(),
                                      n_samp, n_sites, t1->norm2, 0, _ga1_min, _ga1_max);
        } else { memcpy(R1.data(), wf, P*sizeof(float)); }

        // Parabolic refinement for u1 (scan ±max_shift for sub-sample frac)
        float sf1 = 0.f;
        if (t1) {
            std::vector<float> scores1;
            for (int tau = -max_shift; tau <= max_shift; ++tau) {
                fit_amplitude(wf, t1->mean_wf.data(), tmp_R.data(),
                              n_samp, n_sites, t1->norm2, tau, _ga1_min, _ga1_max);
                scores1.push_back(std::sqrt(vector_norm2(tmp_R.data(), P)));
            }
            sf1 = parabolic_peak(scores1, max_shift);  // τ=0 at index max_shift
        }

        // Pass 2: search all OTHER templates with circular shifts
        float R1_norm = std::sqrt(vector_norm2(R1.data(), P));
        int   best2_uid = -1, best2_tau = 0; float best2_a = 0.f;
        float best2_res = R1_norm * 2.f;
        std::vector<float> best2_R(P), tmp_R2(P);
        memcpy(best2_R.data(), R1.data(), P*sizeof(float));
        float sf2 = 0.f;

        for (const auto &t : tmpls) {
            if (t.uid == uid1) continue;
            auto [_ga2_min, _ga2_max] = amplitude_clamp(t);
            std::vector<float> scores2;
            for (int tau = -max_shift; tau <= max_shift; ++tau) {
                float a = fit_amplitude(R1.data(), t.mean_wf.data(), tmp_R2.data(),
                                        n_samp, n_sites, t.norm2, tau, _ga2_min, _ga2_max);
                float res = std::sqrt(vector_norm2(tmp_R2.data(), P));
                scores2.push_back(res);
                if (res < best2_res && a > 0.f) {
                    best2_res = res; best2_uid = t.uid;
                    best2_tau = tau; best2_a   = a;
                    memcpy(best2_R.data(), tmp_R2.data(), P*sizeof(float));
                }
            }
            if (best2_uid == t.uid)
                // Pure sub-sample refinement around the winning integer shift
                // (total_shift = shift_samp + shift_frac; matches comp1 semantics).
                sf2 = parabolic_peak(scores2, best2_tau + max_shift);
        }

        float R2_norm = std::sqrt(vector_norm2(best2_R.data(), P));
        float rel_res = (wf_norm > 1e-12f) ? R2_norm / wf_norm : 1.f;
        records[ci] = SpikeRecord{
            res[spike_i], spike_i,
            uid1,
            bsc_all[spike_i],
            (rel_res < resid_thresh) && (uid1 >= 0) && (best2_uid >= 0),
            rel_res,
            Component{uid1,      0,         sf1,  a1_actual, (uid1 >= 0)},
            Component{best2_uid, best2_tau, sf2,  best2_a,   (best2_uid >= 0)},
        };
    }
    return records;
}
#endif  // USE_CUDA

// ── Output writer ─────────────────────────────────────────────────────────
static void write_col(
    const std::string &path,
    int group_idx, size_t n_spikes, bool is_stderiv, bool exclude_noise,
    const std::vector<Template>    &tmpls,
    const std::vector<SpikeRecord> &records,
    const Args &args)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) { fprintf(stderr, "error: cannot write %s\n", path.c_str()); return; }

    ColHeader hdr{};
    memcpy(hdr.magic, COL_MAGIC, 4);
    hdr.n_spikes    = (uint32_t)n_spikes;
    hdr.n_records   = (uint32_t)records.size();
    hdr.n_templates = (uint32_t)tmpls.size();
    hdr.group_idx   = (uint32_t)group_idx;
    hdr.flags = (exclude_noise ? COL_FLAG_EXCLUDE_NOISE : 0u)
              | (is_stderiv    ? COL_FLAG_STDERIV       : 0u);
    fwrite(&hdr, sizeof(hdr), 1, f);

    ColParams prm{};
    prm.corr_threshold    = args.corr_threshold;
    prm.residual_threshold= args.resid_threshold;
    prm.max_shift_samp    = args.max_shift;
    prm.min_snr_rms       = args.min_snr_rms;
    prm.min_spikes_template = args.min_spk_tmpl;
    fwrite(&prm, sizeof(prm), 1, f);

    for (const auto &t : tmpls) {
        ColTemplate ct{};
        ct.unit_id     = t.uid;
        ct.n_spikes    = t.n_spikes;
        ct.dominant_ch = t.dominant_ch;
        ct.mean_ptp    = t.amp_mean;
        ct.amp_pct01   = t.amp_pct01;
        ct.amp_pct99   = t.amp_pct99;
        fwrite(&ct, sizeof(ct), 1, f);
    }
    for (const auto &r : records) {
        ColRecord cr{};
        cr.ts               = r.ts;
        cr.spike_idx        = r.spike_idx;
        cr.best_single_unit = r.best_single_unit;
        cr.best_single_corr = r.best_single_corr;
        cr.flags = (r.accepted           ? REC_FLAG_ACCEPTED : 0u)
                 | (r.comp1.amp_in_range ? REC_FLAG_AMP1OK   : 0u)
                 | (r.comp2.amp_in_range ? REC_FLAG_AMP2OK   : 0u);
        cr.resid_norm = r.resid_norm;
        cr.u1  = r.comp1.unit_id;    cr.sh1 = r.comp1.shift_samp;
        cr.sf1 = r.comp1.shift_frac; cr.a1  = r.comp1.amplitude;
        cr.u2  = r.comp2.unit_id;    cr.sh2 = r.comp2.shift_samp;
        cr.sf2 = r.comp2.shift_frac; cr.a2  = r.comp2.amplitude;
        fwrite(&cr, sizeof(cr), 1, f);
    }
    fclose(f);
}

// ── Argument parsing ──────────────────────────────────────────────────────
static Args parse_args(int argc, char **argv)
{
    Args a;
    auto bool_of = [](const char *s) {
        return std::string(s) != "false" && std::string(s) != "0" && std::string(s) != "no";
    };
    for (int i = 1; i < argc; ++i) {
        std::string k(argv[i]);
        if (i+1 >= argc) break;
        const char *v = argv[++i];
        if      (k=="--session")            a.session      = v;
        else if (k=="--method")             a.method       = v;
        else if (k=="--param-file")         a.param_file   = v;
        else if (k=="--n-groups")           a.n_groups     = std::atoi(v);
        else if (k=="--n-channels")         a.n_channels   = std::atoi(v);
        else if (k=="--max-shift-samp")     a.max_shift    = std::atoi(v);
        else if (k=="--corr-window")        a.corr_window  = std::atoi(v);
        else if (k=="--corr-threshold")     a.corr_threshold  = std::atof(v);
        else if (k=="--residual-threshold") a.resid_threshold = std::atof(v);
        else if (k=="--min-snr-rms")        a.min_snr_rms  = std::atof(v);
        else if (k=="--min-spikes-template")a.min_spk_tmpl = std::atoi(v);
        else if (k=="--exclude-noise")      a.exclude_noise= bool_of(v);
        else if (k=="--overwrite")          a.overwrite    = bool_of(v);
        else if (k=="--gpu")                a.use_gpu      = bool_of(v);
        else if (k=="--n-workers")          a.n_workers    = std::atoi(v);
    }
    return a;
}

// ── Main ──────────────────────────────────────────────────────────────────
int main(int argc, char **argv)
{
    Args args = parse_args(argc, argv);
    if (args.session.empty() || args.n_groups < 1) {
        fprintf(stderr,
            "Usage: process_decomposecollisions\n"
            "  --session S --param-file P --n-groups G --n-channels C\n"
            "  [--max-shift-samp N] [--corr-window W]\n"
            "  [--corr-threshold T] [--residual-threshold R]\n"
            "  [--min-snr-rms V] [--min-spikes-template M]\n"
            "  [--exclude-noise 0|1] [--overwrite 0|1]\n"
            "  [--gpu 0|1] [--n-workers N]\n");
        return 1;
    }

    bool use_gpu = false;
    (void)use_gpu;   // suppressed when built without USE_CUDA
#ifdef USE_CUDA
    if (args.use_gpu) {
        int dev_count = 0;
        cudaGetDeviceCount(&dev_count);
        if (dev_count > 0) {
            use_gpu = true;
            cudaDeviceProp prop; cudaGetDeviceProperties(&prop, 0);
            fprintf(stderr, "GPU: %s  %.0f MB  SM %d.%d\n",
                prop.name, prop.totalGlobalMem/1e6f,
                prop.major, prop.minor);
        } else {
            fprintf(stderr, "  --gpu requested but no CUDA device found; using CPU\n");
        }
    }
#else
    if (args.use_gpu)
        fprintf(stderr, "  --gpu requested but binary built without CUDA; using CPU\n");
#endif

    std::unordered_set<int> noise_clusters;
    if (args.exclude_noise) { noise_clusters.insert(0); noise_clusters.insert(1); }

    int n_written = 0;

    for (int g = 1; g <= args.n_groups; ++g) {
        namespace cst = neurosuite::custody;
        // .clu is method-specific (strict); .col is method-tagged output.
        std::string clu_path = cst::methodPath(args.session, "clu", args.method, g);
        std::string out_path = cst::methodPath(args.session, "col", args.method, g);
        // .res and raw .spk are shared across methods (fall back to the
        // existing copy).
        cst::Resolved res_r = cst::resolve(args.session, "res", g, args.method);
        cst::Resolved spk_r = cst::resolve(args.session, "spk", g, args.method);
        std::string res_path = res_r.path;
        std::string spk_path = spk_r.path;

        // is_stderiv (transformed waveforms) reflects the .spk ACTUALLY resolved:
        // true only if a .spk.stderiv was found, false for a shared raw .spk.
        std::string active_spk = spk_path;
        bool is_stderiv = cst::resolvedIsStderiv(spk_r);

        if (!args.overwrite && std::ifstream(out_path).good()) {
            fprintf(stderr, "  group %d: %s exists, skipping\n", g, out_path.c_str());
            continue;
        }
        if (!std::ifstream(res_path).good() ||
            !std::ifstream(clu_path).good() ||
            !std::ifstream(active_spk).good()) {
            fprintf(stderr, "  group %d: missing input files, skipping\n", g);
            continue;
        }

        GroupParams gp = read_group_params(args.param_file, g);
        int n_samp   = gp.n_samp;
        int n_sites  = (int)gp.channels.size();
        if (n_sites == 0) n_sites = args.n_channels;

        fprintf(stderr, "  group %d: %s  n_samp=%d n_sites=%d peak=%d\n",
                g, active_spk.c_str(), n_samp, n_sites, gp.peak_sample);

        auto res_ts = read_res(res_path);
        auto clu_ids= read_clu(clu_path);
        size_t n_spk;
        auto wf_all = read_spk(active_spk, n_sites, n_samp, n_spk);

        if (wf_all.empty() || res_ts.empty()) {
            fprintf(stderr, "  group %d: empty data, skipping\n", g); continue;
        }
        size_t n = std::min({n_spk, res_ts.size(), clu_ids.size()});
        res_ts.resize(n); clu_ids.resize(n); wf_all.resize(n * n_samp * n_sites);

        auto tmpls = build_templates(wf_all, clu_ids, n_samp, n_sites,
                                     noise_clusters, args.min_spk_tmpl);
        if ((int)tmpls.size() < 2) {
            fprintf(stderr, "  group %d: fewer than 2 templates, skipping\n", g);
            continue;
        }
        fprintf(stderr, "  group %d: %zu spikes, %zu templates\n",
                g, n, tmpls.size());

        std::vector<SpikeRecord> records;
#ifdef USE_CUDA
        if (use_gpu)
            records = process_group_gpu(wf_all, res_ts, clu_ids, tmpls,
                n_samp, n_sites, n, args.max_shift,
                args.corr_window, gp.peak_sample,
                args.corr_threshold, args.resid_threshold,
                args.min_snr_rms, args.n_workers);
        else
#endif
            records = process_group_cpu(wf_all, res_ts, clu_ids, tmpls,
                n_samp, n_sites, n, args.max_shift,
                args.corr_window, gp.peak_sample,
                args.corr_threshold, args.resid_threshold,
                args.min_snr_rms, args.n_workers);

        // Sort by original spike index (GPU imap may reorder)
        std::sort(records.begin(), records.end(),
                  [](const SpikeRecord &a, const SpikeRecord &b){
                      return a.spike_idx < b.spike_idx; });

        int n_accepted = (int)std::count_if(records.begin(), records.end(),
                                             [](const SpikeRecord &r){ return r.accepted; });
        fprintf(stderr, "  group %d: %d accepted / %zu candidates\n",
                g, n_accepted, records.size());

        write_col(out_path, g, n, is_stderiv, args.exclude_noise,
                  tmpls, records, args);
        fprintf(stderr, "  Wrote %s\n", out_path.c_str());
        ++n_written;
    }

    if (n_written == 0) {
        fprintf(stderr, "  No output files written.\n"); return 1;
    }
    return 0;
}
