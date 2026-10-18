// filecommit_test.cpp -- the all-or-nothing commit and the parity test used by
// the Klusters save.
//
// Checks: a stamp proves parity without reading, a write breaks it, an equal
// rewrite is found by comparison and refreshes the stamp, and a racy stamp is
// never trusted; idFileEquals; stageCopy; a commit installs every target and
// leaves no journal or .rollback; a commit whose third install fails leaves
// every target as it was; an interrupted commit (journal present, half the
// renames done) is rolled forward by recoverCommit.  Qt-free, run via ctest.

#include "filecommit.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace filecommit;
namespace fs = std::filesystem;

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what)
{
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}
static void put(const std::string& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; }
static std::string get(const std::string& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int main()
{
    const fs::path dir = fs::temp_directory_path() / ("filecommit_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string w = (dir / "a.spk.pending").string(), t = (dir / "a.spk").string();

    // ---- stamps ----
    // The stamp's fields are real values on the Unix-epoch nanosecond scale on
    // every platform (POSIX stat, Win32 file information), so a fast-path
    // decision means the same thing on both.
    {
        const std::int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        put(w, "0123456789");
        const FileStamp a = stampOf(w);
        check(a.ok && a.size == 10, "a stamp records the size");
        check(a.mtimeNs > now - 60'000'000'000LL && a.mtimeNs < now + 60'000'000'000LL,
              "a stamp's mtime is now, on the Unix-epoch scale");
        check(a == stampOf(w), "an untouched file keeps its stamp");
        put(w, "0123456789abc");
        check(stampOf(w) != a && stampOf(w).size == 13, "a rewrite changes the stamp");
        check(!stampOf((dir / "missing").string()).ok, "a missing file has no stamp");
        const SyncStamp m = markInSync(w, w);
        check(m.work.mtimeNs < m.takenNs - 5'000'000'000LL
              && m.work.mtimeNs > m.takenNs - 15'000'000'000LL,
              "markInSync moves the working copy's mtime about 10 s back");
    }

    // ---- parity ----
    put(t, "0123456789");
    put(w, "0123456789");
    SyncStamp st = markInSync(w, t);
    check(st.valid, "markInSync records a stamp");
    check(atParity(w, t, st), "in sync");
    put(w, "0123456780");                                   // realign-style rewrite, same size
    check(!atParity(w, t, st), "a write to the working copy breaks parity");
    put(w, "0123456789");                                   // written back to the same bytes
    check(atParity(w, t, st), "an equal rewrite is found by comparison");
    check(st.work == stampOf(w), "and the stamp is refreshed");
    {
        // A stamp the files still match but that is racy (taken in the same tick
        // as the working copy's mtime) must not be trusted.
        put(w, "XXXXXXXXXX");
        SyncStamp racy;
        racy.valid = true; racy.work = stampOf(w); racy.target = stampOf(t);
        racy.takenNs = racy.work.mtimeNs + 1'000'000;      // 1 ms after the write
        check(!atParity(w, t, racy), "a racy stamp is not trusted (files differ)");
        // The same stamp taken long after the write IS trusted -- proving the fast
        // path does not read the files (they differ, yet it says parity).
        SyncStamp old = racy;
        old.takenNs = racy.work.mtimeNs + 20'000'000'000LL;
        check(atParity(w, t, old), "a settled stamp is trusted without reading");
    }
    put(w, "short");
    SyncStamp none;
    check(!atParity(w, t, none), "different sizes differ");
    check(!atParity((dir / "missing").string(), t, none), "a missing file differs");

    // ---- idFileEquals / stageCopy ----
    {
        const std::string clu = (dir / "a.clu.1").string();
        std::ofstream o(clu, std::ios::binary);
        const int32_t v[] = {3, 2, 2, 5};
        o.write(reinterpret_cast<const char*>(v), sizeof v);
        o.close();
        check(idFileEquals(clu, 3, {2, 2, 5}), "idFileEquals: same");
        check(!idFileEquals(clu, 3, {2, 2, 4}) && !idFileEquals(clu, 4, {2, 2, 5})
              && !idFileEquals(clu, 3, {2, 2}), "idFileEquals: ids, header, length");
        std::string e;
        check(stageCopy(clu, (dir / "b.clu.1").string(), &e), "stageCopy: " + e);
        check(get((dir / "b.clu.1.saving").string()) == get(clu) && !fs::exists(dir / "b.clu.1"),
              "stageCopy writes only the staged file");
        discardStaged({(dir / "b.clu.1").string()});
        check(!fs::exists(dir / "b.clu.1.saving"), "discardStaged");
    }

    // ---- commit ----
    const std::string t1 = (dir / "x.clu").string(), t2 = (dir / "x.clc").string(),
                      t3 = (dir / "x.clp").string(), j = (dir / ".x.clu.commit").string();
    const std::vector<std::string> all = {t1, t2, t3};
    put(t1, "old1"); put(t2, "old2");                        // t3 is new
    ::chmod(t1.c_str(), 0660);
    put(staged(t1), "new1"); put(staged(t2), "new2"); put(staged(t3), "new3");
    std::string err;
    check(commitStaged(all, j, &err, {t2}), "commit: " + err);
    check(get(t2 + ".bak") == "old2" && !fs::exists(t1 + ".bak"), "commit keeps a .bak where asked");
    check(get(t1) == "new1" && get(t2) == "new2" && get(t3) == "new3", "commit installs every target");
    check(!fs::exists(j) && !fs::exists(t1 + ".rollback") && !fs::exists(staged(t1)), "commit cleans up");
    check((fs::status(t1).permissions() & fs::perms::group_write) != fs::perms::none, "commit keeps the mode");

    // third install fails (its old file cannot be moved aside: a non-empty
    // directory sits at its .rollback name): nothing changes
    put(staged(t1), "v3-1"); put(staged(t2), "v3-2"); put(staged(t3), "v3-3");
    fs::create_directories(t3 + ".rollback");
    put((fs::path(t3 + ".rollback") / "f").string(), "x");
    check(!commitStaged(all, j, &err) && !err.empty(), "a failed install fails the commit");
    check(get(t1) == "new1" && get(t2) == "new2" && get(t3) == "new3", "and leaves every target as it was");
    check(!fs::exists(j) && !fs::exists(t1 + ".rollback") && !fs::exists(staged(t1)), "and cleans up");
    fs::remove_all(t3 + ".rollback");

    check(!commitStaged({t1, (dir / "nothing").string()}, j, &err), "a target with nothing staged is refused");
    check(get(t1) == "new1", "before anything moves");

    // interrupted commit: journal written, t1 installed, t2 moved aside, t3 untouched
    put(staged(t1), "v4-1"); put(staged(t2), "v4-2"); put(staged(t3), "v4-3");
    const std::string j2 = (dir / ".x.other.commit").string();
    for (const auto& jp : {j, j2}) { std::ofstream jj(jp); jj << "klusters-commit 2\n" << t1 << '\n' << t2 << '\n' << t3 << "\nend 3\n"; }
    fs::rename(t1, t1 + ".rollback"); fs::rename(staged(t1), t1);
    fs::rename(t2, t2 + ".rollback");
    std::string report;
    check(recoverCommit(j, &report), "recover: " + report);
    check(get(t1) == "v4-1" && get(t2) == "v4-2" && get(t3) == "v4-3", "recovery rolls the commit forward");
    check(!fs::exists(j) && !fs::exists(t1 + ".rollback") && !fs::exists(t2 + ".rollback"),
          "recovery removes the journal and the rollbacks");
    check(recoverCommit(j, &report), "no journal: nothing to do");
    check(recoverCommit(j2, &report) && !fs::exists(j2) && get(t1) == "v4-1",
          "the second journal of the same commit finds nothing left to do");

    // a journal cut short (no "end" line): nothing was replaced; discard
    put(staged(t1), "v5-1"); put(staged(t2), "v5-2");
    { std::ofstream jj(j); jj << "klusters-commit 2\n" << t1 << '\n' << t2.substr(0, 5); }
    check(recoverCommit(j, &report), "truncated journal: " + report);
    check(get(t1) == "v4-1" && get(t2) == "v4-2" && !fs::exists(staged(t1)) && !fs::exists(j),
          "a truncated journal discards the staged files and touches nothing");

    // a crash while undoing a failed commit: the undo is finished
    put(staged(t2), "v6-2"); put(staged(t3), "v6-3");
    fs::rename(t1, t1 + ".rollback"); put(t1, "v6-1");          // t1 was installed
    { std::ofstream jj(j); jj << "klusters-commit 2\n" << t1 << '\n' << t2 << '\n' << t3 << "\nend 3\nundo\n"; }
    check(recoverCommit(j, &report), "undo recovery: " + report);
    check(get(t1) == "v4-1" && get(t2) == "v4-2" && get(t3) == "v4-3", "an interrupted undo is finished: all old");
    check(!fs::exists(staged(t2)) && !fs::exists(t1 + ".rollback") && !fs::exists(j), "and cleaned up");

    // two journals: a successful commit removes both
    put(staged(t1), "v7-1");
    check(commitStaged({t1}, std::vector<std::string>{j, j2}, &err) && !fs::exists(j) && !fs::exists(j2)
          && get(t1) == "v7-1", "a commit with two journals removes both");

    fs::remove_all(dir);
    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
