/* filecommit.cpp -- see filecommit.h. */
#include "filecommit.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace filecommit {

namespace fs = std::filesystem;

namespace {
// Coarser than any timestamp granularity a session directory is likely on
// (ext4/xfs: ns but updated at the kernel tick; FAT: 2 s; NTFS via fuse: 100 ns).
constexpr std::int64_t kRacyNs = 2'000'000'000LL;

std::int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string errnoText(const std::string& what, const std::string& path)
{
    return what + " " + path + ": " + std::strerror(errno);
}

#ifdef _WIN32
// A Win32 failure sets GetLastError(), not errno; errnoText() reads errno.
void setErrnoFromWin32()
{
    const DWORD e = ::GetLastError();
    errno = (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? ENOENT
          : (e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION) ? EACCES
          : EIO;
}

// FILETIME (100 ns since 1601-01-01) to ns since the Unix epoch, the scale
// nowNs() and the POSIX stamps use.
constexpr LONGLONG kFileTimeEpoch = 116444736000000000LL;   // 1601 -> 1970, in 100 ns

std::int64_t fileTimeNs(LONGLONG t)
{
    return static_cast<std::int64_t>(t - kFileTimeEpoch) * 100;
}
#endif

bool fsyncPath(const std::string& path, bool directory)
{
#ifdef _WIN32
    // Windows cannot flush a directory handle; NTFS journals the rename itself,
    // which is what the POSIX directory fsync is for.
    if (directory) return true;
    const HANDLE h = ::CreateFileW(fs::path(path).c_str(), GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { setErrnoFromWin32(); return false; }
    const bool ok = ::FlushFileBuffers(h) != 0;
    if (!ok) setErrnoFromWin32();
    ::CloseHandle(h);
    return ok;
#else
    const int fd = ::open(path.c_str(), directory ? (O_RDONLY | O_DIRECTORY) : O_RDONLY);
    if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0 || (directory && (errno == EINVAL || errno == EBADF));
    ::close(fd);
    return ok;
#endif
}

std::string parentDir(const std::string& path)
{
    const fs::path p(path);
    return p.has_parent_path() ? p.parent_path().string() : std::string(".");
}

std::string rollbackOf(const std::string& target) { return target + ".rollback"; }
} // namespace

FileStamp stampOf(const std::string& path)
{
    FileStamp s;
#ifdef _WIN32
    // The same identity as POSIX: the file index stands in for the inode, the
    // volume serial for the device, ChangeTime for ctime.
    const HANDLE h = ::CreateFileW(fs::path(path).c_str(), FILE_READ_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return s;
    BY_HANDLE_FILE_INFORMATION info{};
    FILE_BASIC_INFO basic{};
    const bool ok = ::GetFileInformationByHandle(h, &info) != 0
                 && ::GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof basic) != 0;
    ::CloseHandle(h);
    if (!ok) return s;
    s.ok      = true;
    s.size    = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    s.inode   = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
    s.device  = info.dwVolumeSerialNumber;
    s.mtimeNs = fileTimeNs(basic.LastWriteTime.QuadPart);
    s.ctimeNs = fileTimeNs(basic.ChangeTime.QuadPart);
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return s;
    s.ok      = true;
    s.size    = static_cast<std::uint64_t>(st.st_size);
    s.inode   = static_cast<std::uint64_t>(st.st_ino);
    s.device  = static_cast<std::uint64_t>(st.st_dev);
    s.mtimeNs = static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1'000'000'000LL + st.st_mtim.tv_nsec;
    s.ctimeNs = static_cast<std::int64_t>(st.st_ctim.tv_sec) * 1'000'000'000LL + st.st_ctim.tv_nsec;
#endif
    return s;
}

SyncStamp syncStampOf(const std::string& work, const std::string& target)
{
    SyncStamp s;
    s.work    = stampOf(work);
    s.target  = stampOf(target);
    s.takenNs = nowNs();
    s.valid   = s.work.ok && s.target.ok && s.work.size == s.target.size;
    return s;
}

SyncStamp markInSync(const std::string& work, const std::string& target)
{
    const std::int64_t back = nowNs() - 10'000'000'000LL;
#ifdef _WIN32
    const HANDLE h = ::CreateFileW(fs::path(work).c_str(), FILE_WRITE_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        ULARGE_INTEGER t;
        t.QuadPart = static_cast<ULONGLONG>(back / 100 + kFileTimeEpoch);
        FILETIME mtime;
        mtime.dwLowDateTime  = t.LowPart;
        mtime.dwHighDateTime = t.HighPart;
        ::SetFileTime(h, nullptr, nullptr, &mtime);             // atime unchanged
        ::CloseHandle(h);
    }
#else
    struct timespec ts[2];
    ts[0].tv_sec = 0; ts[0].tv_nsec = UTIME_OMIT;                 // atime unchanged
    ts[1].tv_sec = back / 1'000'000'000LL; ts[1].tv_nsec = back % 1'000'000'000LL;
    ::utimensat(AT_FDCWD, work.c_str(), ts, 0);
#endif
    return syncStampOf(work, target);
}

bool sameContent(const std::string& a, const std::string& b)
{
    std::error_code ec;
    const auto sa = fs::file_size(a, ec);
    if (ec) return false;
    const auto sb = fs::file_size(b, ec);
    if (ec || sa != sb) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb) return false;
    std::vector<char> ba(1 << 20), bb(1 << 20);
    while (fa && fb) {
        fa.read(ba.data(), static_cast<std::streamsize>(ba.size()));
        fb.read(bb.data(), static_cast<std::streamsize>(bb.size()));
        const std::streamsize na = fa.gcount(), nb = fb.gcount();
        if (na != nb) return false;
        if (na == 0) break;
        if (std::memcmp(ba.data(), bb.data(), static_cast<std::size_t>(na)) != 0) return false;
    }
    return true;
}

bool atParity(const std::string& work, const std::string& target, SyncStamp& stamp)
{
    const FileStamp w = stampOf(work), t = stampOf(target);
    if (!w.ok || !t.ok || w.size != t.size) return false;
    if (stamp.valid && w == stamp.work && t == stamp.target
        && stamp.work.mtimeNs < stamp.takenNs - kRacyNs)
        return true;                                   // nothing touched either file since
    if (!sameContent(work, target)) return false;
    stamp = syncStampOf(work, target);                 // proven: trust the stamp next time
    return true;
}

bool idFileEquals(const std::string& path, int32_t header, const std::vector<int32_t>& ids)
{
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size != 4 * (ids.size() + 1)) return false;
    std::ifstream in(path, std::ios::binary);
    int32_t h = 0;
    if (!in.read(reinterpret_cast<char*>(&h), 4) || h != header) return false;
    std::vector<int32_t> buf(1 << 18);
    std::size_t done = 0;
    while (done < ids.size()) {
        const std::size_t n = std::min(buf.size(), ids.size() - done);
        if (!in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(n * 4)))
            return false;
        if (std::memcmp(buf.data(), ids.data() + done, n * 4) != 0) return false;
        done += n;
    }
    return true;
}

std::string staged(const std::string& target) { return target + ".saving"; }

bool flushFile(const std::string& path, std::string* error)
{
    if (fsyncPath(path, false)) return true;
    if (error) *error = errnoText("cannot flush", path);
    return false;
}

bool stageCopy(const std::string& source, const std::string& target, std::string* error)
{
    const std::string tmp = staged(target);
    std::error_code ec;
    fs::remove(tmp, ec);
    fs::copy_file(source, tmp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (error) *error = "cannot copy " + source + " to " + tmp + ": " + ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    std::error_code e1, e2;
    if (fs::file_size(tmp, e1) != fs::file_size(source, e2) || e1 || e2) {
        if (error) *error = "incomplete copy of " + source + " to " + tmp;
        fs::remove(tmp, ec);
        return false;
    }
    if (!flushFile(tmp, error)) { fs::remove(tmp, ec); return false; }
    return true;
}

void discardStaged(const std::vector<std::string>& targets)
{
    std::error_code ec;
    for (const auto& t : targets) fs::remove(staged(t), ec);
}

namespace {
// Moves @p target aside (when it exists) and installs its staged file.
bool installOne(const std::string& target, std::string* error)
{
    std::error_code ec;
    const bool had = fs::exists(target, ec);
    if (had) {
        // Keep the old file's permission bits on the new one.
        std::error_code pe;
        const auto perms = fs::status(target, pe).permissions();
        if (!pe) fs::permissions(staged(target), perms, fs::perm_options::replace, pe);
        fs::rename(target, rollbackOf(target), ec);
        if (ec) { if (error) *error = "cannot move aside " + target + ": " + ec.message(); return false; }
    }
    fs::rename(staged(target), target, ec);
    if (ec) {
        if (error) *error = "cannot install " + target + ": " + ec.message();
        std::error_code back;
        if (had) fs::rename(rollbackOf(target), target, back);
        return false;
    }
    return true;
}

// Undoes @p target's install, if it happened.  Its staged file is consumed by
// the install, so a target whose staged file still exists was never installed
// (or its failed install already put it back) and is left alone.  Otherwise
// the previous version comes back from .rollback, or, when there was none, the
// new file is removed.
bool restoreOne(const std::string& target, std::string* out)
{
    std::error_code ec;
    if (fs::exists(staged(target), ec)) return true;
    if (fs::exists(rollbackOf(target), ec)) {
        fs::rename(rollbackOf(target), target, ec);
        if (ec) { if (out) *out += "could not restore " + target + ": " + ec.message() + "\n"; return false; }
        if (out) *out += "restored " + target + "\n";
        return true;
    }
    fs::remove(target, ec);
    if (out) *out += "removed new " + target + "\n";
    return true;
}

bool writeJournal(const std::string& path, const std::vector<std::string>& targets)
{
    std::ofstream j(path, std::ios::trunc);
    j << "klusters-commit 2\n";
    for (const auto& t : targets) j << t << '\n';
    j << "end " << targets.size() << '\n';          // a journal without this line was never complete
    j.close();
    if (!j || !fsyncPath(path, false)) return false;
    fsyncPath(parentDir(path), true);
    return true;
}

// Marks the journals: the commit is being undone, so recovery must roll back.
void markUndo(const std::vector<std::string>& journals)
{
    for (const auto& path : journals) {
        std::ofstream j(path, std::ios::app);
        j << "undo\n";
        j.close();
        fsyncPath(path, false);
    }
}
} // namespace

bool commitStaged(const std::vector<std::string>& targets, const std::vector<std::string>& journals,
                  std::string* error, const std::vector<std::string>& keepBackup)
{
    std::error_code ec;
    for (const auto& t : targets)
        if (!fs::exists(staged(t), ec)) {
            if (error) *error = "nothing staged for " + t;
            discardStaged(targets);
            return false;
        }
    if (targets.empty()) return true;

    // Before the first rename: every journal names every target and ends with
    // its count, flushed, so a crash from here on is rolled forward by
    // recoverCommit() from whichever journal the next open finds.
    for (const auto& path : journals)
        if (!writeJournal(path, targets)) {
            if (error) *error = "cannot write the commit journal " + path;
            for (const auto& p : journals) fs::remove(p, ec);
            discardStaged(targets);
            return false;
        }

    std::size_t done = 0;
    std::string err;
    for (; done < targets.size(); ++done)
        if (!installOne(targets[done], &err)) break;

    if (done < targets.size()) {
        // Undo the ones already installed: the old set stands.  The journals say
        // so first, so a crash during the undo is finished as an undo.
        markUndo(journals);
        std::string undoReport;
        bool undone = true;
        for (std::size_t k = 0; k < done; ++k) undone = restoreOne(targets[k], &undoReport) && undone;
        if (!undone) {
            // Leave the journals and .rollback files for recoverCommit to retry.
            if (error) *error = err + "\nand the files already replaced could not all be put back:\n"
                              + undoReport + "The journal " + journals.front() + " was kept; reopen to retry.";
            return false;
        }
        discardStaged(targets);
        for (const auto& p : journals) fs::remove(p, ec);
        if (error) *error = err;
        return false;
    }
    for (const auto& t : targets) {
        fsyncPath(parentDir(t), true);
        const bool keep = std::find(keepBackup.begin(), keepBackup.end(), t) != keepBackup.end();
        if (keep && fs::exists(rollbackOf(t), ec)) fs::rename(rollbackOf(t), t + ".bak", ec);
        else fs::remove(rollbackOf(t), ec);
    }
    for (const auto& p : journals) fs::remove(p, ec);
    return true;
}

bool commitStaged(const std::vector<std::string>& targets, const std::string& journal,
                  std::string* error, const std::vector<std::string>& keepBackup)
{
    return commitStaged(targets, std::vector<std::string>{journal}, error, keepBackup);
}

bool recoverCommit(const std::string& journal, std::string* report)
{
    std::error_code ec;
    if (!fs::exists(journal, ec)) return true;
    std::ifstream j(journal);
    std::string line;
    if (!std::getline(j, line) || line.rfind("klusters-commit", 0) != 0) {
        if (report) *report = "unreadable commit journal " + journal + " left in place";
        return false;
    }
    std::vector<std::string> targets;
    bool complete = false, undo = false;
    while (std::getline(j, line)) {
        if (line.empty()) continue;
        if (line.rfind("end ", 0) == 0) {
            complete = std::to_string(targets.size()) == line.substr(4);
            continue;
        }
        if (line == "undo") { undo = true; continue; }
        if (!complete) targets.push_back(line);
    }
    j.close();

    std::string out;
    bool ok = true;
    if (!complete) {
        // The journal was still being written: no file had been replaced yet.
        discardStaged(targets);
        out += "the save had not started replacing files; its new versions were discarded\n";
    } else if (undo) {
        // A failed commit was being undone: finish the undo.
        for (const auto& t : targets) ok = restoreOne(t, &out) && ok;
        if (ok) discardStaged(targets);
    } else {
        for (const auto& t : targets) {
            if (fs::exists(staged(t), ec)) {
                // Not installed yet.  Move the old file aside unless that already
                // happened, then install.
                if (fs::exists(t, ec) && !fs::exists(rollbackOf(t), ec))
                    fs::rename(t, rollbackOf(t), ec);
                fs::rename(staged(t), t, ec);
                if (ec) { ok = false; out += "could not install " + t + ": " + ec.message() + "\n"; }
                else out += "installed " + t + "\n";
            }
        }
    }
    if (ok) {
        for (const auto& t : targets) fs::remove(rollbackOf(t), ec);
        fs::remove(journal, ec);
    }
    if (report) *report = out;
    return ok;
}

} // namespace filecommit
