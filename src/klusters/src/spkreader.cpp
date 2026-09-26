// spkreader.cpp — see spkreader.h for the design rationale.
#include "spkreader.h"

#include <QFile>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#else
#include <QHash>
#include <cstdio>
#endif

SpkReader::~SpkReader()
{
    closeFd();
}

void SpkReader::setPath(const QString& path)
{
    QMutexLocker lk(&mutex);
    if (path == filePath) return;
    filePath = path;
    // close under the lock: openers re-check after acquiring it
    const int old = fdAtomic.exchange(-1);
#ifdef Q_OS_UNIX
    if (old != -1) ::close(old);
#else
    Q_UNUSED(old);
#endif
}

void SpkReader::invalidate()
{
    QMutexLocker lk(&mutex);
    const int old = fdAtomic.exchange(-1);
#ifdef Q_OS_UNIX
    if (old != -1) ::close(old);
#else
    Q_UNUSED(old);
#endif
}

void SpkReader::closeFd()
{
    invalidate();
}

bool SpkReader::ensureOpen()
{
    if (fdAtomic.load(std::memory_order_acquire) != -1) return true;
    QMutexLocker lk(&mutex);
    if (fdAtomic.load(std::memory_order_acquire) != -1) return true;   // raced
    if (filePath.isEmpty()) return false;
#ifdef Q_OS_UNIX
    const int fd = ::open(QFile::encodeName(filePath).constData(),
                          O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    fdAtomic.store(fd, std::memory_order_release);
    return true;
#else
    // Non-POSIX fallback: mark "open" and let read() serialise through the
    // mutex with a stdio handle per call.  Correct, slower, still one place.
    fdAtomic.store(0, std::memory_order_release);
    return true;
#endif
}

bool SpkReader::read(void* dst, qint64 bytes, qint64 offset)
{
    if (bytes <= 0 || offset < 0) return false;
    if (!ensureOpen()) return false;
#ifdef Q_OS_UNIX
    const int fd = fdAtomic.load(std::memory_order_acquire);
    if (fd == -1) return false;                    // invalidated under us
    char*  p    = static_cast<char*>(dst);
    qint64 left = bytes;
    off_t  off  = static_cast<off_t>(offset);
    while (left > 0) {
        const ssize_t n = ::pread(fd, p, static_cast<size_t>(left), off);
        if (n > 0) { p += n; left -= n; off += n; continue; }
        if (n < 0 && errno == EINTR) continue;
        return false;                              // EOF short or error
    }
    return true;
#else
    QMutexLocker lk(&mutex);
    FILE* f = std::fopen(QFile::encodeName(filePath).constData(), "rb");
    if (!f) return false;
    bool ok = std::fseek(f, static_cast<long>(offset), SEEK_SET) == 0
              && std::fread(dst, 1, static_cast<size_t>(bytes), f)
                     == static_cast<size_t>(bytes);
    std::fclose(f);
    return ok;
#endif
}
