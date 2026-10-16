// Conformance test for the stderiv transform (stderiv_transform.hpp) against
// stderiv_vectors.tsv, the table fiber-kit's fiber_stderiv runs too.
//
// Usage: core_test_stderiv_vectors <path-to-stderiv_vectors.tsv>
#include <neurosuite/core/channel_expr.hpp>
#include <neurosuite/core/stderiv_transform.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace neurosuite::core;

static int fails = 0;
static void check(bool c, const std::string& m)
{
    if (!c) { std::printf("FAIL  %s\n", m.c_str()); ++fails; }
}

static std::vector<std::string> splitTabs(const std::string& line)
{
    std::vector<std::string> f;
    size_t p = 0;
    while (true) {
        const size_t t = line.find('\t', p);
        f.push_back(line.substr(p, t == std::string::npos ? std::string::npos : t - p));
        if (t == std::string::npos) break;
        p = t + 1;
    }
    return f;
}

static std::vector<int> ints(const std::string& s)
{
    std::vector<int> v;
    std::istringstream in(s);
    int x;
    while (in >> x) v.push_back(x);
    return v;
}

int main(int argc, char** argv)
{
    const std::string path = argc > 1 ? argv[1] : "stderiv_vectors.tsv";
    std::ifstream in(path);
    if (!in) { std::printf("FAIL  cannot open %s\n", path.c_str()); return 1; }
    int rows = 0;
    bool sawOrder[7] = {false, false, false, false, false, false, false};
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> f = splitTabs(line);
        if (f.size() != 8) { check(false, "8 columns: " + line.substr(0, 60)); continue; }
        ++rows;
        const std::string& name = f[0];
        const int order = std::stoi(f[1]);
        const std::string& map = f[2];
        const int nChan = std::stoi(f[3]), nSamp = std::stoi(f[4]);
        const std::vector<int> prev = ints(f[5]), input = ints(f[6]), want = ints(f[7]);
        if (order < 0 || order > 6) { check(false, name + ": order " + f[1]); continue; }
        sawOrder[order] = true;
        if ((int)prev.size() != nChan || (int)input.size() != nChan * nSamp
            || (int)want.size() != nChan * nSamp) {
            check(false, name + ": column sizes do not match nChan/nSamp");
            continue;
        }

        std::vector<int> partner, setOff{0}, setMem;
        ChannelExprSet expr;
        const int* pp = nullptr;
        const int* so = nullptr;
        const int* sm = nullptr;
        const ChannelExprSet* ep = nullptr;
        if (map != "-") {
            if (order == 4) { partner = ints(map); pp = partner.data(); }
            else if (order == 5) {
                std::istringstream s(map);
                std::string set;
                while (s >> set) {
                    std::istringstream m(set);
                    std::string tok;
                    while (std::getline(m, tok, '+')) setMem.push_back(std::stoi(tok));
                    setOff.push_back((int)setMem.size());
                }
                so = setOff.data(); sm = setMem.data();
            } else if (order == 6) {
                std::string err;
                check(parseChannelExprs(map, expr, &err), name + ": expressions parse (" + err + ")");
                ep = &expr;
            }
        }

        const SdiffOrder o = static_cast<SdiffOrder>(order);
        std::vector<int16_t> prev16(prev.begin(), prev.end()), seed(static_cast<size_t>(nChan));
        std::vector<int16_t> wav(input.begin(), input.end()), out(wav.size());
        stderivSeed(o, prev16.data(), nChan, seed.data(), pp, so, sm, ep);
        applyStderivTransform(o, wav.data(), nChan, nSamp, out.data(), pp, seed.data(), so, sm, ep);
        int bad = 0, firstBad = -1;
        for (size_t i = 0; i < out.size(); ++i)
            if (out[i] != want[i]) { if (firstBad < 0) firstBad = (int)i; ++bad; }
        if (bad)
            check(false, name + ": " + std::to_string(bad) + " samples differ, first at channel "
                         + std::to_string(firstBad / nSamp) + " sample " + std::to_string(firstBad % nSamp)
                         + " (got " + std::to_string(out[firstBad]) + ", want "
                         + std::to_string(want[firstBad]) + ")");

        // In place gives the same bytes (the extractor transforms its buffers in place).
        std::vector<int16_t> inPlace(input.begin(), input.end());
        applyStderivTransform(o, inPlace.data(), nChan, nSamp, inPlace.data(), pp, seed.data(), so, sm, ep);
        check(inPlace == out, name + ": in-place transform matches");
    }
    check(rows >= 30, "the vector file has its rows");
    for (int o = 0; o <= 6; ++o) check(sawOrder[o], "a row for order " + std::to_string(o));

    // A null seed is a zero seed (a window at the very start of the file).
    {
        const int16_t w[8] = {10, -3, 7, 2, 5, 5, -1, 9};
        int16_t a[8], b[8];
        const int16_t zero[2] = {0, 0};
        applyStderivTransform(SdiffOrder::AllPairs, w, 2, 4, a);
        applyStderivTransform(SdiffOrder::AllPairs, w, 2, 4, b, nullptr, zero);
        bool same = true;
        for (int i = 0; i < 8; ++i) same = same && a[i] == b[i];
        check(same, "no seed equals a zero seed");
    }

    if (fails) std::printf("\n%d FAILED\n", fails);
    else std::printf("stderiv_vectors: all passed (%d rows)\n", rows);
    return fails ? 1 : 0;
}
