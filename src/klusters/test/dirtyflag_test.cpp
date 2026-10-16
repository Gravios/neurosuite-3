// dirtyflag_test.cpp -- the document "modified" flag a background save clears.
//
// A save that started before an edit must not mark that edit as saved.
// Checked: bool-like reads and writes; clearIfUnchanged clears only when no
// set happened after the serial was read; and, under threads, that an edit
// racing the clear is never lost (run it under -fsanitize=thread too).
// Qt-free, own main, run via ctest.

#include "dirtyflag.h"

#include <cstdio>
#include <string>
#include <thread>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what)
{
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

int main()
{
    {
        DirtyFlag m;
        check(!m && m.serial() == 0, "starts clean");
        m = true;
        check(m && m.serial() == 1, "set");
        m = true;
        check(m.serial() == 2, "each set counts");
        m = false;
        check(!m && m.serial() == 2, "clear keeps the count");
        check(!m.clearIfUnchanged(2), "nothing to clear when clean");
    }
    {
        DirtyFlag m;
        m = true;
        const auto s = m.serial();          // save takes its snapshot
        check(m.clearIfUnchanged(s) && !m, "no edit during the save: cleared");
    }
    {
        DirtyFlag m;
        m = true;
        const auto s = m.serial();          // save takes its snapshot
        m = true;                           // edit while the save runs
        check(!m.clearIfUnchanged(s) && m, "edit during the save stays modified");
    }
    {
        // An edit thread sets the flag while a save thread clears it with
        // snapshots it took itself.  Whatever the interleaving, the last edit
        // is never cleared by a save whose snapshot predates it.
        DirtyFlag m;
        bool lost = false;
        for (int round = 0; round < 2000 && !lost; ++round) {
            m = true;
            const auto s = m.serial();
            std::thread editor([&] { m = true; });
            std::thread saver([&] { m.clearIfUnchanged(s); });
            editor.join(); saver.join();
            if (!m) lost = true;            // the editor's set came after s
        }
        check(!lost, "a racing edit is never lost");
    }
    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
