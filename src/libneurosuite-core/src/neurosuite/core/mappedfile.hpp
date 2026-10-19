// mappedfile.hpp — a whole file mapped read-only into memory, on every platform.
//
//   POSIX    open + mmap(PROT_READ, MAP_SHARED) + optional madvise
//   Windows  CreateFileA + CreateFileMappingA(PAGE_READONLY) + MapViewOfFile
//
// The processing programs map whole .dat / .fil recordings (often tens of GB)
// and read them in place.  mmap does not exist on Windows; the Windows file
// mapping gives the same read-only view, in binary (a mapping has no text mode).
//
//   neurosuite::core::MappedFile m;
//   if (!m.open(path, &err)) { ... err ... }
//   m.adviseSequential();                       // a hint; no-op on Windows
//   const auto* p = static_cast<const int16_t*>(m.data());   m.size() bytes
//
// An empty file opens successfully with size() 0 and data() nullptr (neither
// API can map zero bytes).  Not copyable; movable.  Unmapped on destruction.
//
// The implementation is in mappedfile.cpp (libneurosuite-core), so this header
// pulls in no system header: <windows.h> defines macros such as near, far,
// ERROR and DELETE that would break the code of every program including it.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEUROSUITE_CORE_MAPPEDFILE_HPP
#define NEUROSUITE_CORE_MAPPEDFILE_HPP

#include "neurosuite_core_export.h"

#include <cstdint>
#include <string>

namespace neurosuite {
namespace core {

class NEUROSUITE_CORE_EXPORT MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& o) noexcept;
    MappedFile& operator=(MappedFile&& o) noexcept;

    /** Maps @p path read-only.  On failure returns false and, with @p error,
     *  says why ("open: <reason>", "size: <reason>", "map: <reason>"). */
    bool open(const std::string& path, std::string* error = nullptr);

    /** Tells the OS the mapping will be read front to back (madvise
     *  MADV_SEQUENTIAL).  Only a hint: a no-op on Windows and on failure. */
    void adviseSequential() const;

    const void*  data() const { return view; }
    std::int64_t size() const { return bytes; }
    bool         isOpen() const { return file != nullptr; }

    void close();

private:
    bool fail(const char* what, std::string* error);
    void swap(MappedFile& o) noexcept;

    void*        view    = nullptr;
    std::int64_t bytes   = 0;
    void*        file    = nullptr;   ///< Windows HANDLE, or the POSIX fd + 1
    void*        mapping = nullptr;   ///< Windows mapping HANDLE (unused on POSIX)
};

}  // namespace core
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_MAPPEDFILE_HPP
