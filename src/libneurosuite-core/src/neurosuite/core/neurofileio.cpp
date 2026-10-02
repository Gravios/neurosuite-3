/***************************************************************************
 * neurofileio.cpp — see neurofileio.h
 ***************************************************************************/
#include "neurofileio.h"
#include "custody.hpp"   // single source of truth for composition/parsing

#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace neurofileio {

// ── .clu.N ────────────────────────────────────────────────────────────────
CluFile readClu(const std::string& path)
{
    CluFile out;
    std::ifstream in(path);
    if (!in) return out;

    std::string line;
    if (!std::getline(in, line)) return out;       // header (cluster count)
    {
        std::istringstream hs(line);
        if (!(hs >> out.nClusters)) return out;
    }
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        int id;
        if (ls >> id) out.ids.push_back(id);
    }
    out.ok = true;
    return out;
}

bool writeClu(const std::string& path, int nClusters,
              const std::vector<int>& ids)
{
    std::ofstream os(path);
    if (!os) return false;
    os << nClusters << '\n';
    for (int id : ids) os << id << '\n';
    return static_cast<bool>(os);
}

bool writeCluBinary(const std::string& path, int nClusters, const std::vector<int>& ids)
{
    std::ofstream os(path, std::ios::binary);
    if (!os) return false;
    const int32_t hdr = static_cast<int32_t>(nClusters);
    os.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    // ids are int (platform int == int32 on the targets); write as int32_t to
    // match readCluBinary, which reads int32_t ids.
    for (int id : ids) { const int32_t v = static_cast<int32_t>(id);
        os.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
    return static_cast<bool>(os);
}

CluFile readCluBinary(const std::string& path, int64_t nSpikes)
{
    CluFile out;
    if (nSpikes < 0) return out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;

    // Validate the length BEFORE reading.  A well-formed binary .clu is exactly
    //     int32 header + nSpikes * int32 ids
    // and must be 1:1 with the .res it is paired against.  Reading only the first nSpikes ids of a
    // LONGER file silently accepts a clustering belonging to a different .res -- another run, group
    // or variant -- and mislabels every spike while still reporting ok, leaving the caller no way to
    // notice.  (loadClusterRes()'s own `clu.ids.size() != nSpikes` guard further down could never
    // fire for exactly this reason.)  Record what the file actually holds so callers can name it.
    in.seekg(0, std::ios::end);
    const std::streamoff bytes = in.tellg();
    in.seekg(0, std::ios::beg);
    if (bytes < static_cast<std::streamoff>(sizeof(int32_t))) return out;   // no header
    const int64_t payload = static_cast<int64_t>(bytes) - static_cast<int64_t>(sizeof(int32_t));
    if (payload % static_cast<int64_t>(sizeof(int32_t)) != 0) return out;   // not a binary .clu
    out.nInFile = payload / static_cast<int64_t>(sizeof(int32_t));
    if (out.nInFile != nSpikes) return out;                                 // different clustering

    int32_t header = 0;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(header))) return out;
    out.nClusters = static_cast<int>(header);

    out.ids.reserve(static_cast<size_t>(nSpikes));
    for (int64_t k = 0; k < nSpikes; ++k) {
        int32_t id = 0;
        in.read(reinterpret_cast<char*>(&id), sizeof(id));
        if (in.gcount() != static_cast<std::streamsize>(sizeof(id))) return out;
        out.ids.push_back(static_cast<int>(id));
    }
    out.ok = true;
    return out;
}

// ── .res.N ────────────────────────────────────────────────────────────────
std::vector<int64_t> readRes(const std::string& path, bool* ok)
{
    std::vector<int64_t> out;
    std::ifstream in(path);
    if (!in) { if (ok) *ok = false; return out; }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        int64_t t;
        if (ls >> t) out.push_back(t);
    }
    if (ok) *ok = true;
    return out;
}

bool writeRes(const std::string& path, const std::vector<int64_t>& times)
{
    std::ofstream os(path);
    if (!os) return false;
    for (int64_t t : times) os << t << '\n';
    return static_cast<bool>(os);
}

bool writeResBinary(const std::string& path, const std::vector<int64_t>& times)
{
    std::ofstream os(path, std::ios::binary);
    if (!os) return false;
    if (!times.empty())
        os.write(reinterpret_cast<const char*>(times.data()),
                 static_cast<std::streamsize>(times.size()) * static_cast<std::streamsize>(sizeof(int64_t)));
    return static_cast<bool>(os);
}

std::vector<int64_t> readResBinary(const std::string& path, bool* ok)
{
    std::vector<int64_t> out;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) { if (ok) *ok = false; return out; }
    const std::streamoff bytes = in.tellg();
    if (bytes < 0 || (bytes % 8) != 0) { if (ok) *ok = false; return out; }
    const int64_t n = static_cast<int64_t>(bytes) / 8;
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(n));
    if (n > 0) {
        in.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(n) * 8);
        if (in.gcount() != static_cast<std::streamsize>(n) * 8) {
            out.clear();
            if (ok) *ok = false;
            return out;
        }
    }
    if (ok) *ok = true;
    return out;
}

bool isBinaryClusterRes(const std::string& resPath)
{
    std::ifstream in(resPath, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff sz = in.tellg();
    if (sz <= 0 || (sz % 8) != 0) return false;
    in.seekg(0, std::ios::beg);
    char first = 0;
    in.read(&first, 1);
    if (in.gcount() != 1) return false;
    return (first < '0' || first > '9');   // not an ASCII digit -> binary
}

ClusterResData readClusterRes(const std::string& cluPath,
                              const std::string& resPath)
{
    ClusterResData out;
    out.binary = isBinaryClusterRes(resPath);

    bool rok = false;
    out.times = out.binary ? readResBinary(resPath, &rok)
                           : readRes(resPath, &rok);
    if (!rok) return out;

    const int64_t nSpikes = static_cast<int64_t>(out.times.size());
    CluFile clu = out.binary ? readCluBinary(cluPath, nSpikes)
                             : readClu(cluPath);
    if (!clu.ok) return out;
    if (static_cast<int64_t>(clu.ids.size()) != nSpikes) return out;  // mismatch

    out.nClusters = clu.nClusters;
    out.ids       = std::move(clu.ids);
    out.ok        = true;
    return out;
}

// ── .fet.N ────────────────────────────────────────────────────────────────
FetFile readFet(const std::string& path)
{
    FetFile out;
    std::ifstream in(path);
    if (!in) return out;

    std::string line;
    if (!std::getline(in, line)) return out;       // header (feature count)
    {
        std::istringstream hs(line);
        if (!(hs >> out.nFeatures)) return out;
    }
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::vector<int> row;
        row.reserve(static_cast<size_t>(out.nFeatures));
        int v;
        while (ls >> v) row.push_back(v);
        if (!row.empty()) out.rows.push_back(std::move(row));
    }
    out.ok = true;
    return out;
}

FetBinaryFile readFetBinary(const std::string& path)
{
    FetBinaryFile out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;

    int32_t nFeat = 0;
    in.read(reinterpret_cast<char*>(&nFeat), sizeof(nFeat));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(nFeat)) || nFeat < 1)
        return out;
    out.nFeatures = static_cast<int>(nFeat);

    in.seekg(0, std::ios::end);
    const std::streamoff fileBytes = in.tellg();
    const int64_t dataBytes =
        static_cast<int64_t>(fileBytes) - static_cast<int64_t>(sizeof(nFeat));
    const int64_t rowBytes =
        static_cast<int64_t>(sizeof(int64_t)) * out.nFeatures;
    if (dataBytes <= 0 || (dataBytes % rowBytes) != 0) return out;
    out.nSpikes = dataBytes / rowBytes;

    in.seekg(static_cast<std::streamoff>(sizeof(nFeat)), std::ios::beg);
    const int64_t total = out.nSpikes * out.nFeatures;
    out.values.resize(static_cast<size_t>(total));
    if (total > 0) {
        in.read(reinterpret_cast<char*>(out.values.data()),
                static_cast<std::streamsize>(total) * 8);
        if (in.gcount() != static_cast<std::streamsize>(total) * 8) {
            out.values.clear();
            return out;
        }
    }
    out.ok = true;
    return out;
}

bool writeFetBinary(const std::string& path, int nFeatures,
                    const std::vector<int64_t>& values)
{
    if (nFeatures < 1) return false;
    if ((values.size() % static_cast<std::size_t>(nFeatures)) != 0) return false;  // not whole rows
    std::ofstream os(path, std::ios::binary);
    if (!os) return false;
    const int32_t hdr = static_cast<int32_t>(nFeatures);
    os.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (!values.empty())
        os.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size()) * static_cast<std::streamsize>(sizeof(int64_t)));
    return static_cast<bool>(os);
}

SpkFile readSpk(const std::string& path, int nSamples, int nChannels)
{
    SpkFile out;
    out.nSamples  = nSamples;
    out.nChannels = nChannels;
    if (nSamples <= 0 || nChannels <= 0) return out;

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return out;
    const std::streamoff bytes = in.tellg();
    const int64_t recVals  = static_cast<int64_t>(nSamples) * nChannels;
    const int64_t recBytes = recVals * static_cast<int64_t>(sizeof(int16_t));
    // A file that is not a whole number of (nSamples × nChannels) records has a
    // geometry mismatch (wrong group/variant) — reject rather than misread.
    if (bytes < 0 || recBytes <= 0 || (static_cast<int64_t>(bytes) % recBytes) != 0)
        return out;
    out.nSpikes = static_cast<int64_t>(bytes) / recBytes;

    in.seekg(0, std::ios::beg);
    const int64_t total = out.nSpikes * recVals;
    out.samples.resize(static_cast<size_t>(total));
    if (total > 0) {
        in.read(reinterpret_cast<char*>(out.samples.data()),
                static_cast<std::streamsize>(total) * static_cast<std::streamsize>(sizeof(int16_t)));
        if (in.gcount() != static_cast<std::streamsize>(total) * static_cast<std::streamsize>(sizeof(int16_t))) {
            out.samples.clear();
            out.nSpikes = 0;
            return out;
        }
    }
    out.ok = true;
    return out;
}

bool writeSpk(const std::string& path, int nSamples, int nChannels,
              const std::vector<int16_t>& samples)
{
    if (nSamples <= 0 || nChannels <= 0) return false;
    const int64_t recVals = static_cast<int64_t>(nSamples) * nChannels;
    // Refuse a buffer that is not a whole number of spike records.
    if (recVals <= 0 || (static_cast<int64_t>(samples.size()) % recVals) != 0)
        return false;
    std::ofstream os(path, std::ios::binary);
    if (!os) return false;
    if (!samples.empty())
        os.write(reinterpret_cast<const char*>(samples.data()),
                 static_cast<std::streamsize>(samples.size()) * static_cast<std::streamsize>(sizeof(int16_t)));
    return static_cast<bool>(os);
}

std::vector<ColDecomp> readColAccepted(const std::string& path)
{
    std::vector<ColDecomp> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;

    auto rdU32 = [&](uint32_t& v) { in.read(reinterpret_cast<char*>(&v), 4); };
    auto rdI32 = [&](int32_t&  v) { in.read(reinterpret_cast<char*>(&v), 4); };
    auto rdI64 = [&](int64_t&  v) { in.read(reinterpret_cast<char*>(&v), 8); };
    auto rdF32 = [&](float&    v) { in.read(reinterpret_cast<char*>(&v), 4); };

    unsigned char magic[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(magic), 4);
    if (!in || magic[0] != 'C' || magic[1] != 'O' || magic[2] != 'L' || magic[3] != 0x01)
        return out;
    uint32_t nSpikes = 0, nRecords = 0, nTemplates = 0, group = 0, flags = 0;
    rdU32(nSpikes); rdU32(nRecords); rdU32(nTemplates); rdU32(group); rdU32(flags);
    in.seekg(8, std::ios::cur);                              // header pad[8]
    in.seekg(32, std::ios::cur);                             // ColParams (32B)
    in.seekg(static_cast<std::streamoff>(nTemplates) * 24, std::ios::cur);  // ColTemplate[] (24B each)
    if (!in) return out;

    static constexpr uint32_t REC_FLAG_ACCEPTED = 1u;
    out.reserve(nRecords);
    for (uint32_t r = 0; r < nRecords; ++r) {
        int64_t ts = 0; int32_t spikeIdx = 0, bsu = 0; float bsc = 0; uint32_t rf = 0; float rn = 0;
        int32_t u1 = 0, sh1 = 0; float sf1 = 0, a1 = 0; int32_t u2 = 0, sh2 = 0; float sf2 = 0, a2 = 0;
        rdI64(ts); rdI32(spikeIdx); rdI32(bsu); rdF32(bsc); rdU32(rf); rdF32(rn);
        rdI32(u1); rdI32(sh1); rdF32(sf1); rdF32(a1);
        rdI32(u2); rdI32(sh2); rdF32(sf2); rdF32(a2);
        if (!in) break;
        if (rf & REC_FLAG_ACCEPTED) {
            ColDecomp d;
            d.spikeIndex = spikeIdx;
            d.u1 = u1; d.sh1 = sh1; d.a1 = a1;
            d.u2 = u2; d.sh2 = sh2; d.a2 = a2;
            out.push_back(d);
        }
    }
    return out;
}

// ── .wti — waveform-template index (see neurofileio.h) ──────────────────────
WtiIndex readWti(const std::string& path)
{
    WtiIndex idx;
    std::ifstream in(path);
    if (!in) return idx;

    std::string line;
    bool haveHeader = false;
    long declaredRows = -1;                       // from an "nRows" line, if present
    while (std::getline(in, line)) {
        // Trim a trailing CR (tolerate CRLF) and skip blank / comment lines.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos || line[s] == '#') continue;

        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (!haveHeader) {
            // The first non-blank, non-comment line MUST be "wti <version>".
            if (key != "wti") return WtiIndex{};
            int ver = 0;
            if (!(ls >> ver) || ver != 1) return WtiIndex{};   // only v1 implemented
            idx.version = ver;
            haveHeader = true;
            continue;
        }
        if (key == "nSamples")        ls >> idx.nSamples;
        else if (key == "nChannels")  ls >> idx.nChannels;
        else if (key == "peakSample") ls >> idx.peakSample;
        else if (key == "sr")         ls >> idx.sr;
        else if (key == "nRows")      ls >> declaredRows;
        else if (key == "row") {
            WtiRow r;
            // row <row> <unit> <link> <bin> <a> <b> <nSpikes>
            if (ls >> r.row >> r.unitId >> r.link >> r.bin >> r.a >> r.b >> r.nSpikes)
                idx.rows.push_back(r);
            // A malformed row line is skipped rather than aborting the whole index.
        }
        // Unknown keys are ignored, so the format can gain fields without
        // breaking older readers.
    }
    if (!haveHeader) return WtiIndex{};
    // If the writer declared a row count, it must match what we parsed.
    if (declaredRows >= 0 && declaredRows != static_cast<long>(idx.rows.size()))
        return WtiIndex{};
    idx.ok = true;
    return idx;
}

bool writeWti(const std::string& path, const WtiIndex& idx)
{
    std::ofstream out(path);
    if (!out) return false;
    out << "wti " << idx.version << "\n";
    out << "nSamples "   << idx.nSamples   << "\n";
    out << "nChannels "  << idx.nChannels  << "\n";
    out << "peakSample " << idx.peakSample << "\n";
    out << "sr "         << idx.sr         << "\n";
    out << "nRows "      << idx.rows.size() << "\n";
    out << "# row unit link bin a b nSpikes\n";
    for (const WtiRow& r : idx.rows) {
        out << "row " << r.row << ' ' << r.unitId << ' '
            << (r.link.empty() ? std::string("drift") : r.link) << ' '
            << r.bin << ' ' << r.a << ' ' << r.b << ' ' << r.nSpikes << "\n";
    }
    return static_cast<bool>(out);
}

std::vector<int> wtiUnits(const WtiIndex& idx)
{
    std::vector<int> units;
    for (const WtiRow& r : idx.rows) units.push_back(r.unitId);
    std::sort(units.begin(), units.end());
    units.erase(std::unique(units.begin(), units.end()), units.end());
    return units;
}

std::vector<std::string> wtiLinks(const WtiIndex& idx, int unitId)
{
    std::vector<std::string> links;                 // first-seen order, de-duplicated
    for (const WtiRow& r : idx.rows) {
        if (r.unitId != unitId) continue;
        if (std::find(links.begin(), links.end(), r.link) == links.end())
            links.push_back(r.link);
    }
    return links;
}

std::vector<WtiRow> wtiSeries(const WtiIndex& idx, int unitId, const std::string& link)
{
    std::vector<WtiRow> rows;
    for (const WtiRow& r : idx.rows)
        if (r.unitId == unitId && r.link == link) rows.push_back(r);
    std::stable_sort(rows.begin(), rows.end(),
                     [](const WtiRow& a, const WtiRow& b) { return a.bin < b.bin; });
    return rows;
}

// ── .eap — EAP membership + offset matrix (see neurofileio.h) ───────────────
EapFile readEap(const std::string& path)
{
    EapFile e;
    std::ifstream in(path, std::ios::binary);
    if (!in) return e;

    unsigned char magic[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char*>(magic), 4);
    if (!in || magic[0] != 'E' || magic[1] != 'A' || magic[2] != 'P' || magic[3] != 0x01)
        return e;
    uint32_t nSpikes = 0, nClasses = 0, group = 0, flags = 0;
    auto rdU32 = [&](uint32_t& v) { in.read(reinterpret_cast<char*>(&v), 4); };
    rdU32(nSpikes); rdU32(nClasses); rdU32(group); rdU32(flags);
    in.seekg(12, std::ios::cur);                            // pad -> 32B header (4+4*4+12)
    if (!in) return e;

    const std::size_t n = static_cast<std::size_t>(nSpikes) * nClasses;
    std::vector<int8_t> cells(n);
    if (n) in.read(reinterpret_cast<char*>(cells.data()), static_cast<std::streamsize>(n));
    if (!in && n) return e;                                 // short body

    e.nSpikes  = static_cast<int64_t>(nSpikes);
    e.nClasses = static_cast<int>(nClasses);
    e.group    = static_cast<int>(group);
    e.flags    = flags;
    e.cells    = std::move(cells);
    e.ok = true;
    return e;
}

bool writeEap(const std::string& path, int64_t nSpikes, int nClasses, int group,
              uint32_t flags, const std::vector<int8_t>& cells)
{
    if (nSpikes < 0 || nClasses < 0) return false;
    if (cells.size() != static_cast<std::size_t>(nSpikes) * static_cast<std::size_t>(nClasses))
        return false;                                       // not a whole N×T matrix
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const unsigned char magic[4] = {'E', 'A', 'P', 0x01};
    out.write(reinterpret_cast<const char*>(magic), 4);
    const uint32_t hs = static_cast<uint32_t>(nSpikes), hc = static_cast<uint32_t>(nClasses),
                   hg = static_cast<uint32_t>(group),   hf = flags;
    auto wrU32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    wrU32(hs); wrU32(hc); wrU32(hg); wrU32(hf);
    const char pad[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   // -> 32B header
    out.write(pad, 12);
    if (!cells.empty())
        out.write(reinterpret_cast<const char*>(cells.data()),
                  static_cast<std::streamsize>(cells.size()));
    return static_cast<bool>(out);
}

bool initEap(const std::string& path, int64_t nSpikes, int nClasses, int group)
{
    if (nSpikes < 0 || nClasses < 0) return false;
    std::vector<int8_t> cells(static_cast<std::size_t>(nSpikes) * nClasses, EAP_ABSENT);
    return writeEap(path, nSpikes, nClasses, group, 0u, cells);
}

EapFile growEap(const EapFile& in, int newT)
{
    if (!in.ok || newT <= in.nClasses) return in;           // no-op: already wide enough
    EapFile out = in;
    out.nClasses = newT;
    out.cells.assign(static_cast<std::size_t>(in.nSpikes) * newT, EAP_ABSENT);
    for (int64_t i = 0; i < in.nSpikes; ++i)
        for (int j = 0; j < in.nClasses; ++j)
            out.cells[static_cast<std::size_t>(i) * newT + j] =
                in.cells[static_cast<std::size_t>(i) * in.nClasses + j];
    return out;
}

std::vector<int64_t> eapClassSpikes(const EapFile& e, int classId)
{
    std::vector<int64_t> out;
    if (!e.ok || classId < 0 || classId >= e.nClasses) return out;
    for (int64_t i = 0; i < e.nSpikes; ++i)
        if (eapPresent(e.cells[static_cast<std::size_t>(i) * e.nClasses + classId]))
            out.push_back(i);
    return out;
}

std::vector<std::pair<int,int8_t>> eapSpikeClasses(const EapFile& e, int64_t spike)
{
    std::vector<std::pair<int,int8_t>> out;
    if (!e.ok || spike < 0 || spike >= e.nSpikes) return out;
    const std::size_t base = static_cast<std::size_t>(spike) * e.nClasses;
    for (int j = 0; j < e.nClasses; ++j) {
        const int8_t v = e.cells[base + j];
        if (eapPresent(v)) out.emplace_back(j, v);
    }
    return out;
}

// ── .tcl — template-class registry (see neurofileio.h) ──────────────────────
static std::string tclStatusToken(const TclEntry& t)
{
    switch (t.status) {
        case TclStatus::Active: return "active";
        case TclStatus::Tomb:   return "tomb";
        case TclStatus::Merged: return "merged:" + std::to_string(t.mergedInto);
        case TclStatus::Free:
        default:                return "free";
    }
}

TclRegistry readTcl(const std::string& path)
{
    TclRegistry reg;
    std::ifstream in(path);
    if (!in) return reg;

    std::string line;
    bool haveHeader = false;
    long declaredN = -1;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos || line[s] == '#') continue;
        if (!haveHeader) {
            std::istringstream hs(line);
            std::string key; int ver = 0;
            if (!(hs >> key >> ver) || key != "tcl" || ver != 1) return TclRegistry{};
            reg.version = ver; haveHeader = true; continue;
        }
        // nClasses header line (space-separated) vs a col row (TAB-separated).
        if (line.compare(0, 9, "nClasses ") == 0) {
            std::istringstream ns(line);
            std::string key; ns >> key >> declaredN;
            continue;
        }
        // col row: col <TAB> status <TAB> label <TAB> prov_clu <TAB> prov_stage <TAB> created
        std::vector<std::string> f;
        std::string cur;
        for (char c : line) { if (c == '\t') { f.push_back(cur); cur.clear(); } else cur += c; }
        f.push_back(cur);
        if (f.size() < 2) continue;                          // not a valid row
        TclEntry e;
        try { e.col = std::stoi(f[0]); } catch (...) { continue; }
        const std::string& st = f[1];
        if      (st == "active") e.status = TclStatus::Active;
        else if (st == "tomb")   e.status = TclStatus::Tomb;
        else if (st == "free")   e.status = TclStatus::Free;
        else if (st.compare(0, 7, "merged:") == 0) {
            e.status = TclStatus::Merged;
            try { e.mergedInto = std::stoi(st.substr(7)); } catch (...) { e.mergedInto = -1; }
        } else e.status = TclStatus::Free;
        auto dash = [](const std::string& v) { return v == "-" ? std::string() : v; };
        if (f.size() > 2) e.label = dash(f[2]);
        if (f.size() > 3) { const std::string p = dash(f[3]);
            try { e.provenanceClu = p.empty() ? -1 : std::stoi(p); } catch (...) { e.provenanceClu = -1; } }
        if (f.size() > 4) e.provenanceStage = dash(f[4]);
        if (f.size() > 5) e.created = dash(f[5]);
        reg.entries.push_back(e);
    }
    if (!haveHeader) return TclRegistry{};
    reg.nClasses = (declaredN >= 0) ? static_cast<int>(declaredN)
                                    : static_cast<int>(reg.entries.size());
    reg.ok = true;
    return reg;
}

bool writeTcl(const std::string& path, const TclRegistry& reg)
{
    std::ofstream out(path);
    if (!out) return false;
    out << "tcl " << reg.version << "\n";
    out << "nClasses " << reg.nClasses << "\n";
    out << "# col\tstatus\tlabel\tprovenance_clu\tprovenance_stage\tcreated\n";
    auto dash = [](const std::string& v) { return v.empty() ? std::string("-") : v; };
    for (const TclEntry& e : reg.entries) {
        out << e.col << '\t' << tclStatusToken(e) << '\t' << dash(e.label) << '\t'
            << (e.provenanceClu < 0 ? std::string("-") : std::to_string(e.provenanceClu)) << '\t'
            << dash(e.provenanceStage) << '\t' << dash(e.created) << "\n";
    }
    return static_cast<bool>(out);
}

TclRegistry initTcl(int nClasses)
{
    TclRegistry reg;
    reg.version = 1;
    reg.nClasses = (nClasses < 0) ? 0 : nClasses;
    reg.entries.resize(static_cast<std::size_t>(reg.nClasses));
    for (int i = 0; i < reg.nClasses; ++i) {
        reg.entries[static_cast<std::size_t>(i)].col = i;
        reg.entries[static_cast<std::size_t>(i)].status = TclStatus::Free;
    }
    reg.ok = true;
    return reg;
}

std::vector<EvtEntry> readEvt(const std::string& path, bool* ok)
{
    std::vector<EvtEntry> out;
    std::ifstream in(path);
    if (!in) { if (ok) *ok = false; return out; }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        EvtEntry e;
        if (!(ls >> e.timeMs)) continue;           // skip malformed lines
        // The label is the remainder of the line after the time token.
        std::string rest;
        std::getline(ls, rest);
        // trim a single leading space/tab left by the time token.
        size_t b = rest.find_first_not_of(" \t");
        e.label = (b == std::string::npos) ? std::string() : rest.substr(b);
        out.push_back(std::move(e));
    }
    if (ok) *ok = true;
    return out;
}

bool writeEvt(const std::string& path, const std::vector<EvtEntry>& events)
{
    std::ofstream os(path);
    if (!os) return false;
    for (const auto& e : events)
        os << e.timeMs << '\t' << e.label << '\n';
    return static_cast<bool>(os);
}

// ── .dat / .lfp ─────────────────────────────────────────────────────────────
int64_t datSampleCount(const std::string& path, int nbChannels)
{
    if (nbChannels <= 0) return -1;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return -1;
    const std::streamoff bytes = in.tellg();
    if (bytes < 0) return -1;
    return static_cast<int64_t>(bytes)
         / (static_cast<int64_t>(nbChannels) * 2);
}

int64_t readDatWindow(const std::string& path, int nbChannels,
                      int64_t startSample, int64_t nSamples, int16_t* out)
{
    if (nbChannels <= 0 || nSamples < 0 || startSample < 0 || !out) return -1;
    std::ifstream in(path, std::ios::binary);
    if (!in) return -1;

    const std::streamoff byteOff =
        static_cast<std::streamoff>(startSample)
        * static_cast<std::streamoff>(nbChannels) * 2;
    in.seekg(byteOff, std::ios::beg);
    if (!in) return 0;                              // seek past EOF -> nothing

    const std::streamsize want =
        static_cast<std::streamsize>(nSamples)
        * static_cast<std::streamsize>(nbChannels)
        * static_cast<std::streamsize>(sizeof(int16_t));
    in.read(reinterpret_cast<char*>(out), want);
    const std::streamsize got = in.gcount();
    return static_cast<int64_t>(got) / (static_cast<int64_t>(nbChannels) * 2);
}

// ── Variant-aware input resolution ──────────────────────────────────────────

static bool fileExists(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

ResolvedInput resolveInput(const std::string& base, const std::string& type,
                           int group,
                           const std::vector<std::string>& preferVariants)
{
    const std::string g = std::to_string(group);
    const std::string canonical = base + "." + type + "." + g;

    ResolvedInput r;
    for (const std::string& v : preferVariants) {
        if (v.empty()) {                          // canonical (no variant)
            if (fileExists(canonical)) {
                r.path = canonical; r.variant.clear();
                r.dotted = false;   r.found = true;
                return r;
            }
            continue;
        }
        // Preferred new form: <base>.<type>.<variant>.<group>
        const std::string dotted = base + "." + type + "." + v + "." + g;
        if (fileExists(dotted)) {
            r.path = dotted; r.variant = v; r.dotted = true; r.found = true;
            return r;
        }
        // Legacy glued form: <base>.<type><variant>.<group>  (e.g. .fetD.N)
        const std::string glued = base + "." + type + v + "." + g;
        if (fileExists(glued)) {
            r.path = glued; r.variant = v; r.dotted = false; r.found = true;
            return r;
        }
    }
    r.path = canonical; r.found = false;
    return r;
}

std::vector<std::string> preferDerived()
{
    return {"stderiv", "D", ""};
}

std::vector<std::string> preferCanonical()
{
    return {"", "stderiv", "D"};
}

// ── Mandatory-method resolution (chain-of-custody naming) ───────────────────

std::string methodPath(const std::string& base, const std::string& type,
                       const std::string& method, int group)
{
    return neurosuite::custody::methodPath(base, type, method, group);
}

std::string stagePath(const std::string& base, const std::string& type,
                      const std::string& method, int group, const std::string& stage)
{
    return neurosuite::custody::stagePath(base, type, method, group, stage);
}

ResolvedInput resolveInputForMethod(const std::string& base, const std::string& type,
                                    int group, const std::string& method)
{
    ResolvedInput r;
    r.path    = methodPath(base, type, method, group);
    r.variant = method;
    r.dotted  = true;
    r.found   = fileExists(r.path);
    return r;
}

std::string methodFromPath(const std::string& path)
{
    // Delegate to the single source of truth.  custody::methodOf is robust
    // against a dotted base (the slot before the group is the method only if it
    // is NOT a known type token), fixing the prior a.b.spk.5 -> "spk" misparse.
    return neurosuite::custody::methodOf(path);
}

}  // namespace neurofileio
