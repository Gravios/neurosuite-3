/* unitsidecar.h -- the per-clustering cluster-information file (header-only,
 * no Qt).
 *
 * Klusters keeps what the curator records about each unit -- structure, type,
 * isolation distance, quality, notes -- in a file beside the clustering it
 * describes, named like it with "cli" (cluster information) for the type:
 *
 *     rat01.clu.stderiv_C5.3.microfiber  ->  rat01.cli.stderiv_C5.3.microfiber
 *
 * It is saved with the clustering (in the same all-or-nothing commit), so it
 * always matches the cluster ids of that .clu, and each stage of a group keeps
 * its own.  The session YAML's units table is NOT written by Klusters: it is
 * the record of finished curation, filled on request by ndManager
 * (Actions > Record Curated Units), which reads these files.
 *
 * Format: UTF-8 text, lines starting with '#' are comments, one unit per line
 * with eleven tab-separated fields
 *
 *     group  cluster  structure  type  isolationDistance  quality  notes
 *     region  division  atlas  regionId
 *
 * in the order of the YAML units table (the last four were added later; a
 * line with fewer fields reads them as empty, so v1 files read unchanged).  A tab, newline, carriage return or
 * backslash inside a field is written as \t, \n, \r or \\.  Units with every
 * descriptive field empty are not written.
 */
#ifndef KLUSTERSSHARED_UNITSIDECAR_H
#define KLUSTERSSHARED_UNITSIDECAR_H

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace unitsidecar {

struct Row {
    int group = 0, cluster = 0;
    std::string structure, type, isolationDistance, quality, notes;
    std::string region, division, atlas, regionId;
    bool describesNothing() const {
        return structure.empty() && type.empty() && isolationDistance.empty()
            && quality.empty() && notes.empty() && region.empty() && division.empty()
            && atlas.empty() && regionId.empty();
    }
    bool operator==(const Row& o) const {
        return group == o.group && cluster == o.cluster && structure == o.structure
            && type == o.type && isolationDistance == o.isolationDistance
            && quality == o.quality && notes == o.notes && region == o.region
            && division == o.division && atlas == o.atlas && regionId == o.regionId;
    }
};

inline const char* header()
{
    return "# klusters units v2\n"
           "# group\tcluster\tstructure\ttype\tisolationDistance\tquality\tnotes"
           "\tregion\tdivision\tatlas\tregionId\n";
}

inline std::string escape(const std::string& s)
{
    std::string o;
    for (char c : s) {
        switch (c) {
        case '\\': o += "\\\\"; break;
        case '\t': o += "\\t"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        default: o += c;
        }
    }
    return o;
}

inline std::string unescape(const std::string& s)
{
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) { o += s[i]; continue; }
        const char n = s[++i];
        o += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
    }
    return o;
}

/** The file text for @p rows (rows that describe nothing are left out). */
inline std::string format(const std::vector<Row>& rows)
{
    std::string out = header();
    for (const Row& r : rows) {
        if (r.describesNothing()) continue;
        out += std::to_string(r.group) + '\t' + std::to_string(r.cluster) + '\t'
             + escape(r.structure) + '\t' + escape(r.type) + '\t'
             + escape(r.isolationDistance) + '\t' + escape(r.quality) + '\t'
             + escape(r.notes) + '\t' + escape(r.region) + '\t' + escape(r.division) + '\t'
             + escape(r.atlas) + '\t' + escape(r.regionId) + '\n';
    }
    return out;
}

/** Parses @p text.  Returns false, with @p error naming the line, on a line
 *  that does not start with two integers; missing trailing fields are empty. */
inline bool parse(const std::string& text, std::vector<Row>& rows, std::string* error)
{
    rows.clear();
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        std::size_t start = 0;
        for (;;) {
            const std::size_t tab = line.find('\t', start);
            f.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) break;
            start = tab + 1;
        }
        auto toInt = [](const std::string& s, int& v) {
            if (s.empty()) return false;
            char* end = nullptr;
            const long x = std::strtol(s.c_str(), &end, 10);
            if (!end || *end) return false;
            v = static_cast<int>(x);
            return true;
        };
        Row r;
        if (f.size() < 2 || !toInt(f[0], r.group) || !toInt(f[1], r.cluster)) {
            if (error) *error = "line " + std::to_string(lineNo) + ": expected <group> <cluster> ...";
            return false;
        }
        f.resize(11);
        r.structure = unescape(f[2]);
        r.type = unescape(f[3]);
        r.isolationDistance = unescape(f[4]);
        r.quality = unescape(f[5]);
        r.notes = unescape(f[6]);
        r.region = unescape(f[7]);
        r.division = unescape(f[8]);
        r.atlas = unescape(f[9]);
        r.regionId = unescape(f[10]);
        rows.push_back(r);
    }
    return true;
}

/** The .cli file of a cluster file: the last ".clu." of the file name becomes
 *  ".cli.".  Empty when the name has no ".clu.". */
inline std::string pathFor(const std::string& cluPath)
{
    const std::size_t slash = cluPath.find_last_of('/');
    const std::size_t nameStart = slash == std::string::npos ? 0 : slash + 1;
    const std::size_t at = cluPath.rfind(".clu.");
    if (at == std::string::npos || at < nameStart) return std::string();
    std::string out = cluPath;
    out.replace(at, 5, ".cli.");
    return out;
}

} // namespace unitsidecar

#endif // KLUSTERSSHARED_UNITSIDECAR_H
