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
    for (const Entry& e : undoEntries) n += (!e.child && !e.annotation && !e.realign);
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

qint64 UndoHistory::totalCost() const
{
    qint64 n = 0;
    for (const Entry& e : undoEntries) n += e.cost;
    for (const Entry& e : redoEntries) n += e.cost;
    return n;
}
bool UndoHistory::dropOldestCostly()
{
    for (QList<Entry>* l : { &undoEntries, &redoEntries }) {
        for (int i = static_cast<int>(l->size()) - 1; i >= 0; --i) {
            if (l->at(i).cost <= 0) continue;
            const int g = l->at(i).group;
            if (g == 0) { l->removeAt(i); return true; }
            for (int k = static_cast<int>(l->size()) - 1; k >= 0; --k)
                if (l->at(k).group == g && l->at(k).cost > 0) l->removeAt(k);
            return true;
        }
    }
    return false;
}
int UndoHistory::removeRealignEntries()
{
    int n = 0;
    for (QList<Entry>* l : { &undoEntries, &redoEntries })
        for (int i = static_cast<int>(l->size()) - 1; i >= 0; --i)
            if (l->at(i).realign) { l->removeAt(i); ++n; }
    return n;
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
