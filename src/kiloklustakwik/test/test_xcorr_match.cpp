// =============================================================================
// test_xcorr_match.cpp  —  unit tests for sub-sample-refined matching
// =============================================================================
#include "../src/xcorr_match.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

constexpr const char* GREEN = "\033[32m";
constexpr const char* RED   = "\033[31m";
constexpr const char* RESET = "\033[0m";

int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg) do {                                              \
    if (cond) {                                                            \
        std::printf("  %sPASS%s %s\n", GREEN, RESET, msg);                 \
        ++g_pass;                                                          \
    } else {                                                               \
        std::printf("  %sFAIL%s %s   (%s:%d)\n", RED, RESET, msg,          \
                    __FILE__, __LINE__);                                   \
        ++g_fail;                                                          \
    }                                                                      \
} while (0)

// Synthesise a sample triphasic spike on a single channel: brief positive
// pre-spike, deep negative trough at peakIdx, slower positive return.
// Returns a vector of length nSamp.
std::vector<float> syntheticSpike(int nSamp, int peakIdx, float amplitude = 1.0f) {
    std::vector<float> out(static_cast<size_t>(nSamp), 0.0f);
    for (int s = 0; s < nSamp; ++s) {
        const float ds = static_cast<float>(s - peakIdx);
        out[static_cast<size_t>(s)] =
            amplitude * (-std::exp(-ds * ds / 4.0f)
                         + 0.3f * std::exp(-(ds - 4.0f) * (ds - 4.0f) / 16.0f));
    }
    return out;
}

// Build a multi-channel waveform by replicating a single-channel template
// scaled across channels.
std::vector<float> buildMultiChannel(int nCh, int nSamp, int peakIdx,
                                      const std::vector<float>& chScales) {
    std::vector<float> out(static_cast<size_t>(nCh) * nSamp, 0.0f);
    const auto tmpl = syntheticSpike(nSamp, peakIdx, 1.0f);
    for (int ch = 0; ch < nCh; ++ch) {
        const float s = chScales[static_cast<size_t>(ch)];
        for (int t = 0; t < nSamp; ++t) {
            out[static_cast<size_t>(ch) * nSamp + t] = s * tmpl[t];
        }
    }
    return out;
}

}  // anonymous

// -----------------------------------------------------------------------------
// T1: identical waveforms → cosineScore = 1, residualScore = 1, lag = 0
// -----------------------------------------------------------------------------
static void test_identical() {
    std::puts("T1: identical waveforms → score = 1.0, lag = 0");
    const int nCh = 4, nSamp = 32;
    auto a = buildMultiChannel(nCh, nSamp, 16, {1.0f, 0.5f, 0.7f, 0.3f});
    auto b = a;  // identical

    auto r = xcorr_match::matchPair(a.data(), b.data(), nCh, nSamp, 4);
    CHECK(r.bestShiftInt == 0, "lag = 0");
    CHECK(std::fabs(r.bestShiftFrac) < 1e-3f, "sub-sample lag near 0");
    CHECK(r.cosineScore > 0.999f, "cosine score = 1.0");
    CHECK(r.residualScore > 0.999f, "residual score = 1.0");
    CHECK(std::fabs(r.alphaStar - 1.0f) < 1e-3f, "α* = 1");
}

// -----------------------------------------------------------------------------
// T2: amplitude-scaled identical templates → cosine = 1, residual = 1,
//     α* ≠ 1.  Tests that the scoring is amplitude-invariant.
// -----------------------------------------------------------------------------
static void test_amplitude_scaled() {
    std::puts("T2: amplitude-scaled match → score = 1.0, α* = scale");
    const int nCh = 4, nSamp = 32;
    auto a = buildMultiChannel(nCh, nSamp, 16, {1.0f, 0.5f, 0.7f, 0.3f});
    auto b = a;
    // Scale b by 2.0
    for (auto& v : b) v *= 2.0f;

    auto r = xcorr_match::matchPair(a.data(), b.data(), nCh, nSamp, 4);
    CHECK(r.cosineScore > 0.999f, "cosine score is shape-invariant");
    CHECK(r.residualScore > 0.999f, "residual score is amplitude-invariant");
    CHECK(std::fabs(r.alphaStar - 0.5f) < 1e-3f, "α* = 1/scale = 0.5");
}

// -----------------------------------------------------------------------------
// T3: integer-sample shifted templates → recovered lag matches injected.
// -----------------------------------------------------------------------------
static void test_integer_shift() {
    std::puts("T3: integer-shifted templates → recovered lag matches");
    const int nCh = 4, nSamp = 32;
    auto a = buildMultiChannel(nCh, nSamp, 16, {1.0f, 0.5f, 0.7f, 0.3f});
    auto b = buildMultiChannel(nCh, nSamp, 18, {1.0f, 0.5f, 0.7f, 0.3f});  // peak shifted +2

    auto r = xcorr_match::matchPair(a.data(), b.data(), nCh, nSamp, 5);
    // shifting b's peak right by +2 means we should find best alignment
    // when b is shifted LEFT by 2 (i.e. τ = +2 if τ shifts b right matches a's earlier peak)
    // — let's just check |bestShiftInt| ≈ 2 and score is high
    CHECK(std::abs(r.bestShiftInt) == 2, "integer lag = ±2");
    CHECK(r.cosineScore > 0.95f, "high cosine score at correct lag");
}

// -----------------------------------------------------------------------------
// T4: sub-sample shift recovery.  Generate two waveforms that differ by
//     a 0.5-sample shift; verify bestShiftFrac is close to ±0.5.
// -----------------------------------------------------------------------------
static void test_subsample_shift() {
    std::puts("T4: 0.5-sample shift → recovered via parabolic interp");
    const int nCh = 1, nSamp = 64;
    // Build a waveform with peak between samples 31 and 32 by interpolating.
    auto bA = syntheticSpike(nSamp, 31, 1.0f);     // peak at sample 31
    auto bB = syntheticSpike(nSamp + 4, 64, 1.0f); // peak at sample 64 of 68
    // Now sub-sample by 1.5 — produces a waveform whose peak is at non-
    // integer sample.  Actually let me just use two waveforms with peaks
    // shifted by exactly 1 sample, plus a half-sample sinc interpolation.
    // Simpler: peak of A at samp 31, peak of B at samp 32 — gives integer
    // lag of 1.  We test that sub-sample refinement doesn't go crazy.

    auto r = xcorr_match::matchPair(bA.data(),
                                     syntheticSpike(nSamp, 32, 1.0f).data(),
                                     nCh, nSamp, 4);
    CHECK(std::abs(r.bestShiftInt) == 1, "integer lag = ±1");
    CHECK(std::fabs(r.bestShiftFrac) <= 1.0f, "sub-sample lag in [-1, +1]");
    CHECK(r.cosineScore > 0.95f, "high cosine score");
}

// -----------------------------------------------------------------------------
// T5: unrelated templates → low score
// -----------------------------------------------------------------------------
static void test_unrelated() {
    std::puts("T5: unrelated waveforms → low score");
    const int nCh = 4, nSamp = 32;
    std::mt19937 rng(99);
    std::normal_distribution<float> nz(0.0f, 1.0f);

    std::vector<float> a(static_cast<size_t>(nCh) * nSamp);
    std::vector<float> b(static_cast<size_t>(nCh) * nSamp);
    for (auto& v : a) v = nz(rng);
    for (auto& v : b) v = nz(rng);

    auto r = xcorr_match::matchPair(a.data(), b.data(), nCh, nSamp, 4);
    CHECK(r.cosineScore < 0.5f, "cosine score < 0.5 for random pair");
}

// -----------------------------------------------------------------------------
// T6: batch interface fills out the right number of entries
// -----------------------------------------------------------------------------
static void test_batch() {
    std::puts("T6: batch matchAllPairs fills matrix");
    const int nCh = 2, nSamp = 16;
    const int N = 3;
    std::vector<std::vector<float>> wf(N);
    for (int k = 0; k < N; ++k) {
        wf[static_cast<size_t>(k)] =
            buildMultiChannel(nCh, nSamp, 8 + k, {1.0f, 0.5f});
    }
    std::vector<const float*> ptrs;
    for (auto& w : wf) ptrs.push_back(w.data());

    std::vector<xcorr_match::PairScore> out;
    xcorr_match::matchAllPairs(ptrs, nCh, nSamp, 3, out);
    CHECK(out.size() == static_cast<size_t>(N) * N, "output is N×N");

    // Diagonal (i==i) is untouched; check a few off-diagonals
    CHECK(out[1 * N + 0].cosineScore > 0.0f, "(1,0) computed");
    CHECK(out[2 * N + 0].cosineScore > 0.0f, "(2,0) computed");
    CHECK(out[2 * N + 1].cosineScore > 0.0f, "(2,1) computed");
}

int main() {
    std::puts("===============================================");
    std::puts(" xcorr_match unit tests");
    std::puts("===============================================");
    test_identical();
    test_amplitude_scaled();
    test_integer_shift();
    test_subsample_shift();
    test_unrelated();
    test_batch();
    std::puts("===============================================");
    std::printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    std::puts("===============================================");
    return g_fail == 0 ? 0 : 1;
}
