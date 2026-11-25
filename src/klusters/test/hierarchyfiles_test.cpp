// hierarchyfiles_test.cpp -- the .clu/.clc/.clp helpers shared by Save and
// "Promote to Hierarchical Session".
//
// Checks: the nesting scan and its straddlers; the .clp payload; promotion of a
// flat .clu (identity children, artifact bin 0 -> child 0, gaps unmapped) and that
// the promoted pair scans back to the same map with no straddler; sibling names
// keep method, group and suffix; a staged write leaves the target untouched
// until it is installed, keeps a .bak, and the files read back with the layout
// neurofileio::readCluBinary expects.  Qt-free, own main, run via ctest.

#include "hierarchyfiles.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>   // getpid

using namespace hierarchyfiles;

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what)
{
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

static std::vector<char> bytesOf(const std::string& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main()
{
    // ---- scanNesting ----
    {
        const std::vector<int> clu = {2, 2, 3, 3, 0, 2};
        const std::vector<int> clc = {5, 6, 7, 7, 0, 5};
        const NestingScan s = scanNesting(clu, clc);
        check(s.ok && s.straddlers.empty(), "nested pair: no straddler");
        check(s.childToParent.size() == 4 && s.childToParent.at(5) == 2 && s.childToParent.at(6) == 2
              && s.childToParent.at(7) == 3 && s.childToParent.at(0) == 0, "nested pair: map");
        const std::vector<int> moved = {2, 3, 3, 3, 0, 4};          // child 5 now under 2 and 4
        const NestingScan t = scanNesting(moved, clc);
        check(t.ok && t.straddlers == std::vector<int>{5}, "straddler found");
        check(t.childToParent.at(5) == 2, "straddler keeps its first-seen fiber");
        check(!scanNesting({1, 2}, {1}).ok, "length mismatch refused");
    }

    // ---- clpFromMap ----
    {
        const std::vector<int32_t> clp = clpFromMap({{0, 0}, {1, 1}, {3, 9}, {4, 2}});
        check(clp == std::vector<int32_t>({1, 0, 9, 2}), "clp: entry c-1, gap 0, atom 0 not stored");
        check(clpFromMap({}).empty() && clpFromMap({{0, 1}}).empty(), "clp: empty / atom 0 only");
    }

    // ---- promoteFlat ----
    {
        const std::vector<int> clu = {0, 1, 4, 4, 2, 1, 4};
        const Promotion p = promoteFlat(clu);
        check(p.clc == std::vector<int>({0, 1, 10004, 10004, 10002, 1, 10004}), "promotion: every fiber's one atom is its self child; the bins keep 0 and 1");
        check(p.clcCount == 4, "promotion: count header = distinct ids");
        check(p.clp.size() == 10004 && p.clp[0] == 1 && p.clp[10001] == 2 && p.clp[10003] == 4 && p.clp[10002] == 0 && p.clp[1] == 0,
              "promotion: .clp entry c-1 holds the fiber of atom c, unused ids unmapped");
        const NestingScan s = scanNesting(clu, p.clc);
        check(s.ok && s.straddlers.empty(), "promoted pair is nested");
        check(clpFromMap(s.childToParent) == p.clp, "promoted .clp equals the map the pair implies");
    }

    // ---- the numbering of children ----
    {
        check(selfChildOf(0) == 0 && selfChildOf(1) == 1, "the bins are their own self child");
        check(selfChildOf(2) == 10002 && selfChildOf(153) == 10153 && selfChildOf(9999) == 19999, "fiber p -> atom p + 10000");
        check(selfChildOf(kReservedClusterLo) == kReservedClusterLo, "a reserved chunk cluster is its own");
        check(isSelfChildId(10002) && isSelfChildId(19999) && !isSelfChildId(10001) && !isSelfChildId(20000) && !isSelfChildId(153),
              "the self-child block is 10002 .. 19999");
        check(parentOfSelfChild(10153) == 153 && parentOfSelfChild(1) == 1 && parentOfSelfChild(0) == 0
              && parentOfSelfChild(20000) == -1 && parentOfSelfChild(153) == -1 && parentOfSelfChild(kReservedClusterLo) == kReservedClusterLo,
              "the fiber an atom is the self child of, by id");
        check(parentIdFits(0) && parentIdFits(9999) && !parentIdFits(10000) && parentIdFits(kReservedClusterLo), "fibers are 0 .. 9999 and the reserved ids");
        bool roundTrip = true;
        for (int p2 = 0; p2 < kSelfChildBase; ++p2) roundTrip = roundTrip && parentOfSelfChild(selfChildOf(p2)) == p2;
        check(roundTrip, "fiberOfSelfChild undoes selfChildOf for every fiber");
    }

    // ---- upgradeChildIds: a .clc in the old numbering ----
    {
        //                               parent: 0  1  4      4      4      2      2   7   7
        const std::vector<int> parent      = {0, 1, 4,     4,     4,     2,     2,  7,  7};
        const std::vector<int> oldChild    = {0, 1, 4,     9,     9,     2,     2,  3, 12};   // 4, 2: self; 9 under 4; 3, 12 under 7
        const ChildIdUpgrade u = upgradeChildIds(parent, oldChild);
        check(u.needed, "ids below 10000 mark the old numbering");
        check(u.ids == std::vector<int>({0, 1, 10004, 20001, 20001, 10002, 10002, 20000, 20002}),
              "self children move to fiber + 10000, the others to 20000.. in ascending old id; the bins stay");
        check(u.oldToNew == std::map<int, int>({{2, 10002}, {3, 20000}, {4, 10004}, {9, 20001}, {12, 20002}}), "the map of every atom that changed");
        const NestingScan s = scanNesting(parent, u.ids);
        check(s.ok && s.straddlers.empty() && s.childToParent.at(20001) == 4 && s.childToParent.at(10002) == 2, "the upgraded pair nests as the old one did");
        const ChildIdUpgrade again = upgradeChildIds(parent, u.ids);
        check(!again.needed && again.ids.empty(), "a file in the new numbering is left alone");
        // an old child numbered like a parent it does not sit under is not a self child
        const ChildIdUpgrade other = upgradeChildIds({2, 2, 5}, {5, 5, 8});
        check(other.needed && other.ids == std::vector<int>({20000, 20000, 20001}), "atom 5 under fiber 2 is not fiber 5's self child");
        // reserved ids keep their number; a high old id is renumbered with the rest
        const int rsv = static_cast<int>(kReservedClusterLo);
        const ChildIdUpgrade high = upgradeChildIds({3, rsv, 3}, {3, rsv, 15000});
        check(high.needed && high.ids == std::vector<int>({10003, rsv, 20000}), "reserved ids stay, an old id above 10000 is renumbered");
        check(!upgradeChildIds({1, 2}, {1}).needed, "arrays of different length: nothing is done");
    }

    // ---- siblingPath ----
    {
        check(siblingPath("/d/rat.clu.stderiv_C5.3.microfiber", "clc") == "/d/rat.clc.stderiv_C5.3.microfiber",
              "sibling keeps method, group and suffix");
        check(siblingPath("/d/rat.clu.3", "clp") == "/d/rat.clp.3", "sibling of an untagged name");
        check(siblingPath("/d/rat.clu.2/rat.fet.3", "clc").empty(), "a .clu. in the directory is not the file's");
        check(siblingPath("/d/rat.fet.3", "clc").empty(), "no .clu. -> empty");
    }

    // ---- staged writes ----
    {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / ("hierarchyfiles_test_" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir);
        const std::string clp = (dir / "s.clp.3").string();
        std::string err;

        check(writeIdFile(clp, 3, {1, 2, 2}, true, &err), "first write: " + err);
        check(!std::filesystem::exists(clp + ".bak"), "first write: no .bak without an old file");
        const IdFile a = readIdFile(clp);
        check(a.ok && a.header == 3 && a.ids == std::vector<int32_t>({1, 2, 2}), "first write reads back");

        const std::vector<char> before = bytesOf(clp);
        check(stageIdFile(clp, 4, std::vector<int>{7, 7, 7, 8}, &err), "stage: " + err);
        check(bytesOf(clp) == before, "staging leaves the target untouched");
        check(std::filesystem::exists(stagedPath(clp)), "staged file exists");
        check(installStaged(clp, true, &err), "install: " + err);
        check(!std::filesystem::exists(stagedPath(clp)), "install consumes the staged file");
        check(bytesOf(clp + ".bak") == before, "install keeps the previous file as .bak");
        const IdFile b = readIdFile(clp);
        check(b.ok && b.header == 4 && b.ids == std::vector<int32_t>({7, 7, 7, 8}), "installed content");
        check(std::filesystem::file_size(clp) == 4 + 4 * 4, "layout: header + one int32 per id");

        namespace fs = std::filesystem;
        fs::permissions(clp, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read
                             | fs::perms::group_write, fs::perm_options::replace);
        check(writeIdFile(clp, 4, {7, 7, 7, 8}, false, &err), "rewrite: " + err);
        check((fs::status(clp).permissions() & fs::perms::group_write) != fs::perms::none,
              "a replaced file keeps its permission bits");

        check(stageIdFile(clp, 1, std::vector<int>{1}, &err), "stage again");
        discardStaged(clp);
        check(!std::filesystem::exists(stagedPath(clp)) && readIdFile(clp).ids.size() == 4,
              "discard drops the staged file and keeps the target");
        check(!installStaged(clp, false, &err), "install with nothing staged fails");

        const std::string unwritable = (dir / "missing-dir" / "x.clc.1").string();
        check(!stageIdFile(unwritable, 0, std::vector<int>{1}, &err) && !err.empty(),
              "stage into a missing directory fails with a message");

        std::ofstream(dir / "odd.clp.1", std::ios::binary) << "abcde";
        check(!readIdFile((dir / "odd.clp.1").string()).ok, "a file of 5 bytes is not an id file");
        std::filesystem::remove_all(dir);
    }

    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
