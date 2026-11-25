// realignio_test.cpp -- what a realign reads of one cluster (realignio.h).
//
// Checked, on files written here: the waveforms of chosen rows come back channel-major and in
// the order asked; the times and the non-projection feature columns of those rows; a .fet whose
// header disagrees with the session is read by its own width, with a warning; a row past the end
// of the .spk or the .res is an error naming the row; a feature past the end of the .fet is a
// warning and stays 0; a file that is not there is reported by number.
// No session, own main, run via ctest.

#include "realignio.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what)
{
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

template <class T>
static void writeAll(const std::string& path, const std::vector<T>& v, const int32_t* header = nullptr)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (header) std::fwrite(header, sizeof(int32_t), 1, f);
    std::fwrite(v.data(), sizeof(T), v.size(), f);
    std::fclose(f);
}

static bool has(const realign_io::Outcome& o, const std::string& text)
{
    for (const std::string& l : o.lines) if (l.find(text) != std::string::npos) return true;
    return false;
}

int main()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("realignio_test_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    const std::string spk = (dir / "s.spk.1").string(), res = (dir / "s.res.1").string(),
                      fet = (dir / "s.fet.1").string(), none = (dir / "missing").string();

    const int nChan = 3, nSamp = 4, nSpikes = 6, nDim = 5;      // 2 projections, 2 extra columns, the time
    // .spk, sample-major: the value encodes (spike, sample, channel).
    std::vector<int16_t> spkData;
    for (int p = 0; p < nSpikes; ++p)
        for (int s = 0; s < nSamp; ++s)
            for (int ch = 0; ch < nChan; ++ch)
                spkData.push_back(static_cast<int16_t>(p * 100 + s * 10 + ch));
    writeAll(spk, spkData);
    std::vector<int64_t> resData, fetData;
    for (int p = 0; p < nSpikes; ++p) {
        resData.push_back(1000 + 7 * p);
        for (int c = 0; c < nDim; ++c) fetData.push_back(c == nDim - 1 ? 1000 + 7 * p : p * 10 + c);
    }
    writeAll(res, resData);
    const int32_t header = nDim;
    writeAll(fet, fetData, &header);

    const std::vector<int64_t> rows = { 4, 1, 5 };
    {   // waveforms: channel-major, in the order of the rows
        std::vector<int16_t> w;
        const realign_io::Outcome o = realign_io::readWaveforms(spk, rows, nChan, nSamp, w);
        check(o.ok && o.lines.empty() && o.cannotOpen == 0, "waveforms: read without a line");
        bool right = w.size() == rows.size() * static_cast<size_t>(nChan * nSamp);
        for (size_t i = 0; right && i < rows.size(); ++i)
            for (int ch = 0; ch < nChan; ++ch)
                for (int s = 0; s < nSamp; ++s)
                    right = right && w[i * static_cast<size_t>(nChan * nSamp) + static_cast<size_t>(ch * nSamp + s)]
                                     == static_cast<int16_t>(rows[i] * 100 + s * 10 + ch);
        check(right, "waveforms: each value is at [spike][channel][sample]");
    }
    {   // toChannelMajor by itself
        const int16_t in[6] = { 1, 2, 3,  4, 5, 6 };            // 2 samples of 3 channels
        int16_t out[6] = { 0 };
        realign_io::toChannelMajor(in, 3, 2, out);
        const int16_t want[6] = { 1, 4,  2, 5,  3, 6 };
        bool same = true; for (int i = 0; i < 6; ++i) same = same && out[i] == want[i];
        check(same, "toChannelMajor: out[ch * nSamp + s] = in[s * nChan + ch]");
    }
    {   // a row past the end; no rows; a missing file
        std::vector<int16_t> w;
        realign_io::Outcome o = realign_io::readWaveforms(spk, { 1, 6 }, nChan, nSamp, w);
        check(!o.ok && has(o, "ERROR: .spk short read at spike 6"), "waveforms: a row past the end is an error naming it");
        o = realign_io::readWaveforms(spk, {}, nChan, nSamp, w);
        check(o.ok && w.empty(), "waveforms: no rows is no data and no error");
        o = realign_io::readWaveforms(none, rows, nChan, nSamp, w);
        check(!o.ok && o.cannotOpen == 1 && o.lines.empty(), "waveforms: a missing file is reported as path 1");
    }
    {   // times and the extra columns
        std::vector<int64_t> t; std::vector<std::vector<int64_t>> x;
        realign_io::Outcome o = realign_io::readTimesAndExtraFeatures(res, fet, rows, nDim, 2, 2, t, x);
        check(o.ok && o.lines.empty(), "times: read without a line");
        check(t == std::vector<int64_t>({ 1028, 1007, 1035 }), "times: those of the rows, in their order");
        check(x.size() == 3 && x[0] == std::vector<int64_t>({ 42, 43 }) && x[1] == std::vector<int64_t>({ 12, 13 })
              && x[2] == std::vector<int64_t>({ 52, 53 }), "extras: columns 2 and 3 of each row");
        o = realign_io::readTimesAndExtraFeatures(res, fet, rows, nDim, 4, 0, t, x);
        check(o.ok && x.empty() && t.size() == 3, "extras: none asked, none returned");
    }
    {   // the session expects another width: the file's own is used, with a warning
        std::vector<int64_t> t; std::vector<std::vector<int64_t>> x;
        const realign_io::Outcome o = realign_io::readTimesAndExtraFeatures(res, fet, rows, nDim + 2, 2, 2, t, x);
        check(o.ok && has(o, "WARNING: .fet nDimensions=5 but timeDim=7"), "a header that disagrees is a warning");
        check(x.size() == 3 && x[0] == std::vector<int64_t>({ 42, 43 }), "... and the file's width is the one used");
    }
    {   // rows past the end
        std::vector<int64_t> t; std::vector<std::vector<int64_t>> x;
        realign_io::Outcome o = realign_io::readTimesAndExtraFeatures(res, fet, { 2, 9 }, nDim, 2, 2, t, x);
        check(!o.ok && has(o, "ERROR: cannot read .res at spike 9"), "times: a row past the end of the .res is an error");
        // a .fet shorter than the .res: the time is read, the features are not
        writeAll(fet, std::vector<int64_t>(fetData.begin(), fetData.begin() + 3 * nDim), &header);
        o = realign_io::readTimesAndExtraFeatures(res, fet, { 1, 4 }, nDim, 2, 2, t, x);
        check(o.ok && t == std::vector<int64_t>({ 1007, 1028 }), "a short .fet: the times are still read");
        check(has(o, "WARNING: cannot read extra feat col=2 spike=4") && has(o, "col=3 spike=4") && o.lines.size() == 2,
              "a short .fet: one warning per feature that could not be read");
        check(x[0] == std::vector<int64_t>({ 12, 13 }) && x[1] == std::vector<int64_t>({ 0, 0 }),
              "a short .fet: the unread features are 0");
    }
    {   // missing files, an empty .fet
        std::vector<int64_t> t; std::vector<std::vector<int64_t>> x;
        realign_io::Outcome o = realign_io::readTimesAndExtraFeatures(none, fet, rows, nDim, 2, 2, t, x);
        check(!o.ok && o.cannotOpen == 1, "a missing .res is reported as path 1");
        o = realign_io::readTimesAndExtraFeatures(res, none, rows, nDim, 2, 2, t, x);
        check(!o.ok && o.cannotOpen == 2, "a missing .fet is reported as path 2");
        writeAll(fet, std::vector<int64_t>());
        o = realign_io::readTimesAndExtraFeatures(res, fet, rows, nDim, 2, 2, t, x);
        check(!o.ok && has(o, "ERROR: cannot read .fet header"), "a .fet without a header is an error");
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf("%d checks, %d failed\n", g_ran, g_fail);
    return g_fail ? 1 : 0;
}
