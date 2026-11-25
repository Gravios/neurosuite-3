/* dirtyflag.h -- a document "modified" flag that a background save can clear
 * without losing an edit made while it ran.
 *
 * Klusters saves on a SaveThread while the curator keeps working.  The save
 * used to end with `modified = false`, so an edit made during the save was
 * marked as saved: closing afterwards asked nothing and the edit was lost.
 *
 * DirtyFlag reads and assigns like the bool it replaces, and also counts how
 * often it was set.  A save notes serial() when it takes its snapshot and, on
 * success, calls clearIfUnchanged(serial): the flag is cleared only if nothing
 * set it since.  State and serial live in one atomic word, so the check and
 * the clear cannot be split by an edit on another thread.
 */
#ifndef KLUSTERS_DIRTYFLAG_H
#define KLUSTERS_DIRTYFLAG_H

#include <atomic>
#include <cstdint>

class DirtyFlag {
public:
    explicit DirtyFlag(bool dirty = false) : state_(dirty ? 1u : 0u) {}
    DirtyFlag(const DirtyFlag&) = delete;
    DirtyFlag& operator=(const DirtyFlag&) = delete;

    /** Setting true counts as a new modification; setting false clears it. */
    DirtyFlag& operator=(bool dirty)
    {
        std::uint64_t s = state_.load();
        for (;;) {
            const std::uint64_t next = dirty ? (((s >> 1) + 1) << 1) | 1u : (s & ~std::uint64_t(1));
            if (state_.compare_exchange_weak(s, next)) break;
        }
        return *this;
    }

    operator bool() const { return (state_.load() & 1u) != 0; }

    /** How many times the flag has been set true so far. */
    std::uint64_t serial() const { return state_.load() >> 1; }

    /** Clears the flag if it was set and has not been set again since
     *  @p serial was read.  Returns whether it cleared it. */
    bool clearIfUnchanged(std::uint64_t serial)
    {
        std::uint64_t expected = (serial << 1) | 1u;
        return state_.compare_exchange_strong(expected, serial << 1);
    }

private:
    std::atomic<std::uint64_t> state_;
};

#endif // KLUSTERS_DIRTYFLAG_H
