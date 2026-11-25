/***************************************************************************
 * realignio.h -- what a realign reads of one cluster, from plain files.
 *
 * The rows of a cluster in the three per-spike files of a feature space:
 *
 *   .spk   int16, no header; row p is nChan * nSamp values at byte
 *          p * nChan * nSamp * 2, SAMPLE-major ([s * nChan + ch]);
 *   .res   int64, no header; row p is one sample index at byte p * 8;
 *   .fet   int32 nDimensions, then rows of nDimensions int64: the features,
 *          the last one the time.
 *
 * No document, no Qt: KlustersDoc::realignSpikes calls these with the paths
 * and the rows it resolved, and realignio_test.cpp calls them on files it
 * writes.  Paths are in the encoding fopen takes.  The lines returned are for
 * the realign log (UTF-8, no line end) and never contain a path: a file that
 * cannot be opened is reported by number so that the caller names it.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef REALIGNIO_H
#define REALIGNIO_H

#include <cstdint>
#include <string>
#include <vector>

namespace realign_io {

struct Outcome {
    bool ok = true;                   ///< false: nothing read may be used
    int  cannotOpen = 0;              ///< 1 or 2: the first or second path given could not be opened
    std::vector<std::string> lines;   ///< "ERROR: ..." / "WARNING: ...", in the order they arose
};

/// One spike from the file's order to the realign's: out[ch * nSamp + s] = in[s * nChan + ch].
void toChannelMajor(const int16_t* sampleMajor, int nChan, int nSamp, int16_t* channelMajor);

/** The waveforms of the rows @p rows (0-based file rows, any order) of @p spkPath, CHANNEL-major:
 *  spike i at out[i * nChan * nSamp], in the order of @p rows. */
Outcome readWaveforms(const std::string& spkPath, const std::vector<int64_t>& rows,
                      int nChan, int nSamp, std::vector<int16_t>& channelMajor);

/** The times of the rows @p rows from @p resPath, and from @p fetPath the @p nExtra feature
 *  columns starting at @p firstExtraColumn (the columns that are not projections: they are
 *  carried over unchanged when the row is rewritten).  @p timeDim is the number of columns the
 *  session expects; the file's own header is used when it differs (a warning line).  A feature
 *  that cannot be read is left 0 with a warning; a time that cannot be read is an error. */
Outcome readTimesAndExtraFeatures(const std::string& resPath, const std::string& fetPath,
                                  const std::vector<int64_t>& rows, int timeDim,
                                  int firstExtraColumn, int nExtra,
                                  std::vector<int64_t>& times,
                                  std::vector<std::vector<int64_t>>& extras);

} // namespace realign_io

#endif // REALIGNIO_H
