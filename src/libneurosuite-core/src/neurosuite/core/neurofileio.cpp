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
