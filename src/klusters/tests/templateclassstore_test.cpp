// templateclassstore_test.cpp — the Qt-free EAP template-class model
// (TemplateClassStore): the app-level concerns the unit-tested engine leaves
// out — fresh-matrix load, create → the creation-provenance getters
// (provClu / provStage / created, mirroring label / members), the primary
// follow, out-of-range guards, the .eap/.tcl persistence round-trip, and the
// stage-independence of the .tcl (a class id — and its provenance — denotes the
// same column across every stage).  The store wraps neurosuite::templateclass +
// neurofileio, so this links Neurosuite::core.  Self-contained, run via ctest.

#include "templateclassstore.h"
#include "neurosuite/core/custody.hpp"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

int main()
{
    namespace cu = neurosuite::custody;
    const std::string base = "tclstore.tmp";
    const int group = 5;
    const int64_t nSpikes = 8;

    // Clean slate so the test is idempotent across ctest runs (a prior run's saved
    // .eap/.tcl would otherwise seed this one).
    const std::string baseEap = cu::untaggedPath(base, "eap", group);
    const std::string refEap  = baseEap + ".refine";
    const std::string tclPath = cu::untaggedPath(base, "tcl", group);
    auto cleanup = [&]{
        std::remove(baseEap.c_str());
        std::remove(refEap.c_str());
        std::remove(tclPath.c_str());
        std::remove((baseEap + ".sortB").c_str());
    };
    cleanup();

    // Fresh load (no .eap / .tcl on disk): an all-absent matrix + free registry.
    TemplateClassStore st;
    check(st.load(base, group, "refine", nSpikes), "store load ok (fresh)");
    check(st.ok(), "ok() true after load");
    check(st.activeClasses().empty(), "no active classes on a fresh matrix");
    check(st.primary() == -1, "no primary on a fresh matrix");

    // Create class A with explicit provenance.
    const int cA = st.createClass({0,1,2}, "cellA", /*provClu=*/479, "refine", "2026-10-05");
    check(cA >= 0, "createClass A returned a column");
    check(st.primary() == cA, "a new class becomes primary");
    check(st.label(cA)   == "cellA",      "label(A) round-trips");
    check(st.provClu(cA) == 479,          "provClu(A) is the originating cluster");
    check(st.provStage(cA) == "refine",   "provStage(A) round-trips");
    check(st.created(cA) == "2026-10-05", "created(A) round-trips");
    const std::vector<int64_t> mA = st.members(cA);
    check(mA.size() == 3 && mA[0] == 0 && mA[1] == 1 && mA[2] == 2, "members(A) = {0,1,2}");

    // Create class B — distinct column, primary moves to it.
    const int cB = st.createClass({3,4}, "cellB", /*provClu=*/512, "sortA", "2026-10-06");
    check(cB >= 0 && cB != cA, "createClass B returned a distinct column");
    check(st.primary() == cB, "primary moved to B");
    check(st.provClu(cB) == 512, "provClu(B) is the originating cluster");
    check(st.provClu(cA) == 479, "provClu(A) unchanged by B's creation");

    // Out-of-range guards mirror label(): -1 / "" rather than a crash.
    check(st.provClu(-1)      == -1, "provClu(-1) guarded");
    check(st.provClu(1 << 20) == -1, "provClu(huge) guarded");
    check(st.provStage(-1).empty(),  "provStage(-1) guarded");
    check(st.created(1 << 20).empty(), "created(huge) guarded");

    // Persistence: save + reload the same stage; provenance (and membership)
    // survive the .eap/.tcl round-trip.
    check(st.save(), "save ok");
    TemplateClassStore st2;
    check(st2.load(base, group, "refine", nSpikes), "reload (refine) ok");
    check(st2.provClu(cA) == 479 && st2.provClu(cB) == 512, "provClu persists across reload");
    check(st2.provStage(cA) == "refine" && st2.created(cA) == "2026-10-05", "provStage/created persist");
    check(st2.label(cA) == "cellA" && st2.label(cB) == "cellB", "labels persist");
    const std::vector<int64_t> mA2 = st2.members(cA);
    check(mA2.size() == 3, "members(A) persist across reload");

    // Stage-independence of the .tcl: loading a DIFFERENT stage reads the same
    // registry, so a class id's provenance denotes the same column everywhere.
    TemplateClassStore st3;
    check(st3.load(base, group, "sortB", nSpikes), "load a second stage (sortB) ok");
    check(st3.provClu(cA) == 479 && st3.provClu(cB) == 512, "provClu is stage-independent (.tcl shared)");

    cleanup();   // leave no stray files in the test working dir
    std::printf("templateclassstore_test: %d checks, %d failed\n", g_ran, g_fail);
    return g_fail ? 1 : 0;
}
