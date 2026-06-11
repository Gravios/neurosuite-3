/***************************************************************************
 * neurofileio.cpp — see neurofileio.h
 ***************************************************************************/
#include "neurofileio.h"
#include "custody.hpp"   // single source of truth for composition/parsing

#include <cstdio>
#include <fstream>
#include <sstream>

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

CluFile readCluBinary(const std::string& path, int64_t nSpikes)
{
    CluFile out;
    if (nSpikes < 0) return out;
    std::ifstream in(path, std::ios::binary);
    if (!in) return out;

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
