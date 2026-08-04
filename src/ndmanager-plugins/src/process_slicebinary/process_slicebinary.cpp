/***************************************************************************
 *   process_slicebinary                                                   *
 *                                                                         *
 *   Copy a subset of channels out of a multiplexed (channel-interleaved)   *
 *   binary recording into a new, smaller binary.  The source is never      *
 *   modified.                                                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify   *
 *   it under the terms of the GNU General Public License as published by   *
 *   the Free Software Foundation; either version 3 of the License, or      *
 *   (at your option) any later version.                                    *
 ***************************************************************************/

// Why this is not process_extractchannels
// ---------------------------------------
// process_extractchannels does a superficially similar copy, but it is not
// usable here for three reasons, each of which would fail silently:
//
//   1. It hardcodes `typedef short Data`.  On a 32-bit recording it would
//      read the file at the wrong stride and write plausible-looking
//      garbage.  Sample width is a parameter here, and a width the caller
//      did not ask for is an error rather than a reinterpretation.
//   2. Its channel tokens carry gain (`5*1.5`) and post-hoc referencing
//      (`5-2`) syntax.  A slice is meant to be a byte-exact excerpt; a
//      stray `-` in a channel list silently producing a *derived* signal
//      is exactly the kind of substitution that is invisible downstream.
//      Tokens here are plain integers and anything else is refused.
//   3. It is used destructively (ndm_extractchannels moves its temp over
//      the session .dat), so it has no notion of leaving the source alone.
//
// It also does not check that the file divides evenly into records, which
// is the one cheap integrity check available on a headerless format.

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

const char *kProgram = "process_slicebinary";

void usage()
{
    std::cout
        << "\nusage: " << kProgram
        << " [options] <input> <output> <nChannels> <channelList>\n\n"
        << "  Copies the listed channels out of a channel-interleaved binary\n"
        << "  into a new binary holding only those channels, in the order given.\n"
        << "  The input file is opened read-only and never modified.\n\n"
        << "  <nChannels>    channels per record in the INPUT file\n"
        << "  <channelList>  comma-separated, 0-based, e.g. 0,1,4,5\n"
        << "                 order is preserved, so it can also reorder;\n"
        << "                 a channel may be repeated (duplicates it in the output)\n\n"
        << "  -b, --bits N   sample width in bits: 16 (default) or 32\n"
        << "  -s, --buffer N records buffered per pass (default 65536)\n"
        << "  -q, --quiet    suppress the summary written to stderr\n"
        << "  -h, --help     this message\n\n";
}

/** Parses a comma-separated list of plain non-negative integers.
 *
 *  Deliberately strict: no whitespace, no gain or reference syntax, no
 *  ranges, no empty fields.  A slice must be a verbatim excerpt, so any
 *  token that could mean "compute something" is refused rather than
 *  partially honoured. */
bool parseChannelList(const std::string &spec, int nChannels,
                      std::vector<int> &out, std::string &error)
{
    out.clear();
    if (spec.empty()) { error = "empty channel list"; return false; }

    std::size_t pos = 0;
    while (true) {
        const std::size_t comma = spec.find(',', pos);
        const std::string tok =
            spec.substr(pos, comma == std::string::npos ? std::string::npos
                                                        : comma - pos);
        if (tok.empty()) { error = "empty field in channel list"; return false; }
        for (const char c : tok) {
            if (c < '0' || c > '9') {
                error = "bad channel token \"" + tok +
                        "\": only plain 0-based integers are accepted"
                        " (no ranges, gains or references)";
                return false;
            }
        }
        errno = 0;
        const long v = std::strtol(tok.c_str(), nullptr, 10);
        if (errno != 0 || v > 1000000L) {
            error = "channel number out of range: " + tok;
            return false;
        }
        if (v >= nChannels) {
            error = "cannot slice channel " + tok + ": the input has " +
                    std::to_string(nChannels) + " channels, numbered from 0";
            return false;
        }
        out.push_back(static_cast<int>(v));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return true;
}

/** Streams the slice.  Templated on the sample type so the record stride is
 *  a compile-time fact rather than a byte count computed in three places. */
template <typename Sample>
int slice(std::ifstream &in, std::ofstream &out, int nChannels,
          const std::vector<int> &channels, long recordsPerPass,
          std::uintmax_t nRecords)
{
    const std::size_t nOut = channels.size();
    std::vector<Sample> inBuf(static_cast<std::size_t>(nChannels) *
                              static_cast<std::size_t>(recordsPerPass));
    std::vector<Sample> outBuf(nOut * static_cast<std::size_t>(recordsPerPass));

    std::uintmax_t done = 0;
    while (done < nRecords) {
        const std::uintmax_t want =
            std::min<std::uintmax_t>(static_cast<std::uintmax_t>(recordsPerPass),
                                     nRecords - done);
        in.read(reinterpret_cast<char *>(inBuf.data()),
                static_cast<std::streamsize>(want * nChannels * sizeof(Sample)));
        if (in.gcount() !=
            static_cast<std::streamsize>(want * nChannels * sizeof(Sample))) {
            std::cerr << kProgram << ": error: short read at record " << done
                      << '\n';
            return EXIT_FAILURE;
        }
        for (std::uintmax_t r = 0; r < want; ++r) {
            const Sample *src = inBuf.data() + r * nChannels;
            Sample *dst = outBuf.data() + r * nOut;
            for (std::size_t c = 0; c < nOut; ++c) dst[c] = src[channels[c]];
        }
        out.write(reinterpret_cast<const char *>(outBuf.data()),
                  static_cast<std::streamsize>(want * nOut * sizeof(Sample)));
        if (!out.good()) {
            std::cerr << kProgram << ": error: write failed at record " << done
                      << " (disk full?)\n";
            return EXIT_FAILURE;
        }
        done += want;
    }
    return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char *argv[])
{
    int bits = 16;
    long recordsPerPass = 65536;
    bool quiet = false;

    int i = 1;
    for (; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") { usage(); return EXIT_SUCCESS; }
        else if ((a == "-b" || a == "--bits") && i + 1 < argc) bits = std::atoi(argv[++i]);
        else if ((a == "-s" || a == "--buffer") && i + 1 < argc) recordsPerPass = std::atol(argv[++i]);
        else if (a == "-q" || a == "--quiet") quiet = true;
        else if (!a.empty() && a[0] == '-') {
            std::cerr << kProgram << ": error: unknown option " << a
                      << " (try --help)\n";
            return EXIT_FAILURE;
        } else break;
    }

    if (argc - i != 4) {
        std::cerr << kProgram << ": error: expected 4 arguments, got "
                  << (argc - i) << " (try --help)\n";
        return EXIT_FAILURE;
    }
    const std::string inPath = argv[i];
    const std::string outPath = argv[i + 1];
    const int nChannels = std::atoi(argv[i + 2]);
    const std::string channelSpec = argv[i + 3];

    if (bits != 16 && bits != 32) {
        // Refusing beats guessing: reading a 32-bit file as 16-bit produces a
        // file of the right length full of wrong numbers, which nothing
        // downstream can detect.
        std::cerr << kProgram << ": error: unsupported sample width " << bits
                  << " bits (supported: 16, 32)\n";
        return EXIT_FAILURE;
    }
    if (nChannels < 1) {
        std::cerr << kProgram << ": error: nChannels must be >= 1\n";
        return EXIT_FAILURE;
    }
    if (recordsPerPass < 1) {
        std::cerr << kProgram << ": error: buffer must be >= 1 record\n";
        return EXIT_FAILURE;
    }
    if (inPath == outPath) {
        std::cerr << kProgram
                  << ": error: input and output are the same file; the source"
                     " is never modified in place\n";
        return EXIT_FAILURE;
    }

    std::vector<int> channels;
    std::string error;
    if (!parseChannelList(channelSpec, nChannels, channels, error)) {
        std::cerr << kProgram << ": error: " << error << '\n';
        return EXIT_FAILURE;
    }

    std::ifstream in(inPath, std::ios::binary);
    if (!in.good()) {
        std::cerr << kProgram << ": error: could not open " << inPath << '\n';
        return EXIT_FAILURE;
    }
    in.seekg(0, std::ios::end);
    const std::uintmax_t bytes = static_cast<std::uintmax_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    const std::size_t sampleBytes = (bits == 16) ? sizeof(std::int16_t)
                                                 : sizeof(std::int32_t);
    const std::uintmax_t recordBytes =
        static_cast<std::uintmax_t>(nChannels) * sampleBytes;
    if (bytes == 0) {
        std::cerr << kProgram << ": error: " << inPath << " is empty\n";
        return EXIT_FAILURE;
    }
    if (bytes % recordBytes != 0) {
        // The only integrity check a headerless interleaved format allows.
        // A remainder means nChannels or the sample width is wrong, and every
        // record after the first would be sheared across channel boundaries.
        std::cerr << kProgram << ": error: " << inPath << " is " << bytes
                  << " bytes, which is not a whole number of " << nChannels
                  << "-channel " << bits << "-bit records (" << recordBytes
                  << " bytes each); nChannels or the sample width is wrong\n";
        return EXIT_FAILURE;
    }
    const std::uintmax_t nRecords = bytes / recordBytes;

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        std::cerr << kProgram << ": error: could not create " << outPath << '\n';
        return EXIT_FAILURE;
    }

    const int rc = (bits == 16)
        ? slice<std::int16_t>(in, out, nChannels, channels, recordsPerPass, nRecords)
        : slice<std::int32_t>(in, out, nChannels, channels, recordsPerPass, nRecords);

    out.flush();
    if (rc == EXIT_SUCCESS && !out.good()) {
        std::cerr << kProgram << ": error: failed to flush " << outPath << '\n';
        return EXIT_FAILURE;
    }
    out.close();
    if (rc != EXIT_SUCCESS) {
        // A truncated slice that looks like a valid file is worse than none.
        std::remove(outPath.c_str());
        return rc;
    }

    if (!quiet) {
        std::cerr << kProgram << ": " << nRecords << " records, " << nChannels
                  << " -> " << channels.size() << " channels, " << bits
                  << "-bit -> " << outPath << '\n';
    }
    return EXIT_SUCCESS;
}
