/***************************************************************************
 * binfile.h
 *
 * Low-level binary I/O for .res, .fet, .clu files.
 *
 * Format:
 *   .res  — no header; N × int64_t timestamps (little-endian)
 *   .fet  — int32_t nDimensions; N × nDimensions × int64_t row-major
 *   .clu  — int32_t nClusters;   N × int32_t cluster ids
 *
 * All integers are native-endian (same machine reads and writes).
 * The spike count N is inferred from file size / record size.
 ***************************************************************************/

#pragma once

#include <QString>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace BinFile {

// ---------------------------------------------------------------------------
// .res  — timestamps
// ---------------------------------------------------------------------------

/// Read all timestamps. Returns false on I/O error.
inline bool readRes(const QString& path, std::vector<int64_t>& ts)
{
    FILE* f = fopen(path.toLocal8Bit().constData(), "rb");
    if (!f) return false;
    fseeko(f, 0, SEEK_END);
    int64_t sz = (int64_t)ftello(f);
    rewind(f);
    int64_t n = sz / (int64_t)sizeof(int64_t);
    ts.resize((size_t)n);
    bool ok = ((int64_t)fread(ts.data(), sizeof(int64_t), (size_t)n, f) == n);
    fclose(f);
    return ok;
}

/// Write all timestamps. Returns false on I/O error.
inline bool writeRes(const QString& path, const std::vector<int64_t>& ts)
{
    FILE* f = fopen(path.toLocal8Bit().constData(), "wb");
    if (!f) return false;
    bool ok = (fwrite(ts.data(), sizeof(int64_t), ts.size(), f) == ts.size());
    fclose(f);
    return ok;
}

// ---------------------------------------------------------------------------
// .fet  — features
// ---------------------------------------------------------------------------

struct FetFile {
    int32_t              nDimensions = 0;
    int64_t              nSpikes     = 0;
    std::vector<int64_t> data; // [spike * nDimensions + dim], last dim = timestamp
};

/// Read .fet. Returns false on I/O error or corrupt header.
inline bool readFet(const QString& path, FetFile& fet)
{
    FILE* f = fopen(path.toLocal8Bit().constData(), "rb");
    if (!f) return false;

    if (fread(&fet.nDimensions, sizeof(int32_t), 1, f) != 1) { fclose(f); return false; }
    if (fet.nDimensions <= 0) { fclose(f); return false; }

    fseeko(f, 0, SEEK_END);
    int64_t sz = (int64_t)ftello(f) - (int64_t)sizeof(int32_t);
    rewind(f);
    fseeko(f, sizeof(int32_t), SEEK_SET);

    fet.nSpikes = sz / ((int64_t)sizeof(int64_t) * fet.nDimensions);
    int64_t total = fet.nSpikes * fet.nDimensions;
    fet.data.resize((size_t)total);
    bool ok = ((int64_t)fread(fet.data.data(), sizeof(int64_t), (size_t)total, f) == total);
    fclose(f);
    return ok;
}

/// Write .fet. Returns false on I/O error.
inline bool writeFet(const QString& path, const FetFile& fet)
{
    FILE* f = fopen(path.toLocal8Bit().constData(), "wb");
    if (!f) return false;
    bool ok = (fwrite(&fet.nDimensions, sizeof(int32_t), 1, f) == 1);
    if (ok)
        ok = (fwrite(fet.data.data(), sizeof(int64_t), fet.data.size(), f)
              == fet.data.size());
    fclose(f);
    return ok;
}

// ---------------------------------------------------------------------------
// .clu  — cluster ids
// ---------------------------------------------------------------------------

struct CluFile {
    int32_t              nClusters = 0;
    std::vector<int32_t> ids;      // one per spike, in timestamp order
};

/// Read .clu. Returns false on I/O error or corrupt header.
inline bool readClu(const QString& path, CluFile& clu)
{
    FILE* f = fopen(path.toLocal8Bit().constData(), "rb");
    if (!f) return false;

    if (fread(&clu.nClusters, sizeof(int32_t), 1, f) != 1) { fclose(f); return false; }

    fseeko(f, 0, SEEK_END);
    int64_t sz = (int64_t)ftello(f) - (int64_t)sizeof(int32_t);
    rewind(f);
    fseeko(f, sizeof(int32_t), SEEK_SET);

    int64_t n = sz / (int64_t)sizeof(int32_t);
    clu.ids.resize((size_t)n);
    bool ok = ((int64_t)fread(clu.ids.data(), sizeof(int32_t), (size_t)n, f) == n);
    fclose(f);
    return ok;
}

/// Write .clu. Returns false on I/O error.
inline bool writeClu(const QString& path, const CluFile& clu)
{
    FILE* f = fopen(path.toLocal8Bit().constData(), "wb");
    if (!f) return false;
    bool ok = (fwrite(&clu.nClusters, sizeof(int32_t), 1, f) == 1);
    if (ok)
        ok = (fwrite(clu.ids.data(), sizeof(int32_t), clu.ids.size(), f)
              == clu.ids.size());
    fclose(f);
    return ok;
}

// ---------------------------------------------------------------------------
// Random-access helpers (for realignment — no full load needed)
// ---------------------------------------------------------------------------

/// Read a single timestamp from .res at 0-based spike index p.
inline bool readResAt(FILE* f, int64_t p, int64_t& ts)
{
    if (fseeko(f, (off_t)(p * (int64_t)sizeof(int64_t)), SEEK_SET) != 0) return false;
    return fread(&ts, sizeof(int64_t), 1, f) == 1;
}

/// Write a single timestamp to .res at 0-based spike index p.
inline bool writeResAt(FILE* f, int64_t p, int64_t ts)
{
    if (fseeko(f, (off_t)(p * (int64_t)sizeof(int64_t)), SEEK_SET) != 0) return false;
    return fwrite(&ts, sizeof(int64_t), 1, f) == 1;
}

/// Read one full .fet row (nDimensions values) at 0-based spike index p.
/// headerBytes = sizeof(int32_t).
inline bool readFetRow(FILE* f, int64_t p, int32_t nDimensions,
                       std::vector<int64_t>& row)
{
    row.resize((size_t)nDimensions);
    off_t off = (off_t)sizeof(int32_t)
              + (off_t)(p * nDimensions) * (off_t)sizeof(int64_t);
    if (fseeko(f, off, SEEK_SET) != 0) return false;
    return (int32_t)fread(row.data(), sizeof(int64_t), (size_t)nDimensions, f)
           == nDimensions;
}

/// Write one full .fet row at 0-based spike index p.
inline bool writeFetRow(FILE* f, int64_t p, int32_t nDimensions,
                        const std::vector<int64_t>& row)
{
    off_t off = (off_t)sizeof(int32_t)
              + (off_t)(p * nDimensions) * (off_t)sizeof(int64_t);
    if (fseeko(f, off, SEEK_SET) != 0) return false;
    return (int32_t)fwrite(row.data(), sizeof(int64_t), (size_t)nDimensions, f)
           == nDimensions;
}

/// Read one .clu entry at 0-based spike index p.
inline bool readCluAt(FILE* f, int64_t p, int32_t& id)
{
    off_t off = (off_t)sizeof(int32_t)
              + (off_t)(p * (int64_t)sizeof(int32_t));
    if (fseeko(f, off, SEEK_SET) != 0) return false;
    return fread(&id, sizeof(int32_t), 1, f) == 1;
}

/// Write one .clu entry at 0-based spike index p.
inline bool writeCluAt(FILE* f, int64_t p, int32_t id)
{
    off_t off = (off_t)sizeof(int32_t)
              + (off_t)(p * (int64_t)sizeof(int32_t));
    if (fseeko(f, off, SEEK_SET) != 0) return false;
    return fwrite(&id, sizeof(int32_t), 1, f) == 1;
}

} // namespace BinFile
