/***************************************************************************
 *   undohistory.h -- the one undo / redo history of a document.
 *
 *   What Undo and Redo walk: the entries, newest first, the commands that
 *   group them, and the depth.  It holds no cluster data and does nothing to
 *   a document: KlustersDoc takes an entry from it, applies it
 *   (klustersdoc_undo.cpp) and puts it on the other side.  That keeps the
 *   bookkeeping -- which entries are one step, what the depth drops -- in one
 *   place that can be tested without a session (undohistory_test.cpp).
 ***************************************************************************/
#ifndef UNDOHISTORY_H
#define UNDOHISTORY_H

#include <QList>
#include <memory>
#include <QVector>

#include "types.h"            // dataType
#include "unitannotation.h"

class UndoHistory {
public:
    /// The child clusters one child edit changed, for the curation log.
    struct ChildEdit {
        QList<int> added;
        QList<int> modified;
        QList<int> deleted;
        int logIdx = -1;      ///< action_idx of the edit's curation-log entry (-1: none)
    };

    /** One entry.  A parent entry stands for one level of the parent Data history;
     *  a child entry and an annotation entry stand for none.  Each of the first two
     *  carries the child labelling to restore when it is applied: the layers are not
     *  independent (a parent edit re-cuts the children), so an entry of either layer
     *  puts the children back as they were. */
    struct Entry {
        bool child = false;
        /// The entries one command pushed share a group and are one step; 0: a step of its own.
        int  group = 0;
        QVector<dataType> childLabels;   ///< empty: no child layer when it was pushed
        ChildEdit edit;                  ///< child entries: the ids, for the log
        /// A change of one cluster's information on the layer `child` says: no
        /// labelling, no level, and not counted against the depth.
        bool annotation = false;
        int  annotatedCluster = 0;
        UnitAnnotation annotationToRestore;
        /// A realign: the rows it rewrote, as they were and as it left them (the
        /// document's record; this class does not look inside).  No labelling and no
        /// level, but a step that counts against the depth, and `cost` bytes kept.
        std::shared_ptr<const void> realign;
        qint64 cost = 0;
    };

    enum Direction { Undo, Redo };

    // ── commands ────────────────────────────────────────────────────────────
    /// Every entry pushed until the matching endGroup (nested ones included) is one step.
    void beginGroup() { if (groupDepth++ == 0) groupCurrent = ++groupCounter; }
    void endGroup()   { if (groupDepth > 0 && --groupDepth == 0) groupCurrent = 0; }
    /// No command is open (the user undoes; a document was closed).
    void closeGroups() { groupDepth = 0; groupCurrent = 0; }
    /// The next entry pushed joins the step of the entry before it.  Reset by the push.
    void joinNext(bool join = true) { joinNextPush = join; }

    // ── a new edit ──────────────────────────────────────────────────────────
    /** Put @p e on the undo side and end every redo.  The oldest steps are dropped
     *  whole while more than @p depth steps hold a labelling (at least one is kept),
     *  and the oldest entries while there are more than @p entryCap. */
    void push(Entry e, int depth, int entryCap);

    // ── walking it ──────────────────────────────────────────────────────────
    bool isEmpty(Direction d) const { return side(d).isEmpty(); }
    const Entry& newest(Direction d) const { return side(d).first(); }
    Entry take(Direction d) { return side(d).takeFirst(); }
    /// @p e becomes the newest entry of side @p d (an entry applied goes to the other side).
    void put(Direction d, const Entry& e) { side(d).prepend(e); }

    // ── counts ──────────────────────────────────────────────────────────────
    /// Steps Undo / Redo can take: a command counts as one.
    int steps(Direction d) const { return stepCount(side(d), false); }
    /// Child entries at the top of the side: what the forced child undo can reach.
    int childEntriesOnTop(Direction d) const;
    int entryCount(Direction d) const { return static_cast<int>(side(d).size()); }
    /// Undo entries that stand for a level of the parent Data history.
    int parentLevels() const;

    // ── the depth changed, the document closed ──────────────────────────────
    // ── realign entries ─────────────────────────────────
    /// Bytes kept by the entries of both sides.
    qint64 totalCost() const;
    /** Drop the entries that keep bytes from the oldest step that has any (the undo side's
     *  oldest first, then the redo side's): the step's other entries stay.  False: none. */
    bool dropOldestCostly();
    /// Drop every realign entry of both sides (their rows no longer describe the files).
    int removeRealignEntries();

    /// Keep the newest @p n undo entries (at least one) and end every redo.
    void limitEntries(int n);
    void clear();

private:
    QList<Entry>& side(Direction d) { return d == Undo ? undoEntries : redoEntries; }
    const QList<Entry>& side(Direction d) const { return d == Undo ? undoEntries : redoEntries; }
    static int stepCount(const QList<Entry>& l, bool labelStepsOnly);

    QList<Entry> undoEntries;        // newest first
    QList<Entry> redoEntries;
    int  groupDepth   = 0;           // open commands
    int  groupCurrent = 0;           // the group of the entries being pushed, 0 outside a command
    int  groupCounter = 0;
    bool joinNextPush = false;
};

#endif // UNDOHISTORY_H
