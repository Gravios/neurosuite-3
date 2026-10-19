// mappedfile.cpp — see mappedfile.hpp.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mappedfile.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace neurosuite {
namespace core {

namespace {
#if !defined(_WIN32)
// The POSIX fd is kept in the opaque `file` slot as fd + 1, so 0 (nullptr) is
// "not open" on both platforms.
void* fdToSlot(int fd) { return reinterpret_cast<void*>(static_cast<std::intptr_t>(fd) + 1); }
int   slotToFd(void* s) { return static_cast<int>(reinterpret_cast<std::intptr_t>(s) - 1); }
#endif
}  // namespace

MappedFile::~MappedFile() { close(); }

MappedFile::MappedFile(MappedFile&& o) noexcept { swap(o); }

MappedFile& MappedFile::operator=(MappedFile&& o) noexcept
{
    if (this != &o) { close(); swap(o); }
    return *this;
}

bool MappedFile::open(const std::string& path, std::string* error)
{
    close();
#if defined(_WIN32)
    HANDLE h = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return fail("open", error);
    file = h;
    LARGE_INTEGER sz{};
    if (!::GetFileSizeEx(h, &sz)) return fail("size", error);
    bytes = static_cast<std::int64_t>(sz.QuadPart);
    if (bytes == 0) return true;
    mapping = ::CreateFileMappingA(h, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) return fail("map", error);
    view = ::MapViewOfFile(static_cast<HANDLE>(mapping), FILE_MAP_READ, 0, 0, 0);
    if (!view) return fail("map", error);
    return true;
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return fail("open", error);
    file = fdToSlot(fd);
    struct stat st{};
    if (::fstat(fd, &st) != 0) return fail("size", error);
    bytes = static_cast<std::int64_t>(st.st_size);
    if (bytes == 0) return true;
    void* p = ::mmap(nullptr, static_cast<size_t>(bytes), PROT_READ, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) return fail("map", error);
    view = p;
    return true;
#endif
}

void MappedFile::adviseSequential() const
{
#if !defined(_WIN32)
    if (view) ::madvise(view, static_cast<size_t>(bytes), MADV_SEQUENTIAL);
#endif
}

void MappedFile::close()
{
#if defined(_WIN32)
    if (view)    ::UnmapViewOfFile(view);
    if (mapping) ::CloseHandle(static_cast<HANDLE>(mapping));
    if (file)    ::CloseHandle(static_cast<HANDLE>(file));
#else
    if (view) ::munmap(view, static_cast<size_t>(bytes));
    if (file) ::close(slotToFd(file));
#endif
    view = nullptr;
    mapping = nullptr;
    file = nullptr;
    bytes = 0;
}

bool MappedFile::fail(const char* what, std::string* error)
{
#if defined(_WIN32)
    const DWORD e = ::GetLastError();
    char msg[256] = {0};
    ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e,
                     0, msg, sizeof msg, nullptr);
    std::string reason(msg);
    while (!reason.empty() && (reason.back() == '\n' || reason.back() == '\r' || reason.back() == ' '))
        reason.pop_back();
#else
    const std::string reason = std::strerror(errno);
#endif
    if (error) *error = std::string(what) + ": " + reason;
    close();
    return false;
}

void MappedFile::swap(MappedFile& o) noexcept
{
    std::swap(view, o.view);
    std::swap(bytes, o.bytes);
    std::swap(file, o.file);
    std::swap(mapping, o.mapping);
}

}  // namespace core
}  // namespace neurosuite
