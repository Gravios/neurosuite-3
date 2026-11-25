// realignio.cpp -- see realignio.h.

#include "realignio.h"

#include <neurosuite/core/fileio64.hpp>

#include <cstddef>
#include <cstdio>

namespace realign_io {

void toChannelMajor(const int16_t* sampleMajor, int nChan, int nSamp, int16_t* channelMajor)
{
    for (int s = 0; s < nSamp; ++s)
        for (int ch = 0; ch < nChan; ++ch)
            channelMajor[ch * nSamp + s] = sampleMajor[s * nChan + ch];
}

Outcome readWaveforms(const std::string& spkPath, const std::vector<int64_t>& rows,
                      int nChan, int nSamp, std::vector<int16_t>& channelMajor)
{
    Outcome out;
    const size_t  spkElems      = static_cast<size_t>(nChan) * static_cast<size_t>(nSamp);
    const int64_t bytesPerSpike = static_cast<int64_t>(spkElems) * 2;
    channelMajor.assign(rows.size() * spkElems, 0);

    std::FILE* sf = std::fopen(spkPath.c_str(), "rb");
    if (!sf) { out.ok = false; out.cannotOpen = 1; return out; }
    std::vector<int16_t> row(spkElems);
    for (size_t i = 0; i < rows.size(); ++i) {
        if (neurosuite::core::fseek64(sf, static_cast<std::int64_t>(rows[i] * bytesPerSpike), SEEK_SET) != 0) {
            out.lines.push_back("ERROR: .spk seek failed at spike " + std::to_string(rows[i]));
            out.ok = false;
            break;
        }
        if (std::fread(row.data(), 2, spkElems, sf) != spkElems) {
            out.lines.push_back("ERROR: .spk short read at spike " + std::to_string(rows[i]));
            out.ok = false;
            break;
        }
        toChannelMajor(row.data(), nChan, nSamp, channelMajor.data() + i * spkElems);
    }
    std::fclose(sf);
    return out;
}

Outcome readTimesAndExtraFeatures(const std::string& resPath, const std::string& fetPath,
                                  const std::vector<int64_t>& rows, int timeDim,
                                  int firstExtraColumn, int nExtra,
                                  std::vector<int64_t>& times,
                                  std::vector<std::vector<int64_t>>& extras)
{
    Outcome out;
    times.assign(rows.size(), 0);
    extras.clear();
    if (nExtra > 0)
        extras.assign(rows.size(), std::vector<int64_t>(static_cast<size_t>(nExtra), 0));

    std::FILE* rf = std::fopen(resPath.c_str(), "rb");
    if (!rf) { out.ok = false; out.cannotOpen = 1; return out; }
    std::FILE* ff = std::fopen(fetPath.c_str(), "rb");
    if (!ff) { std::fclose(rf); out.ok = false; out.cannotOpen = 2; return out; }

    int32_t fetNDim = 0;
    if (std::fread(&fetNDim, sizeof(int32_t), 1, ff) != 1) {
        std::fclose(rf); std::fclose(ff);
        out.lines.push_back("ERROR: cannot read .fet header");
        out.ok = false;
        return out;
    }
    if (fetNDim != static_cast<int32_t>(timeDim))
        out.lines.push_back("WARNING: .fet nDimensions=" + std::to_string(fetNDim) + " but timeDim="
                            + std::to_string(timeDim) + " \xE2\x80\x94 proceeding with file value");
    const int32_t fileDim = (fetNDim > 0) ? fetNDim : static_cast<int32_t>(timeDim);

    for (size_t i = 0; i < rows.size(); ++i) {
        const int64_t p = rows[i];
        if (neurosuite::core::fseek64(rf, static_cast<std::int64_t>(p * static_cast<int64_t>(sizeof(int64_t))), SEEK_SET) != 0
            || std::fread(&times[i], sizeof(int64_t), 1, rf) != 1) {
            out.lines.push_back("ERROR: cannot read .res at spike " + std::to_string(p));
            out.ok = false;
            break;
        }
        for (int k = 0; k < nExtra; ++k) {
            const int col = firstExtraColumn + k;
            const std::int64_t off = static_cast<std::int64_t>(sizeof(int32_t))
                                   + static_cast<std::int64_t>(p * static_cast<int64_t>(fileDim) + col)
                                   * static_cast<std::int64_t>(sizeof(int64_t));
            if (neurosuite::core::fseek64(ff, off, SEEK_SET) != 0
                || std::fread(&extras[i][static_cast<size_t>(k)], sizeof(int64_t), 1, ff) != 1)
                out.lines.push_back("WARNING: cannot read extra feat col=" + std::to_string(col)
                                    + " spike=" + std::to_string(p));
        }
    }
    std::fclose(rf);
    std::fclose(ff);
    return out;
}

} // namespace realign_io
