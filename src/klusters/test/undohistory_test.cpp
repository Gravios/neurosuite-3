// undohistory_test.cpp -- the bookkeeping of the one undo / redo history.
//
// Checked: an entry is a step; the entries of one command are one step, nested
// commands included; a joined entry shares the step before it; the depth counts
// the steps that hold a labelling and drops the oldest whole; annotations do not
// count against it; the entry cap; a push ends every redo; what the forced child
// undo can reach; the parent levels; a lowered depth; clear.
// No session, own main, run via ctest.

#include "undohistory.h"

#include <cstdio>
#include <memory>
#include <string>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what)
{
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

using H = UndoHistory;
static H::Entry parent() { return H::Entry(); }
static H::Entry child()  { H::Entry e; e.child = true; return e; }
static H::Entry note()   { H::Entry e; e.annotation = true; return e; }

int main()
{
    const int cap = 1000;
    {   // entries are steps; undoing moves them to the redo side
        H h;
        check(h.isEmpty(H::Undo) && h.isEmpty(H::Redo) && h.steps(H::Undo) == 0, "starts empty");
        h.push(parent(), 20, cap);
        h.push(child(), 20, cap);
        check(h.steps(H::Undo) == 2 && h.entryCount(H::Undo) == 2, "two edits, two steps");
        check(h.newest(H::Undo).child, "the newest entry is first");
        h.put(H::Redo, h.take(H::Undo));
        check(h.steps(H::Undo) == 1 && h.steps(H::Redo) == 1 && h.newest(H::Redo).child, "an undone entry can be redone");
        h.push(parent(), 20, cap);
        check(h.isEmpty(H::Redo) && h.steps(H::Undo) == 2, "a new edit ends every redo");
    }
    {   // a command is one step, nested commands included
        H h;
        h.push(parent(), 20, cap);
        h.beginGroup();
        h.push(parent(), 20, cap);
        h.beginGroup();
        h.push(child(), 20, cap);
        h.endGroup();
        h.push(child(), 20, cap);
        h.endGroup();
        h.push(parent(), 20, cap);
        check(h.entryCount(H::Undo) == 5 && h.steps(H::Undo) == 3, "a command of three entries is one step");
        check(h.newest(H::Undo).group == 0, "an entry after the command is its own step");
        h.take(H::Undo);
        const int g = h.newest(H::Undo).group;
        int n = 0;
        while (!h.isEmpty(H::Undo) && g != 0 && h.newest(H::Undo).group == g) { h.put(H::Redo, h.take(H::Undo)); ++n; }
        check(n == 3 && h.steps(H::Redo) == 1 && h.steps(H::Undo) == 1, "the command is undone whole and is one redo step");
    }
    {   // two commands in a row are two steps
        H h;
        h.beginGroup(); h.push(parent(), 20, cap); h.push(child(), 20, cap); h.endGroup();
        h.beginGroup(); h.push(parent(), 20, cap); h.push(child(), 20, cap); h.endGroup();
        check(h.steps(H::Undo) == 2, "two commands, two steps");
        h.endGroup();
        h.push(parent(), 20, cap);
        check(h.newest(H::Undo).group == 0, "an endGroup too many opens nothing");
    }
    {   // a joined entry shares the step of the one before it
        H h;
        h.push(parent(), 20, cap);
        h.push(parent(), 20, cap);
        h.joinNext();
        h.push(parent(), 20, cap);
        check(h.steps(H::Undo) == 2 && h.entryCount(H::Undo) == 3, "the joined entry is in the step before it");
        h.push(parent(), 20, cap);
        check(h.steps(H::Undo) == 3, "the join holds for one push");
        H e;
        e.joinNext();
        e.push(parent(), 20, cap);
        e.push(parent(), 20, cap);
        check(e.steps(H::Undo) == 2, "a join with nothing before it joins nothing, and does not linger");
        H c;
        c.joinNext(); c.joinNext(false);
        c.push(parent(), 20, cap); c.push(parent(), 20, cap);
        check(c.steps(H::Undo) == 2, "a join taken back");
    }
    {   // the depth counts steps and drops the oldest whole
        H h;
        h.beginGroup(); h.push(parent(), 2, cap); h.push(child(), 2, cap); h.push(child(), 2, cap); h.endGroup();
        h.push(parent(), 2, cap);
        check(h.steps(H::Undo) == 2 && h.entryCount(H::Undo) == 4, "within the depth nothing is dropped");
        h.push(child(), 2, cap);
        check(h.steps(H::Undo) == 2 && h.entryCount(H::Undo) == 2, "the oldest step goes whole, all three entries");
        h.push(parent(), 0, cap);
        check(h.steps(H::Undo) == 1, "a depth of 0 keeps one step");
    }
    {   // annotations do not count against the depth
        H h;
        h.push(parent(), 2, cap);
        h.push(parent(), 2, cap);
        for (int i = 0; i < 5; ++i) h.push(note(), 2, cap);
        check(h.steps(H::Undo) == 7 && h.parentLevels() == 2, "five annotations push no edit out");
        h.push(parent(), 2, cap);
        check(h.parentLevels() == 2 && h.steps(H::Undo) == 7, "the third edit drops the oldest, the annotations stay");
    }
    {   // the entry cap, and one command larger than it
        H h;
        h.beginGroup();
        for (int i = 0; i < 6; ++i) h.push(parent(), 20, 4);
        h.endGroup();
        check(h.entryCount(H::Undo) == 4 && h.steps(H::Undo) == 1, "a command larger than the cap loses its oldest entries");
    }
    {   // what the forced child undo can reach; the parent levels
        H h;
        h.push(child(), 20, cap);
        h.push(parent(), 20, cap);
        h.push(note(), 20, cap);
        check(h.childEntriesOnTop(H::Undo) == 0, "an annotation on top is no child edit");
        h.push(child(), 20, cap);
        h.push(child(), 20, cap);
        check(h.childEntriesOnTop(H::Undo) == 2, "the child edits above the newest other entry");
        check(h.parentLevels() == 1, "one entry stands for a parent level");
        h.put(H::Redo, h.take(H::Undo));
        check(h.childEntriesOnTop(H::Undo) == 1 && h.childEntriesOnTop(H::Redo) == 1, "and on the redo side");
    }
    {   // a lowered depth; clear
        H h;
        for (int i = 0; i < 6; ++i) h.push(parent(), 20, cap);
        h.put(H::Redo, h.take(H::Undo));
        h.limitEntries(3);
        check(h.entryCount(H::Undo) == 3 && h.isEmpty(H::Redo), "a lowered depth keeps the newest entries and ends the redo");
        h.limitEntries(0);
        check(h.entryCount(H::Undo) == 1, "at least one entry is kept");
        h.beginGroup(); h.joinNext();
        h.clear();
        h.push(parent(), 20, cap); h.push(parent(), 20, cap);
        check(h.steps(H::Undo) == 2 && h.newest(H::Undo).group == 0, "clear closes the open command and the join");
    }
    {   // realign entries: steps that count against the depth, no parent level, bytes kept
        H h;
        const auto rows = std::make_shared<const int>(0);
        auto realign = [&](qint64 cost) { H::Entry e; e.realign = rows; e.cost = cost; return e; };
        h.push(parent(), 20, cap);
        h.push(realign(100), 20, cap);
        check(h.steps(H::Undo) == 2 && h.parentLevels() == 1, "a realign is a step and stands for no parent level");
        check(h.childEntriesOnTop(H::Undo) == 0, "a realign on top is no child edit");
        for (int i = 0; i < 3; ++i) h.push(realign(10), 2, cap);
        check(h.steps(H::Undo) == 2 && h.totalCost() == 20, "realign steps count against the depth and leave with it");

        H g;                                           // the oldest step that keeps bytes goes, whole
        g.beginGroup(); g.push(realign(5), 20, cap); g.push(realign(6), 20, cap); g.endGroup();
        g.push(parent(), 20, cap);
        g.joinNext(); g.push(realign(7), 20, cap);     // a merge and its automatic realign
        g.push(realign(8), 20, cap);
        check(g.steps(H::Undo) == 3 && g.totalCost() == 26, "three steps keeping 26 bytes");
        check(g.dropOldestCostly() && g.totalCost() == 15 && g.steps(H::Undo) == 2, "the oldest costly step is dropped with all its entries");
        check(g.dropOldestCostly() && g.totalCost() == 8 && g.steps(H::Undo) == 2 && g.parentLevels() == 1,
              "from a mixed step only the entries that keep bytes go: the merge stays a step");
        g.put(H::Redo, g.take(H::Undo));
        check(g.dropOldestCostly() && g.totalCost() == 0 && g.isEmpty(H::Redo), "then the redo side's");
        check(!g.dropOldestCostly(), "nothing left to drop");

        H n;                                           // what the next Undo applies
        check(n.newestStep(H::Undo).isEmpty(), "no entry, no next step");
        n.push(parent(), 20, cap);
        n.beginGroup(); n.push(realign(1), 20, cap); n.push(realign(1), 20, cap); n.endGroup();
        check(n.newestStep(H::Undo).size() == 2 && n.newestStep(H::Undo).first().realign, "the next step is the whole command");
        n.push(parent(), 20, cap);
        check(n.newestStep(H::Undo).size() == 1 && !n.newestStep(H::Undo).first().realign, "a lone entry is a step of one");

        H r;                                           // a nudge: every realign entry goes, the rest stays
        r.push(parent(), 20, cap); r.push(realign(1), 20, cap); r.push(note(), 20, cap); r.push(realign(1), 20, cap);
        r.put(H::Redo, r.take(H::Undo));
        check(r.removeRealignEntries() == 2 && r.entryCount(H::Undo) == 2 && r.isEmpty(H::Redo) && r.totalCost() == 0,
              "removeRealignEntries drops them on both sides and nothing else");
    }
    std::printf("undohistory: %d checks, %d failed\n", g_ran, g_fail);
    return g_fail ? 1 : 0;
}
