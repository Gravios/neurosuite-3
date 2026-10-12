// spkmemory.h — every .spk the open group uses, held in memory.
//
// Klusters keeps the waveform file of each feature space in memory so that
// waveform reads, switching spaces and syncing never wait on the disk.  One
// process-wide cache maps a file path to its bytes AS OF ONE FILE VERSION
// (size + modification time): a reader installed on a path gets the cached
// copy while the file is unchanged and a fresh read once it was rewritten
// (Save, re-extract, a sync, a pending re-seed).  That matches SpkReader's
// one-reader-per-file-version lifecycle: a snapshot pinning a reader keeps
// its bytes alive through the shared_ptr whatever the cache does later.
//
// In-place writers (realign / nudge rewrite rows of the pending .spk) do not
// change what an installed reader returns: their rows are staged in the
// SpkOverlay, read first; and when the overlay is cleared at Save the reader
// is reinstalled, which reloads the rewritten file.
#ifndef SPKMEMORY_H
#define SPKMEMORY_H

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <iterator>
#include <memory>

class SpkMemoryCache {
public:
    static SpkMemoryCache& instance();

    /**The bytes of @p path as the file is now: the cached copy when it is
    * still that file version, else read afresh (and cached).  nullptr when
    * the file is missing, unreadable or too large to hold.*/
    std::shared_ptr<const QByteArray> get(const QString& path);

    /**Drop every entry whose path is not in @p paths.*/
    void keepOnly(const QStringList& paths);

    /**Drop everything (document closed).*/
    void clear();

    /**Bytes currently held, for the status line.*/
    qint64 bytesHeld() const;

private:
    struct Entry {
        qint64    size = -1;
        QDateTime modified;
        std::shared_ptr<const QByteArray> bytes;
    };
    mutable QMutex mutex;
    QHash<QString, Entry> entries;
};

#endif // SPKMEMORY_H
