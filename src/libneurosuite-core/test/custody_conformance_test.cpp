// custody_conformance_test.cpp — runs the shared chain-of-custody vectors
// (custody_vectors.tsv) against custody.hpp.  ONE table, three runners (this,
// the Python test, and a future bash mirror) so the implementations cannot
// drift.  Self-contained; built when NS_BUILD_TESTS=ON, run via ctest.
//
// Usage: custody_conformance_test <path-to-custody_vectors.tsv>
// (CMake passes ${CMAKE_CURRENT_SOURCE_DIR}/custody_vectors.tsv).

#include "neurosuite/core/custody.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace neurosuite::custody;

static int g_fail = 0;
static int g_ran  = 0;

static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

// Split on '\t' preserving trailing/empty fields.
static std::vector<std::string> tabs(const std::string& line) {
    std::vector<std::string> out;
    std::string::size_type start = 0, tab;
    while ((tab = line.find('\t', start)) != std::string::npos) {
        out.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    out.push_back(line.substr(start));
    return out;
}

static std::string klassName(Klass k) {
    switch (k) {
        case Klass::MethodSpecific: return "MethodSpecific";
        case Klass::Shared:         return "Shared";
        case Klass::SessionWide:    return "SessionWide";
    }
    return "?";
}

static std::vector<std::string> csv(const std::string& s) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::string::size_type start = 0, c;
    while ((c = s.find(',', start)) != std::string::npos) {
        out.push_back(s.substr(start, c - start));
        start = c + 1;
    }
    out.push_back(s.substr(start));
    return out;
}

static std::string baseNameOf(const std::string& p) {
    const std::string::size_type slash = p.find_last_of("/\\");
    return (slash == std::string::npos) ? p : p.substr(slash + 1);
}

int main(int argc, char** argv) {
    const std::string vpath = (argc > 1) ? argv[1] : "custody_vectors.tsv";
    std::ifstream in(vpath);
    if (!in) { std::printf("FAIL: cannot open vectors %s\n", vpath.c_str()); return 2; }

    char tmpl[] = "/tmp/custodyvecXXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) { std::printf("FAIL: mkdtemp\n"); return 2; }
    const std::string base = std::string(dir) + "/sess";

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> f = tabs(line);
        const std::string& kind = f[0];

        if (kind == "classify") {
            check(klassName(classify(f[1])) == f[2],
                  "classify " + f[1] + " -> " + f[2]);
        } else if (kind == "method_of") {
            check(methodOf(f[1]) == f[2],
                  "method_of " + f[1] + " -> '" + f[2] + "'");
        } else if (kind == "parse_anchor") {
            const Anchor a = parseAnchor(f[1]);
            const bool wantOk = (f[7] == "1");
            bool ok = (a.ok == wantOk);
            if (wantOk) {
                ok = ok && a.base == f[2] && a.type == f[3] && a.method == f[4]
                        && std::to_string(a.group) == f[5] && a.suffix == f[6];
            }
            check(ok, "parse_anchor " + f[1]);
        } else if (kind == "resolve") {
            for (const std::string& suf : csv(f[1]))
                std::ofstream(base + "." + suf).put('x');
            const int group = std::atoi(f[3].c_str());
            const Resolved r = resolve(base, f[2], group, f[4]);
            const bool wantFound = (f[6] == "1");
            check(baseNameOf(r.path) == f[5] && r.found == wantFound,
                  "resolve " + f[2] + "/" + f[3] + "/" + f[4]
                      + " -> " + f[5] + " (found=" + f[6] + ")");
            for (const std::string& suf : csv(f[1]))
                std::remove((base + "." + suf).c_str());
        } else if (kind == "method_token") {
            const MethodSpec ms = parseMethodToken(f[1]);
            const std::string kindStr = ms.kind ? std::string(1, ms.kind) : std::string();
            const std::string orderStr = (ms.order < 0) ? std::string()
                                                        : std::to_string(ms.order);
            check(ms.family == f[2] && kindStr == f[3] && orderStr == f[4],
                  "method_token " + f[1]);
        } else if (kind == "is_stderiv") {
            check(isStderivMethod(f[1]) == (f[2] == "1"),
                  "is_stderiv " + f[1] + " -> " + f[2]);
        } else {
            std::printf("FAIL: unknown vector kind '%s'\n", kind.c_str());
            ++g_fail;
        }
    }

    std::printf("custody conformance: %d checks, %d failed\n", g_ran, g_fail);
    if (g_fail == 0) std::printf("ALL CUSTODY CONFORMANCE TESTS PASS\n");
    return g_fail == 0 ? 0 : 1;
}
