/***************************************************************************
 *                          spikeassignment.h                              *
 *                                                                         *
 *   Incremental cluster-membership store for Klusters.                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/
#ifndef SPIKEASSIGNMENT_H
#define SPIKEASSIGNMENT_H

#include "types.h" // dataType (== long); Qt-free

#include <algorithm>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * SpikeAssignment — incremental cluster-membership store.
 *
 * Replaces the rebuild-everything `spikesByCluster` table with three arrays:
 *
 *   clusterOf[spike]   spike -> cluster id.  In file order, so it IS the .clu
 *                      column and writing it back is a straight copy.
 *   members[cluster]   cluster id -> contiguous vector<spike> (a packed
 *                      "sparse set"): O(1) add/remove via swap-pop, and
 *                      cache-friendly iteration for the views and worker
 *                      threads that stream a cluster's spikes.
 *   slotOf[spike]      index of `spike` inside its current cluster's members
 *                      vector, so removal is O(1) without a search.
 *
 * Consequences vs the old table:
 *   - an edit is O(spikes moved), not O(total spikes);
 *   - undo/redo store per-edit deltas (the changed spikes only), not a full
 *     table copy per level — O(moved) memory instead of O(N) x undoDepth.
 *
 * Policy-free on purpose: it does not know that cluster 0/1 are special, does
 * not renumber, and does not decide what an "empty" cluster means beyond
 * dropping its (now empty) bucket.  Those policies stay in the Data layer.
 *
 * CONCURRENCY: this type performs IN-PLACE mutation and is NOT internally
 * synchronised.  The current Data model is safe for reader threads precisely
 * because reassignment builds a new table and swaps the pointer, leaving the
 * old table alive; in-place mutation removes that guarantee.  When wiring this
 * into Data, restore an equivalent guarantee for worker threads — e.g. have a
 * worker copy only the members() of the cluster(s) it renders under the data
 * mutex (O(viewed), not O(N)), or publish a versioned read snapshot — rather
 * than letting a worker iterate members() concurrently with an edit.
 */
class SpikeAssignment
{
public:
    using Spike   = dataType; // spike index, file order
    using Cluster = dataType; // cluster id

    SpikeAssignment() = default;
    explicit SpikeAssignment(const std::vector<Cluster> &labels) { reset(labels); }

    /** (Re)initialise from per-spike labels (e.g. the .clu column). Clears
     *  undo/redo history. */
    void reset(const std::vector<Cluster> &labels)
    {
        if (labels.size() > 0xFFFFFFFFull)
            throw std::length_error("SpikeAssignment: spike count exceeds 2^32");

        clusterOf_ = labels;
        slotOf_.assign(labels.size(), 0);
        buckets_.clear();
        maxId_ = 0;
        editing_ = false;
        open_.clear();
        undo_.clear();
        redo_.clear();

        for (std::size_t s = 0; s < clusterOf_.size(); ++s) {
            const Cluster c = clusterOf_[s];
            std::vector<Spike> &m = buckets_[c];
            slotOf_[s] = static_cast<std::uint32_t>(m.size());
            m.push_back(static_cast<Spike>(s));
            if (c > maxId_)
                maxId_ = c;
        }
    }

    // ---- queries -----------------------------------------------------------
    Spike   nbSpikes()          const { return static_cast<Spike>(clusterOf_.size()); }
    Cluster clusterOf(Spike s)  const { return clusterOf_[static_cast<std::size_t>(s)]; }
    bool    contains(Cluster c) const { return buckets_.find(c) != buckets_.end(); }
    Cluster maxClusterId()      const { return maxId_; }

    Spike count(Cluster c) const
    {
        auto it = buckets_.find(c);
        return it == buckets_.end() ? 0 : static_cast<Spike>(it->second.size());
    }

    /** Contiguous spike list of cluster @p c (empty if @p c has no spikes). */
    const std::vector<Spike> &members(Cluster c) const
    {
        auto it = buckets_.find(c);
        return it == buckets_.end() ? empty_() : it->second;
    }

    /** Active (non-empty) cluster ids, ascending. */
    std::vector<Cluster> clusters() const
    {
        std::vector<Cluster> out;
        out.reserve(buckets_.size());
        for (const auto &kv : buckets_)
            out.push_back(kv.first);
        std::sort(out.begin(), out.end());
        return out;
    }

    // ---- edits (one undoable delta per begin/end) --------------------------
    void beginEdit()
    {
        if (editing_)
            throw std::logic_error("SpikeAssignment: edit already in progress");
        editing_ = true;
        open_.clear();
    }

    /** Move one spike to cluster @p to.  No-op (and not recorded) if already
     *  there.  Must be inside begin/endEdit. */
    void moveSpike(Spike s, Cluster to)
    {
        if (!editing_)
            throw std::logic_error("SpikeAssignment: moveSpike outside an edit");
        const Cluster before = clusterOf_[static_cast<std::size_t>(s)];
        if (before == to)
            return;
        open_.push_back(Change{s, before, to});
        applyMove_(s, to);
    }

    void moveSpikes(const std::vector<Spike> &spikes, Cluster to)
    {
        for (Spike s : spikes)
            moveSpike(s, to);
    }

    /** Merge every spike of @p from into @p to.  Returns the number moved.
     *  Must be inside begin/endEdit. */
    Spike mergeCluster(Cluster from, Cluster to)
    {
        if (from == to)
            return 0;
        auto it = buckets_.find(from);
        if (it == buckets_.end())
            return 0;
        // Copy first: the bucket is mutated as we move spikes out of it.
        const std::vector<Spike> toMove = it->second;
        for (Spike s : toMove)
            moveSpike(s, to);
        return static_cast<Spike>(toMove.size());
    }

    /** Commit the open edit.  A no-op edit (nothing changed) pushes nothing
     *  and leaves the redo stack intact. */
    void endEdit()
    {
        if (!editing_)
            throw std::logic_error("SpikeAssignment: endEdit without beginEdit");
        editing_ = false;
        if (open_.empty())
            return;
        redo_.clear();
        undo_.push_back(std::move(open_));
        open_.clear();
        trimUndo_();
    }

    bool editInProgress() const { return editing_; }

    // ---- undo / redo (O(spikes changed in that edit)) ----------------------
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    void undo()
    {
        if (undo_.empty())
            return;
        Delta d = std::move(undo_.back());
        undo_.pop_back();
        // Reverse order, restore the `before` cluster of each change, so a
        // spike moved several times within the edit lands on its net origin.
        for (auto it = d.rbegin(); it != d.rend(); ++it)
            applyMove_(it->spike, it->before);
        redo_.push_back(std::move(d));
    }

    void redo()
    {
        if (redo_.empty())
            return;
        Delta d = std::move(redo_.back());
        redo_.pop_back();
        for (const Change &c : d)
            applyMove_(c.spike, c.after);
        undo_.push_back(std::move(d));
    }

    /** Maximum retained undo levels; trims oldest immediately if lowered. */
    void setUndoDepth(std::size_t n)
    {
        undoDepth_ = n;
        trimUndo_();
    }
    std::size_t undoDepth() const { return undoDepth_; }

    // ---- integrity (debug / tests) -----------------------------------------
    /** Verify the three arrays are mutually consistent. */
    bool checkInvariants() const
    {
        std::size_t total = 0;
        for (const auto &kv : buckets_) {
            const std::vector<Spike> &m = kv.second;
            if (m.empty())
                return false; // empty buckets must be dropped
            total += m.size();
            for (std::uint32_t i = 0; i < m.size(); ++i) {
                const Spike s = m[i];
                if (clusterOf_[static_cast<std::size_t>(s)] != kv.first)
                    return false;
                if (slotOf_[static_cast<std::size_t>(s)] != i)
                    return false;
            }
        }
        return total == clusterOf_.size();
    }

private:
    struct Change { Spike spike; Cluster before; Cluster after; };
    using Delta = std::vector<Change>;

    /** O(1) move with no delta recording: swap-pop out of `from`, push to `to`. */
    void applyMove_(Spike s, Cluster to)
    {
        const std::size_t si = static_cast<std::size_t>(s);
        const Cluster from = clusterOf_[si];
        if (from == to)
            return;

        auto fit = buckets_.find(from);
        std::vector<Spike> &mf = fit->second; // `from` must exist
        const std::uint32_t i = slotOf_[si];
        const Spike last = mf.back();
        mf[i] = last;
        slotOf_[static_cast<std::size_t>(last)] = i;
        mf.pop_back();
        if (mf.empty())
            buckets_.erase(fit);

        std::vector<Spike> &mt = buckets_[to];
        slotOf_[si] = static_cast<std::uint32_t>(mt.size());
        mt.push_back(s);
        clusterOf_[si] = to;
        if (to > maxId_)
            maxId_ = to;
    }

    void trimUndo_()
    {
        while (undo_.size() > undoDepth_)
            undo_.pop_front();
    }

    static const std::vector<Spike> &empty_()
    {
        static const std::vector<Spike> e;
        return e;
    }

    std::vector<Cluster> clusterOf_;                          // spike -> cluster
    std::vector<std::uint32_t> slotOf_;                       // spike -> index in members
    std::unordered_map<Cluster, std::vector<Spike>> buckets_; // cluster -> members
    Cluster maxId_ = 0;

    bool  editing_ = false;
    Delta open_;
    std::deque<Delta> undo_, redo_;
    std::size_t undoDepth_ = 50;
};

#endif // SPIKEASSIGNMENT_H
