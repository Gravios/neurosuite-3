// undohistory.cpp -- see undohistory.h.

#include "undohistory.h"

int UndoHistory::stepCount(const QList<Entry>& l, bool labelStepsOnly)
{
    int n = 0, last = -1;
    for (const Entry& e : l) {
        if (labelStepsOnly && e.annotation) continue;
        if (e.group == 0 || e.group != last) ++n;
        last = e.group ? e.group : -1;
    }
    return n;
}

int UndoHistory::childEntriesOnTop(Direction d) const
{
    int n = 0;
    for (const Entry& e : side(d)) { if (!e.child) break; ++n; }
    return n;
}

int UndoHistory::parentLevels() const
{
    int n = 0;
    for (const Entry& e : undoEntries) n += (!e.child && !e.annotation);
    return n;
}

void UndoHistory::push(Entry e, int depth, int entryCap)
{
    e.group = groupCurrent;
    if (joinNextPush && !undoEntries.isEmpty()) {
        // Klusters' own follow-up to the edit before it (the renumbering after a merge).
        if (undoEntries.first().group == 0) undoEntries.first().group = ++groupCounter;
        e.group = undoEntries.first().group;
    }
    joinNextPush = false;
    undoEntries.prepend(e);
    // The depth counts STEPS: the oldest step goes whole.  Each entry holds a labelling,
    // so the entries are bounded too, and a command larger than that loses its oldest.
    while (undoEntries.size() > 1
           && (stepCount(undoEntries, true) > qMax(1, depth) || undoEntries.size() > entryCap))
        undoEntries.removeLast();
    redoEntries.clear();                           // a new edit ends every redo, of either layer
}

void UndoHistory::limitEntries(int n)
{
    while (undoEntries.size() > qMax(1, n)) undoEntries.removeLast();
    redoEntries.clear();
}

void UndoHistory::clear()
{
    undoEntries.clear();
    redoEntries.clear();
    closeGroups();
    joinNextPush = false;
}
