// hierarchyfiles_test.cpp -- the .clu/.clc/.clp helpers shared by Save and
// "Promote to Hierarchical Session".
//
// Checks: the nesting scan and its straddlers; the .clp payload; promotion of a
// flat .clu (identity atoms, artefact bin 0 -> atom 0, gaps unmapped) and that
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
        const std::vector<int> moved = {2, 3, 3, 3, 0, 4};          // atom 5 now under 2 and 4
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
        check(p.clc == clu, "promotion: atom id = fiber id per spike");
        check(p.clcCount == 4, "promotion: count header = distinct ids");
        check(p.clp == std::vector<int32_t>({1, 2, 0, 4}), "promotion: identity map, unused id 3 unmapped");
        const NestingScan s = scanNesting(clu, p.clc);
        check(s.ok && s.straddlers.empty(), "promoted pair is nested");
        check(clpFromMap(s.childToParent) == p.clp, "promoted .clp equals the map the pair implies");
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
