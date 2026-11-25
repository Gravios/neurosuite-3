/* hierarchyfiles.h -- the .clu/.clc/.clp triple on disk, without Qt.
 *
 * A hierarchical clustering is three files that share one anchor
 * (doc/klusters/hierarchical-clustering.md):
 *
 *   .clu  fiber (parent) id per spike     int32 count header + nSpikes x int32
 *   .clc  atom  (child)  id per spike     same layout
 *   .clp  fiber id per atom, atom c at    int32 header + nAtoms x int32
 *         entry c-1 (0 = unmapped)
 *
 * This module holds the parts of reading and writing them that do not need a
 * KlustersDoc, so the save path and the "Promote to Hierarchical Session"
 * action share one implementation and a test can drive it directly:
 *
 *   - scanNesting():   the atom -> fiber map the two per-spike files imply,
 *                      with the atoms that break the nesting invariant;
 *   - clpFromMap():    the .clp payload for such a map;
 *   - promoteFlat():   the .clc/.clp that turn a flat .clu into a hierarchy
 *                      in which every fiber is one atom, its self child;
 *   - selfChildOf() and its companions: the numbering of atoms (below);
 *   - upgradeChildIds(): a .clc in the numbering used before, brought to it;
 *   - stageIdFile() / installStaged(): a two-step write.  The new content goes
 *                      to "<path>.saving" first; installing renames it over the
 *                      target (after a ".bak" copy of the old file), so a crash
 *                      or a full disk never leaves a truncated .clc or .clp.
 */
#ifndef KLUSTERS_HIERARCHYFILES_H
#define KLUSTERS_HIERARCHYFILES_H

#include "types.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hierarchyfiles {

/* ── The numbering of atoms ────────────────────────────────────────────────
 *
 * Atom ids and fiber ids are DISJOINT, so that an id names one thing whichever
 * layer a view, a palette or a log line is looking at:
 *
 *     0, 1                       the artifact and noise bins: the one atom of
 *                                each bin carries the bin's own id
 *     2 .. 9999                  fibers (parents) only
 *     10002 .. 19999             SELF CHILDREN: the self child of fiber p is
 *                                p + 10000 (fiber 153 -> atom 10153).  It is
 *                                the atom a fiber has when it has only one,
 *                                and the atom that takes a fiber's spikes no
 *                                other atom covers.
 *     20000 ..                   every other atom, numbered upwards as made
 *
 * A reserved chunk cluster (isReservedClusterId, at the top of the id range)
 * keeps the old identity: its one atom carries its own id.
 *
 * Before 2026-11-21 an atom took its fiber's id when it was the only one, and
 * the two numberings overlapped everywhere else too (atom 7 and fiber 7 were
 * unrelated).  upgradeChildIds() reads such a file. */
constexpr int kSelfChildBase = 10000;   ///< self child of fiber p (2 <= p < 10000): p + this
constexpr int kFreeChildBase = 20000;   ///< the lowest id of an atom that is not a self child

/** True for the fibers whose self child is p + kSelfChildBase. */
constexpr bool hasOffsetSelfChild(long long fiber)
{
    return fiber >= 2 && fiber < kSelfChildBase;
}

/** The id of fiber @p fiber's self child.  The bins (0, 1) and a reserved
 *  chunk cluster are their own (as is any id fiberIdFits() rejects). */
constexpr long long selfChildOf(long long fiber)
{
    return hasOffsetSelfChild(fiber) ? fiber + kSelfChildBase : fiber;
}

/** True when @p atom lies in the block of ids kept for self children. */
constexpr bool isSelfChildId(long long atom)
{
    return atom >= kSelfChildBase + 2 && atom < kFreeChildBase;
}

/** The fiber @p atom is the self child of, going by its id alone: atom - 10000
 *  in the self-child block, the atom itself for a bin or a reserved id, and -1
 *  for any other atom (it is no fiber's self child). */
constexpr long long fiberOfSelfChild(long long atom)
{
    if (isSelfChildId(atom)) return atom - kSelfChildBase;
    if (atom == 0 || atom == 1 || isReservedClusterId(static_cast<dataType>(atom))) return atom;
    return -1;
}

/** True when the numbering has room for @p fiber: a bin, 2 .. 9999, or a
 *  reserved chunk cluster.  A session with any other fiber id cannot carry an
 *  atom layer. */
constexpr bool fiberIdFits(long long fiber)
{
    return fiber >= 0 && (fiber < kSelfChildBase || isReservedClusterId(static_cast<dataType>(fiber)));
}

/** A per-spike atom array brought from the old numbering to this one. */
struct ChildIdUpgrade {
    bool               needed = false;   ///< false: @p child was in this numbering already (ids is empty)
    std::vector<int>   ids;              ///< the new atom id per spike
    std::map<int, int> oldToNew;         ///< every old atom id that changed
};

/** @p child is in the old numbering when any of its ids lies in 2 .. 9999.
 *  Then every atom outside the bins and the reserved chunk ids is renumbered:
 *  an atom that carries the id of the fiber of its first spike becomes that
 *  fiber's self child, and the others take 20000, 20001, ... in ascending
 *  order of their old id.  Spikes keep their grouping; only names change. */
ChildIdUpgrade upgradeChildIds(const std::vector<int>& parent, const std::vector<int>& child);

/** The atom -> fiber map implied by aligned per-spike fiber and atom ids. */
struct NestingScan {
    std::map<int, int> childToParent;   ///< first-seen fiber of every atom id seen
    std::vector<int>   straddlers;      ///< atoms seen under more than one fiber, ascending
    bool               ok = false;      ///< false when the two arrays differ in length
};

/** Scans @p parent (fiber id per spike) and @p child (atom id per spike).
 *  Every atom id, 0 included, gets the fiber of its first spike; an atom
 *  whose spikes carry more than one fiber id is listed in straddlers. */
NestingScan scanNesting(const std::vector<int>& parent, const std::vector<int>& child);

/** The .clp payload for @p childToParent: entry c-1 holds the fiber of atom c,
 *  for c = 1 .. the highest atom id; ids without an entry, and atom 0, are
 *  not mapped (0). */
std::vector<int32_t> clpFromMap(const std::map<int, int>& childToParent);

/** Number of distinct ids in @p ids (the count header of a .clu or .clc). */
int distinctCount(const std::vector<int>& ids);

/** The .clc and .clp that make a flat .clu hierarchical: each fiber becomes
 *  one atom, its self child (selfChildOf: fiber 153 -> atom 10153; the bins
 *  keep 0 and 1) -- the state "Flatten Hierarchy" leaves behind -- and every
 *  later split works on real atoms. */
struct Promotion {
    std::vector<int>     clc;        ///< atom id per spike (the self child of the spike's fiber)
    int                  clcCount = 0;
    std::vector<int32_t> clp;        ///< fiber id per atom (0 for unused ids)
};
Promotion promoteFlat(const std::vector<int>& clu);

/** The sibling of a .clu path for another type: the last ".clu." in the file
 *  name becomes ".<type>." (rat.clu.stderiv.3.mf -> rat.clc.stderiv.3.mf).
 *  Empty when the file name has no ".clu.". */
std::string siblingPath(const std::string& cluPath, const std::string& type);

/** The scratch name a staged write goes to: @p path + ".saving". */
std::string stagedPath(const std::string& path);

/** Writes an int32 header and @p ids to stagedPath(@p path).  Returns false
 *  (with @p error) if the file cannot be written completely; a partial scratch
 *  file is removed. */
bool stageIdFile(const std::string& path, int32_t header, const std::vector<int32_t>& ids,
                 std::string* error);

/** Moves stagedPath(@p path) over @p path.  When @p path exists and
 *  @p keepBackup is set, its previous content is copied to @p path + ".bak"
 *  first.  The rename replaces the target in one step, and the target keeps
 *  its previous permission bits. */
bool installStaged(const std::string& path, bool keepBackup, std::string* error);

/** Removes a staged file left by a write that is not going to be installed. */
void discardStaged(const std::string& path);

/** Writes @p ids to @p path through stageIdFile + installStaged. */
bool writeIdFile(const std::string& path, int32_t header, const std::vector<int32_t>& ids,
                 bool keepBackup, std::string* error);

/** Reads a whole id file of unknown length (a .clp): the int32 header, then
 *  every int32 that follows.  ok is false if the file is missing, shorter than
 *  the header, or not a whole number of int32s. */
struct IdFile {
    int32_t              header = 0;
    std::vector<int32_t> ids;
    bool                 ok = false;
};
IdFile readIdFile(const std::string& path);

} // namespace hierarchyfiles

#endif // KLUSTERS_HIERARCHYFILES_H
