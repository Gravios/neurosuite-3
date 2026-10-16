/* filecommit.h -- an all-or-nothing commit of a set of files, and the cheap
 * test that a file is already at parity with its source.  No Qt.
 *
 * A Klusters save replaces several files that only make sense together: the
 * .clu, its .clc/.clp, the .spk/.res/.fet the session realigned, other feature
 * spaces synced to the new spike times, and the parameter file's unit table.
 * Replacing them one by one leaves a mixed set behind whenever one replacement
 * fails or the process dies part way.
 *
 * The commit has two phases:
 *
 *   1. Staging -- every new file is written completely to "<target>.saving"
 *      next to its target and flushed to disk.  This is where the time goes
 *      and where most failures happen (a full disk, a permission); a failure
 *      here discards the staged files and no target has been touched.
 *   2. Install -- a journal listing the targets is written and flushed, then
 *      each target is moved aside to "<target>.rollback" and its staged file
 *      renamed into place.  Renames within one directory do not run out of
 *      space; if one fails anyway, the targets already installed are put back
 *      from their .rollback files, so the set is again the old one.  On
 *      success the .rollback files and the journal are removed.
 *
 * If the process dies during phase 2, the journal is still there: every staged
 * file it names was complete before it was written, so recoverCommit() rolls
 * the commit FORWARD on the next open -- the set becomes the new one.
 *
 * Parity: a save should not rewrite a 300 MB .spk that is byte-identical to its
 * pending copy.  A full checksum would read both files on every save, which
 * costs about as much as the copy.  Instead a SyncStamp is recorded whenever
 * the two are known identical (after a seed or a commit): size, mtime, ctime
 * and inode of each.  If neither file's stamp changed since, they are still
 * identical and nothing is read.  A stamp is not trusted for the working copy
 * when its recorded mtime is within the filesystem's timestamp granularity of
 * the moment it was taken ("racy", as git calls it): a write in that same tick
 * would not move the mtime.  Then, and whenever a stamp changed but the sizes
 * agree, the files are compared byte by byte, stopping at the first
 * difference, and a match refreshes the stamp.
 */
#ifndef KLUSTERS_FILECOMMIT_H
#define KLUSTERS_FILECOMMIT_H

#include <cstdint>
#include <string>
#include <vector>

namespace filecommit {

/** What stat() says about a file; ok is false when it does not exist. */
struct FileStamp {
    bool          ok = false;
    std::uint64_t size = 0, inode = 0, device = 0;
    std::int64_t  mtimeNs = 0, ctimeNs = 0;
    bool operator==(const FileStamp& o) const {
        return ok == o.ok && size == o.size && inode == o.inode && device == o.device
            && mtimeNs == o.mtimeNs && ctimeNs == o.ctimeNs;
    }
    bool operator!=(const FileStamp& o) const { return !(*this == o); }
};
FileStamp stampOf(const std::string& path);

/** Stamps of a working copy and the file it is committed to, taken when the
 *  two were known to be identical. */
struct SyncStamp {
    bool         valid = false;
    FileStamp    work, target;
    std::int64_t takenNs = 0;     ///< wall clock when recorded
};
SyncStamp syncStampOf(const std::string& work, const std::string& target);

/** Records that @p work and @p target are identical right now (the caller just
 *  copied one onto the other).  The working copy's mtime is first set ten
 *  seconds back, so the stamp is not racy: any later write moves it to "now"
 *  and the stamp no longer matches.  Nothing else reads the working copy's
 *  mtime as a version (it is Klusters' own scratch file). */
SyncStamp markInSync(const std::string& work, const std::string& target);

/** True when @p work and @p target hold the same bytes.  Uses @p stamp when it
 *  proves it without reading (see the header comment), else compares the
 *  files; on a proven match @p stamp is refreshed.  A missing file differs. */
bool atParity(const std::string& work, const std::string& target, SyncStamp& stamp);

/** Byte comparison of two files, stopping at the first difference. */
bool sameContent(const std::string& a, const std::string& b);

/** True when @p path holds exactly an int32 @p header followed by @p ids. */
bool idFileEquals(const std::string& path, int32_t header, const std::vector<int32_t>& ids);

/** The staged name of @p target: target + ".saving". */
std::string staged(const std::string& target);

/** Copies @p source to staged(@p target), flushed to disk, size checked. */
bool stageCopy(const std::string& source, const std::string& target, std::string* error);

/** Flushes an already written staged file to disk. */
bool flushFile(const std::string& path, std::string* error);

/** Phase 2 for @p targets (each has a complete staged file): journal, move
 *  aside, install, clean up.  On failure every target is as it was, the staged
 *  files are removed, and @p error says which step failed.  For the targets in
 *  @p keepBackup the previous file is kept as "<target>.bak" instead of being
 *  removed. */
bool commitStaged(const std::vector<std::string>& targets, const std::string& journal,
                  std::string* error, const std::vector<std::string>& keepBackup = {});

/** Removes the staged files of @p targets (an abandoned commit). */
void discardStaged(const std::vector<std::string>& targets);

/** Finishes a commit interrupted in phase 2, if @p journal exists: installs
 *  every staged file it names, then removes the .rollback files and the
 *  journal.  Returns false only when a journal was found and could not be
 *  completed; @p report lists what was done. */
bool recoverCommit(const std::string& journal, std::string* report);

} // namespace filecommit

#endif // KLUSTERS_FILECOMMIT_H
