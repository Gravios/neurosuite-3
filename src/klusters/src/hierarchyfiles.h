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
 *                      in which every fiber is one atom (atom id = fiber id);
 *   - stageIdFile() / installStaged(): a two-step write.  The new content goes
 *                      to "<path>.saving" first; installing renames it over the
 *                      target (after a ".bak" copy of the old file), so a crash
 *                      or a full disk never leaves a truncated .clc or .clp.
 */
#ifndef KLUSTERS_HIERARCHYFILES_H
#define KLUSTERS_HIERARCHYFILES_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hierarchyfiles {

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
 *  one atom with the same id (fiber 0, the artefact bin, gets atom 0), so the
 *  atom layer starts as a copy of the fiber layer -- the state "Flatten
 *  Hierarchy" leaves behind -- and every later split works on real atoms. */
struct Promotion {
    std::vector<int>     clc;        ///< atom id per spike (equal to the .clu ids)
    int                  clcCount = 0;
    std::vector<int32_t> clp;        ///< fiber id per atom (identity; 0 for unused ids)
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
 *  first.  The rename replaces the target in one step. */
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
