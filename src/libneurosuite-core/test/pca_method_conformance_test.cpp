// pca_method_conformance_test.cpp — runs the shared PCAE transform-method vectors
// (pca_method_vectors.tsv) against pca_projection.hpp.  ONE table, two runners
// (this and the fiber-kit Python test) so the cross-repo Method enum and its
// helpers cannot drift.  Self-contained; built when NS_BUILD_TESTS=ON, run via ctest.
//
// Usage: pca_method_conformance_test <path-to-pca_method_vectors.tsv>
// (CMake passes ${CMAKE_CURRENT_SOURCE_DIR}/pca_method_vectors.tsv).

#include "neurosuite/core/pca_projection.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace neurosuite::core;

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

// Name -> enum constant, via the ACTUAL enum, so a reordered value makes the
// (int) comparison against the TSV fail.  Unknown names are a hard error.
static bool methodByName(const std::string& n, Method& m) {
    if (n == "Standard")         { m = Method::Standard;         return true; }
    if (n == "SdiffFirst")       { m = Method::SdiffFirst;       return true; }
    if (n == "SdiffLaplacian")   { m = Method::SdiffLaplacian;   return true; }
    if (n == "SdiffAllPairs")    { m = Method::SdiffAllPairs;    return true; }
    if (n == "StderivFirst")     { m = Method::StderivFirst;     return true; }
    if (n == "StderivLaplacian") { m = Method::StderivLaplacian; return true; }
    if (n == "StderivAllPairs")  { m = Method::StderivAllPairs;  return true; }
    if (n == "StderivCustom")    { m = Method::StderivCustom;    return true; }
    if (n == "StderivCustomCar") { m = Method::StderivCustomCar; return true; }
    return false;
}

int main(int argc, char** argv) {
    const std::string vpath = (argc > 1) ? argv[1] : "pca_method_vectors.tsv";
    std::ifstream in(vpath);
    if (!in) { std::printf("FAIL: cannot open vectors %s\n", vpath.c_str()); return 2; }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> f = tabs(line);
        const std::string& kind = f[0];

        if (kind == "format") {
            const long magic = std::strtol(f[1].c_str(), nullptr, 0);
            const int  ver   = std::atoi(f[2].c_str());
            check(static_cast<long>(kPcaeMagic) == magic,
                  "format magic " + f[1]);
            check(kPcaeVersion == ver, "format version " + f[2]);
        } else if (kind == "method") {
            const int         value    = std::atoi(f[1].c_str());
            const std::string& name    = f[2];
            const int         order    = std::atoi(f[3].c_str());
            const bool        temporal = (f[4] == "1");
            const std::string& tag     = f[5];
            Method m;
            if (!methodByName(name, m)) {
                std::printf("FAIL: unknown method name '%s'\n", name.c_str());
                ++g_fail; continue;
            }
            // Pins the integer value (the cross-repo contract).
            check(static_cast<int>(m) == value, "method " + name + " == " + f[1]);
            // Pins the helper outputs.
            check(static_cast<int>(spatialOrder(m)) == order,
                  "spatialOrder(" + name + ") == " + f[3]);
            check(hasTemporalDiff(m) == temporal,
                  "hasTemporalDiff(" + name + ") == " + f[4]);
            check(std::string(methodTag(m)) == tag,
                  "methodTag(" + name + ") == " + tag);
            // methodValid accepts every canonical value.
            check(methodValid(value), "methodValid(" + f[1] + ")");
        } else {
            std::printf("FAIL: unknown vector kind '%s'\n", kind.c_str());
            ++g_fail;
        }
    }
    // The enum has exactly the canonical values: anything outside 0..6 is invalid.
    check(!methodValid(-1) && !methodValid(9), "methodValid rejects out-of-range");

    std::printf("pca method conformance: %d checks, %d failed\n", g_ran, g_fail);
    if (g_fail == 0) std::printf("ALL PCA METHOD CONFORMANCE TESTS PASS\n");
    return g_fail == 0 ? 0 : 1;
}
