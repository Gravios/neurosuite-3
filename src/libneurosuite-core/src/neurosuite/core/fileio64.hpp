// fileio64.hpp — 64-bit positioning on a C stdio FILE*, on every platform.
//
// Header-only and dependency-free (std + the platform's stdio), so the Qt
// programs and the plugins can use it by #include alone.
//
// Why: fseek / ftell take and return `long`, which is 32 bits on Windows, so a
// .fil / .spk / .res past 2 GiB is positioned wrongly there.  The POSIX
// fseeko / ftello take `off_t`, which MSVC does not provide in that form
// (its <sys/types.h> off_t is a 32-bit long, and there is no fseeko at all);
// glibc's fseeko64 is Linux-only.  Offsets here are std::int64_t everywhere:
//
//   POSIX    fseeko / ftello            (off_t is 64-bit on LP64 Linux/macOS)
//   Windows  _fseeki64 / _ftelli64      (MSVC CRT, also clang-cl / icx)
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEUROSUITE_CORE_FILEIO64_HPP
#define NEUROSUITE_CORE_FILEIO64_HPP

#include <cstdint>
#include <cstdio>

#if !defined(_WIN32)
#include <sys/types.h>   // off_t
#endif

namespace neurosuite {
namespace core {

#if !defined(_WIN32)
static_assert(sizeof(off_t) == 8,
              "fileio64.hpp: off_t must be 64-bit (define _FILE_OFFSET_BITS=64 on 32-bit targets)");
#endif

/** fseek with a 64-bit offset; @p whence is SEEK_SET / SEEK_CUR / SEEK_END.
 *  Returns 0 on success, as fseek does. */
inline int fseek64(std::FILE* f, std::int64_t offset, int whence)
{
#if defined(_WIN32)
    return ::_fseeki64(f, offset, whence);
#else
    return ::fseeko(f, static_cast<off_t>(offset), whence);
#endif
}

/** ftell with a 64-bit result; -1 on error, as ftell. */
inline std::int64_t ftell64(std::FILE* f)
{
#if defined(_WIN32)
    return static_cast<std::int64_t>(::_ftelli64(f));
#else
    return static_cast<std::int64_t>(::ftello(f));
#endif
}

}  // namespace core
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_FILEIO64_HPP
