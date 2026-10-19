// fileops.hpp — replacing, flushing and naming files the same way everywhere.
//
// Header-only (std + <io.h>/<process.h> on Windows, <unistd.h> elsewhere; no
// <windows.h>), so the Qt programs and the plugins can use it by #include alone.
//
//   replaceFile(from, to)  moves @p from onto @p to, REPLACING @p to if it
//                          exists.  std::rename() does that on POSIX but FAILS
//                          on Windows when the destination exists, which breaks
//                          every "write <file>.tmp, then rename it over
//                          <file>" save on a second run.  std::filesystem::
//                          rename replaces on both (MoveFileExW with
//                          MOVEFILE_REPLACE_EXISTING on Windows); on one POSIX
//                          file system it is atomic, as rename(2).
//   syncFile(f)            fflush + fsync (POSIX) / _commit (Windows): the data
//                          is on disk before a following replaceFile().
//   processId()            getpid() / _getpid(), e.g. for a unique temp name.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEUROSUITE_CORE_FILEOPS_HPP
#define NEUROSUITE_CORE_FILEOPS_HPP

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <io.h>        // _commit, _fileno
#include <process.h>   // _getpid
#else
#include <unistd.h>    // fsync, fileno, getpid
#endif

namespace neurosuite {
namespace core {

/** Moves @p from onto @p to, replacing an existing @p to.  Returns false and,
 *  with @p error, the reason when the move fails. */
inline bool replaceFile(const std::string& from, const std::string& to,
                        std::string* error = nullptr)
{
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(from), std::filesystem::path(to), ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

/** Flushes @p f's buffer and the OS cache to the disk.  False on failure. */
inline bool syncFile(std::FILE* f)
{
    if (std::fflush(f) != 0) return false;
#if defined(_WIN32)
    return ::_commit(::_fileno(f)) == 0;
#else
    return ::fsync(::fileno(f)) == 0;
#endif
}

/** The current process id. */
inline long processId()
{
#if defined(_WIN32)
    return static_cast<long>(::_getpid());
#else
    return static_cast<long>(::getpid());
#endif
}

}  // namespace core
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_FILEOPS_HPP
