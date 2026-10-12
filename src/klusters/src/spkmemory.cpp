// spkmemory.cpp — see spkmemory.h.
#include "spkmemory.h"

#include <QFile>
#include <QFileInfo>

#include <cstring>
#include <new>

SpkMemoryCache& SpkMemoryCache::instance()
{
    static SpkMemoryCache cache;
    return cache;
}

std::shared_ptr<const QByteArray> SpkMemoryCache::get(const QString& path)
{
    if (path.isEmpty()) return nullptr;
    const QFileInfo info(path);
    if (!info.exists()) return nullptr;
    const qint64    size     = info.size();
    const QDateTime modified = info.lastModified();
    {
        QMutexLocker lk(&mutex);
        const auto it = entries.constFind(path);
        if (it != entries.constEnd() && it->size == size && it->modified == modified)
            return it->bytes;
    }

    // Read outside the lock: a large file must not block other lookups.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return nullptr;
    std::shared_ptr<QByteArray> bytes;
    try {
        bytes = std::make_shared<QByteArray>(f.readAll());
    } catch (const std::bad_alloc&) {
        return nullptr;                     // too large: the reader falls back to the file
    }
    if (bytes->size() != size) return nullptr;   // changed while reading: do not cache

    QMutexLocker lk(&mutex);
    Entry& e   = entries[path];
    e.size     = size;
    e.modified = modified;
    e.bytes    = std::move(bytes);
    return e.bytes;
}

void SpkMemoryCache::markWritten(const QString& path)
{
    QMutexLocker lk(&mutex);
    const auto it = entries.find(path);
    if (it != entries.end()) it->size = -1;      // never matches: re-read on next get()
}

void SpkMemoryCache::moved(const QString& from, const QString& to)
{
    QMutexLocker lk(&mutex);
    const auto it = entries.find(from);
    if (it == entries.end()) return;
    Entry e = it.value();
    entries.erase(it);
    const QFileInfo info(to);
    if (info.size() != e.size) return;          // not the same file after all
    e.modified = info.lastModified();
    entries.insert(to, e);
}

void SpkMemoryCache::patch(const QString& path, qint64 offset, const void* src, qint64 len)
{
    QMutexLocker lk(&mutex);
    const auto it = entries.find(path);
    if (it == entries.end()) return;
    Entry& e = it.value();
    if (!e.bytes || e.bytes.use_count() != 1 || offset < 0 || offset + len > e.bytes->size()) {
        e.size = -1;                              // shared or out of range: re-read later
        return;
    }
    // The cache created these bytes non-const (make_shared<QByteArray>); with
    // no other reference, writing through them is safe.
    QByteArray& bytes = const_cast<QByteArray&>(*e.bytes);
    std::memcpy(bytes.data() + offset, src, static_cast<size_t>(len));
    e.modified = QFileInfo(path).lastModified();
}

void SpkMemoryCache::keepOnly(const QStringList& paths)
{
    QMutexLocker lk(&mutex);
    for (auto it = entries.begin(); it != entries.end();)
        it = paths.contains(it.key()) ? std::next(it) : entries.erase(it);
}

void SpkMemoryCache::clear()
{
    QMutexLocker lk(&mutex);
    entries.clear();
}

qint64 SpkMemoryCache::bytesHeld() const
{
    QMutexLocker lk(&mutex);
    qint64 total = 0;
    for (const Entry& e : entries) total += e.size;
    return total;
}
