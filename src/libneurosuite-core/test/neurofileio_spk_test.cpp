// neurofileio_spk_test.cpp — round-trips the .spk int16 waveform I/O and the
// stage-tagged path composition added to the shared library, so klusters'
// "Save As stage" / grown-waveform writing and any other consumer share one
// implementation.  Self-contained (own main, assert-based); built when
// NS_BUILD_TESTS=ON, run via ctest.
//
// Usage: neurofileio_spk_test   (no args; writes a scratch file in CWD)

#include "neurosuite/core/neurofileio.h"
#include "neurosuite/core/custody.hpp"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

static int g_fail = 0;
static int g_ran  = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

int main()
{
    using namespace neurofileio;

    // ── .spk round-trip ─────────────────────────────────────────────────────
    const int nSamples = 4, nChannels = 3;
    const int64_t nSpikes = 5;
    const int64_t recVals = static_cast<int64_t>(nSamples) * nChannels;  // 12
    std::vector<int16_t> buf(static_cast<size_t>(nSpikes * recVals));
    // Fill with a value that encodes (spike, sample, channel) so a transpose or
    // off-by-one in the flat index is caught, spanning negative int16 too.
    for (int64_t s = 0; s < nSpikes; ++s)
        for (int t = 0; t < nSamples; ++t)
            for (int c = 0; c < nChannels; ++c) {
                const int64_t idx = (s * nSamples + t) * nChannels + c;
                buf[static_cast<size_t>(idx)] =
                    static_cast<int16_t>((s * 100 + t * 10 + c) - 250);
            }

    const std::string path = "nfio_spk_roundtrip.tmp.spk";
    check(writeSpk(path, nSamples, nChannels, buf), "writeSpk ok");

    SpkFile r = readSpk(path, nSamples, nChannels);
    check(r.ok, "readSpk ok");
    check(r.nSamples == nSamples && r.nChannels == nChannels, "readSpk geometry preserved");
    check(r.nSpikes == nSpikes, "readSpk nSpikes derived from file size");
    check(r.samples == buf, "readSpk bytes identical to writeSpk input");

    // Wrong geometry → not a whole number of records → rejected (no silent misread).
    SpkFile bad = readSpk(path, nSamples, nChannels + 1);
    check(!bad.ok && bad.nSpikes == 0, "readSpk rejects a geometry mismatch");

    // Missing file → not ok.
    check(!readSpk("does_not_exist.spk", nSamples, nChannels).ok, "readSpk missing file not ok");

    // writeSpk refuses a buffer that is not a whole number of records.
    std::vector<int16_t> ragged(static_cast<size_t>(recVals + 1), 0);
    check(!writeSpk("nfio_spk_ragged.tmp.spk", nSamples, nChannels, ragged),
          "writeSpk refuses a partial-record buffer");

    // Empty (zero-spike) .spk is valid and round-trips to nSpikes == 0.
    check(writeSpk(path, nSamples, nChannels, {}), "writeSpk empty ok");
    SpkFile empty = readSpk(path, nSamples, nChannels);
    check(empty.ok && empty.nSpikes == 0 && empty.samples.empty(), "readSpk empty -> 0 spikes");

    // ── stage-tagged path composition ───────────────────────────────────────
    // <base>.<type>.<method>.<group>[.<stage>], and parseAnchor reads the stage
    // back as the suffix (no leading dot), so a staged file round-trips.
    const std::string sp =
        stagePath("/d/sirotaA-jg-000005-20120316", "spk", "stderiv_C5_D34", 6, "final");
    check(sp == "/d/sirotaA-jg-000005-20120316.spk.stderiv_C5_D34.6.final",
          "stagePath composes <base>.<type>.<method>.<group>.<stage>");
    const auto a = neurosuite::custody::parseAnchor(sp);
    check(a.ok && a.suffix == "final" && a.method == "stderiv_C5_D34" && a.group == 6,
          "parseAnchor round-trips stagePath (suffix == stage)");
    // Empty stage degrades to the plain methodPath.
    check(stagePath("/d/base", "clu", "standard", 8, "") == methodPath("/d/base", "clu", "standard", 8),
          "stagePath empty stage == methodPath");

    std::remove(path.c_str());

    std::printf("%s: %d checks, %d failed\n", (g_fail ? "FAIL" : "OK"), g_ran, g_fail);
    return g_fail ? 1 : 0;
}
