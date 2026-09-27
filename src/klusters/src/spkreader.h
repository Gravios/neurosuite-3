// spkreader.h — shared positioned-read access to the .spk waveform file.
//
// One open descriptor per Data instance serves every concurrent reader:
// pread() is an atomic read-at-offset with no shared seek state, so any
// number of loader / matrix / strip threads (including OMP teams inside one
// thread) read through the same fd with zero locking on the hot path.  This
// replaces the fopen-per-consumer pattern, whose descriptor cost scaled with
// the number of live readers — one FILE* each — and, together with the
// per-thread event-dispatcher pipes, exhausted the process descriptor budget
// on heavily over-clustered sessions (worker-pool conversion, step 1).
//
// Lifecycle (epoch-snapshot step 2): one SpkReader per FILE VERSION, owned
// by shared_ptr — Data holds the current one and every published
// ClusteringSnapshot pins the reader of its epoch.  A shared reader is never
// setPath()'d or invalidate()'d: when the file behind a path is replaced
// (the pending-redirect at open, a re-extract's rename, a pending reseed),
// Data installs a FRESH reader and republishes, so jobs holding an older
// epoch keep reading the older inode through the held descriptor — the
// unix pin-by-fd guarantee — while new epochs open the new file.  prime()
// opens eagerly at install for exactly that reason: a lazily-opened reader
// would resolve its path to whatever inode is there LATER.  In-place
// writers (the realign nudge's r+b handle) still need nothing: pread sees
// the updated bytes through the same inode (their epoch story is plan
// step 7's overlay).
#ifndef SPKREADER_H
#define SPKREADER_H

#include <QMutex>
#include <QString>
#include <atomic>

class SpkReader {
public:
    SpkReader() = default;
    ~SpkReader();
    SpkReader(const SpkReader&) = delete;
    SpkReader& operator=(const SpkReader&) = delete;

    /**Sets the file this reader serves.  A changed path closes the current
    * descriptor; the next read reopens against the new file.*/
    void setPath(const QString& path);

    /**Closes the descriptor; the next read reopens the same path.  Kept for
    * a reader with a single owner; a SHARED reader (one a snapshot may pin)
    * is replaced wholesale instead — see the lifecycle note above.*/
    void invalidate();

    /**Opens the descriptor now (best effort; a missing file simply leaves
    * the reader failing its reads, as ever).  Called at install so the
    * reader is bound to the inode currently behind its path, not to
    * whatever replaces it later.*/
    void prime() { ensureOpen(); }

    /**Thread-safe positioned read: @p bytes at @p offset into @p dst.
    * Returns false on open failure (path unset, file missing) or short read;
    * @p dst is unspecified then.  Safe from any number of threads at once.*/
    bool read(void* dst, qint64 bytes, qint64 offset);

private:
    bool ensureOpen();
    void closeFd();

    QString          filePath;
    QMutex           mutex;          // guards open/close and the path; the
                                     // read path is lock-free once open
    std::atomic<int> fdAtomic{-1};
};

#endif // SPKREADER_H
