/***************************************************************************
 * process_initeap — pre-construct the EAP membership layer for a group.
 *
 * The template-class model (claude/eap-template-class-design.md) stores spike
 * membership in an N×T int8 .eap matrix whose column index IS a stable
 * template-class id, pre-allocated from the spike group's `nCells` (default
 * 128).  This setup tool writes, once per group, an ALL-ABSENT .eap and a
 * FREE-slot .tcl registry, so later tools (fiber-template, decollide, Klusters)
 * only ever fill an already-sized matrix.
 *
 *   process_initeap --session S --group G
 *       [--method M] [--n-cells N] [--param-file P] [--overwrite 0|1]
 *
 * N (rows) = the group's spike count, taken from its .res (method-independent,
 * resolved leniently).  T (columns) = --n-cells if given, else the group's
 * `nCells:` in --param-file, else 128.  Writes the method-less, stage-independent
 *   <base>.eap.<group>   (all EAP_ABSENT)
 *   <base>.tcl.<group>   (nCells FREE slots)
 * and refuses to clobber either unless --overwrite 1.
 ***************************************************************************/
#include <neurosuite/core/neurofileio.h>
#include <neurosuite/core/custody.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace cst = neurosuite::custody;

// Scan the Nth (1-based) spikeDetection channel group's `nCells:`.  The scan is
// ANCHORED to the top-level `spikeDetection:` section and bounded by the next
// top-level key: the session YAML also carries `anatomicalDescription:` groups
// that use the same `- channels:` token, so counting groups from the top of the
// file (as a naive scan, or process_decomposecollisions' read_group_params, does)
// lands in the wrong section.  Returns -1 when absent -> caller falls back to 128.
static int read_ncells(const std::string& yaml_path, int group_idx)
{
    std::ifstream f(yaml_path);
    if (!f.is_open()) return -1;
    std::string line;
    bool in_section = false;      // inside the top-level spikeDetection: block
    bool in_group   = false;      // inside the target group's entry
    int  grp = 0;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // A top-level key (column 0, not a comment) bounds sections.
        if (!line.empty() && line[0] != ' ' && line[0] != '\t' && line[0] != '#') {
            in_section = (line.rfind("spikeDetection", 0) == 0);
            in_group = false;
            continue;
        }
        if (!in_section) continue;
        if (line.find("- channels:") != std::string::npos) { ++grp; in_group = (grp == group_idx); continue; }
        if (in_group) {
            const std::size_t p = line.find("nCells:");
            if (p != std::string::npos) {
                try { return std::stoi(line.substr(p + 7)); } catch (...) { return -1; }
            }
        }
    }
    return -1;
}

static bool bool_of(const char* s)
{
    const std::string v(s);
    return v != "0" && v != "false" && v != "no";
}

int main(int argc, char** argv)
{
    std::string session, method, param_file;
    int  group   = 0;
    int  n_cells = -1;        // -1 = not given on the CLI
    bool overwrite = false;

    for (int i = 1; i + 1 < argc; ++i) {
        const std::string k = argv[i];
        const char* v = argv[++i];
        if      (k == "--session")    session    = v;
        else if (k == "--method")     method     = v;
        else if (k == "--group")      group      = std::atoi(v);
        else if (k == "--n-cells")    n_cells    = std::atoi(v);
        else if (k == "--param-file") param_file = v;
        else if (k == "--overwrite")  overwrite  = bool_of(v);
    }

    if (session.empty() || group < 1) {
        std::fprintf(stderr,
            "Usage: process_initeap --session S --group G\n"
            "  [--method M] [--n-cells N] [--param-file P] [--overwrite 0|1]\n"
            "Pre-constructs <base>.eap.<G> (all-absent) + <base>.tcl.<G> (free slots).\n");
        return 1;
    }

    // Resolve the group's .res (membership row count is method-independent, so
    // accept any method's res).  resolveAny walks method -> standard/stderiv/sdiff
    // -> a directory scan for any bare token -> untagged.
    cst::Resolved res_r = cst::resolve(session, "res", group, method);
    if (!res_r.found) res_r = cst::resolveAny(session, "res", group, method);
    if (!res_r.found) {
        std::fprintf(stderr, "  group %d: no .res found (looked at %s); cannot size .eap\n",
                     group, res_r.path.c_str());
        return 1;
    }
    bool ok = false;
    const std::vector<int64_t> times =
        neurofileio::isBinaryClusterRes(res_r.path)
            ? neurofileio::readResBinary(res_r.path, &ok)
            : neurofileio::readRes(res_r.path, &ok);
    if (!ok) {
        std::fprintf(stderr, "  group %d: could not read %s\n", group, res_r.path.c_str());
        return 1;
    }
    const int64_t nSpikes = static_cast<int64_t>(times.size());

    // Column count T: CLI override, else the YAML group block, else 128.
    int T = n_cells;
    if (T < 0 && !param_file.empty()) T = read_ncells(param_file, group);
    if (T < 0) T = 128;
    if (T < 1) { std::fprintf(stderr, "  group %d: nCells must be >= 1 (got %d)\n", group, T); return 1; }

    const std::string eapPath = cst::untaggedPath(session, "eap", group);
    const std::string tclPath = cst::untaggedPath(session, "tcl", group);

    if (!overwrite) {
        const bool haveEap = std::ifstream(eapPath).good();
        const bool haveTcl = std::ifstream(tclPath).good();
        if (haveEap || haveTcl) {
            std::fprintf(stderr, "  group %d: %s%s%s exists; pass --overwrite 1 to replace\n",
                         group, haveEap ? eapPath.c_str() : "",
                         (haveEap && haveTcl) ? " and " : "",
                         haveTcl ? tclPath.c_str() : "");
            return 0;                       // not an error: already set up
        }
    }

    if (!neurofileio::initEap(eapPath, nSpikes, T, group)) {
        std::fprintf(stderr, "  group %d: failed to write %s\n", group, eapPath.c_str());
        return 1;
    }
    if (!neurofileio::writeTcl(tclPath, neurofileio::initTcl(T))) {
        std::fprintf(stderr, "  group %d: failed to write %s\n", group, tclPath.c_str());
        return 1;
    }

    std::fprintf(stderr, "  group %d: %lld spikes x %d classes -> %s + %s\n",
                 group, static_cast<long long>(nSpikes), T, eapPath.c_str(), tclPath.c_str());
    return 0;
}
