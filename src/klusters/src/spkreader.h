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
// Lifecycle: setPath() at Data::initialize (and after any swap that REPLACES
// the file — a renewed pending .spk is a new inode, and a held fd would keep
// reading the old one; call invalidate() there).  The fd opens lazily on the
// first read and closes on setPath / invalidate / destruction.  In-place
// writers (the realign nudge's r+b handle) need nothing: pread sees the
// updated bytes through the same inode.
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

    /**Closes the descriptor; the next read reopens the same path.  For
    * writers that replace the file behind the path (new inode).*/
    void invalidate();

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
