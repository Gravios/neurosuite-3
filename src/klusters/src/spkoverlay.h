// spkoverlay.h — the in-memory overlay of rewritten .spk records
// (epoch-snapshot step 7).
//
// The byte writers — realignSpikes and the nudge — rewrite spike records of
// the pending .spk in place, through the one inode every epoch's pinned
// descriptor shares, so the file alone cannot give different epochs
// different bytes.  The overlay can: each batch of rewritten records is
// ALSO staged here, each epoch's snapshot pins the overlay generation
// current at its publication, and reads go overlay-first
// (ClusteringSnapshot::readSpk).  A snapshot published before a batch never
// sees that batch's records through the overlay; the pending FILE remains
// the journal the writers keep for Save (commitAndRenewPending) and for
// their own readbacks, exactly as before.
//
// Generations are immutable once published, like everything a snapshot
// carries: a batch builds a NEW SpkOverlay whose hash copies the previous
// generation's entries (shallow — the records are shared_ptr'd) plus its
// own, and Data publishes it (applySpkOverlayBatch).  Data keeps the
// superseded generations on a history stack so rejecting an uncommitted
// batch restores the exact previous generation (revertLastSpkOverlayBatch)
// — which, unlike the historical reseed-and-forget, keeps EARLIER
// uncommitted batches readable.  Save clears the overlay
// (clearSpkOverlay): the committed base then holds the same bytes.
//
// Keying is by BYTE OFFSET in the file, one whole record per entry, and a
// read hits only when an entry exists at exactly its offset with exactly
// its size — anything else falls through to the epoch's pinned descriptor.
// Every reader in the codebase reads one whole record at a record-aligned
// offset, so real reads either hit exactly or miss entirely.
#ifndef SPKOVERLAY_H
#define SPKOVERLAY_H

#include <QByteArray>
#include <QHash>
#include <memory>

struct SpkOverlay {
    /**Rewritten records: key = the record's byte offset in the .spk file,
    * value = the record's complete bytes.*/
    QHash<qint64, std::shared_ptr<const QByteArray>> records;
};

#endif // SPKOVERLAY_H
