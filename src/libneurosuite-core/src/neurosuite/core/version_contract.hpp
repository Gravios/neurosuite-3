// ════════════════════════════════════════════════════════════════════════════
//  version_contract.hpp — what this build promises at its boundaries: the
//  ndm plugin protocol, the session-schema version, and the versions of the
//  version-tagged file formats.
//
//  contract.yaml at the repository root states the same values for people and
//  for fiber-kit; core_test_contract fails when the two disagree, and
//  test_shared_contract.py checks contract.yaml against fiber-kit's.
//
//  ndmanager uses kNdmPluginProtocol for its plugin handshake, and
//  parseNdmVersion / formatMismatches to warn when a plugin's formats are
//  behind or ahead of this build.  A plugin answers `--ndm-version` with
//
//      ndm-plugin-protocol <N>
//      <name> <version>                       (any other line is ignored)
//      format <format> <read> <write>         (one line per format)
//
//  where <read> is the highest version it reads (it reads every version from
//  1 up) and <write> the version it writes.
// ════════════════════════════════════════════════════════════════════════════
#ifndef NEUROSUITE_CORE_VERSION_CONTRACT_HPP
#define NEUROSUITE_CORE_VERSION_CONTRACT_HPP

#include <sstream>
#include <string>
#include <vector>

#include "neurofileio.h"        // kWtiVersion, kWtlVersion, kTclVersion, kEapVersion
#include "pca_projection.hpp"   // kPcaeVersion

namespace neurosuite {
namespace contract {

// Bumped whenever a value below changes, so two contract.yaml files can be
// compared by one number first.
inline constexpr int kContractVersion = 1;

// The ndm-plugin-protocol this build speaks; ndmanager skips a plugin that
// declares a newer one.
inline constexpr int kNdmPluginProtocol = 1;

// parameters.version written into a session YAML.
inline constexpr const char* kSessionSchema = "1.0";

struct FormatVersion {
    std::string name;
    int read  = 0;   // highest version read (every version from 1 up)
    int write = 0;   // version written
};

// The version-tagged formats, as this build reads and writes them.  .wti is
// written as v1 when no row has a lineage parent and as v2 otherwise; it is
// listed as writing v2, the version a reader must accept.
inline std::vector<FormatVersion> formats() {
    using namespace neurofileio;
    return {
        {"pca", core::kPcaeVersion, core::kPcaeVersion},
        {"wti", kWtiVersion, kWtiVersion},
        {"wtl", kWtlVersion, kWtlVersion},
        {"tcl", kTclVersion, kTclVersion},
        {"eap", kEapVersion, kEapVersion},
    };
}

struct PluginVersion {
    bool ok = false;                     // the first line is a protocol line
    int  protocol = 0;
    std::vector<FormatVersion> formats;  // its `format` lines, in order
};

// Parse a plugin's `--ndm-version` output (see the file comment).  Malformed
// `format` lines are skipped; ok is false when the first line is not
// "ndm-plugin-protocol <N>".
inline PluginVersion parseNdmVersion(const std::string& text) {
    PluginVersion v;
    std::istringstream in(text);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (first) {
            first = false;
            int n = 0;
            if (key != "ndm-plugin-protocol" || !(ls >> n)) return v;
            v.protocol = n;
            v.ok = true;
            continue;
        }
        if (key != "format") continue;
        FormatVersion f;
        std::string rest;
        if (!(ls >> f.name >> f.read >> f.write) || (ls >> rest)) continue;
        if (f.read < 1 || f.write < 1) continue;
        v.formats.push_back(f);
    }
    return v;
}

// The formats both sides know where one writes a version the other cannot
// read, one message each ("wtl: the plugin reads up to v1, this build writes
// v2").  A format only one side lists is not a mismatch.
inline std::vector<std::string> formatMismatches(const std::vector<FormatVersion>& host,
                                                 const std::vector<FormatVersion>& plugin) {
    std::vector<std::string> out;
    for (const FormatVersion& h : host)
        for (const FormatVersion& p : plugin) {
            if (h.name != p.name) continue;
            if (p.read < h.write)
                out.push_back(h.name + ": the plugin reads up to v" + std::to_string(p.read)
                              + ", this build writes v" + std::to_string(h.write));
            if (h.read < p.write)
                out.push_back(h.name + ": the plugin writes v" + std::to_string(p.write)
                              + ", this build reads up to v" + std::to_string(h.read));
        }
    return out;
}

}  // namespace contract
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_VERSION_CONTRACT_HPP
