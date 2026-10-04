#ifndef TEMPLATELINEAGESTORE_H
#define TEMPLATELINEAGESTORE_H

// TemplateLineageStore — the Klusters-side state model for the MANUAL template
// lineage (claude/template-curation-plan.md phase 5).  Holds the open group +
// stage's .wtl forest in memory and turns the curator's edits into the rendered
// library: each node is a median over an explicit spike set, linked into a
// per-class tree (drift-root → adapt/collision leaves).
//
// Qt-free on purpose (plain std types over neurofileio + the neurosuite-core
// renderLineage* engine), so its logic is harness-testable and the TemplateView
// GUI is thin wiring — exactly like TemplateClassStore for the .eap/.tcl layer.
// The store does NOT reach into Data: the GUI hands it the spike indices of a
// selection; the store owns the tree, its persistence and the render.

#include "neurosuite/core/neurofileio.h"
#include "neurosuite/core/template_generate.hpp"
#include "neurosuite/core/drift_partition.hpp"

#include <string>
#include <vector>
#include <cstdint>

class TemplateLineageStore {
public:
    // Resolve coordinates and load the lineage for `base`/`group`/`stage` (stage =
    // the open clu tag, "" for the untagged canonical).  Reads the per-stage,
    // method-less <base>.wtl.<group>[.<stage>] when present, else starts an empty
    // forest.  The render reads the input waveforms from
    // <base>.spk.<spkVariant>.<group>[.<spkTag>] and writes the library
    // (<base>.wtl/.wti.<group>[.<stage>] + <base>.wtf.<spkVariant>.<group>[.<stage>]).
    // Always usable afterward; ok() reflects whether coordinates resolved.
    bool load(const std::string& base, int group, const std::string& stage,
              const std::string& spkVariant, const std::string& spkTag,
              int nSamples, int nChannels, double sr);
    bool ok() const { return loaded_; }

    // ── forest access ─────────────────────────────────────────────────────────
    const neurofileio::WtlForest& forest() const { return forest_; }
    std::size_t nodeCount() const { return forest_.nodes.size(); }
    const neurofileio::WtlNode* node(int nodeId) const;   // nullptr if absent

    // ── node ops (mutate memory; the caller commit()s) ─────────────────────────
    // Append a node (its id = max existing id + 1) and return that id.  `spikes`
    // is the explicit set medianed to build it (empty = an unset/placeholder node).
    int  addNode(int classId, const std::string& kind, int parent,
                 double a, double b, const std::vector<int64_t>& spikes);
    // Remove a node; any children are ORPHANED (their parent reset to -1), never
    // silently deleted.  Returns false if the id is absent.
    bool removeNode(int nodeId);
    bool setParent(int nodeId, int parent);          // false if id absent
    bool setKind(int nodeId, const std::string& kind);
    bool setWindow(int nodeId, double a, double b);
    void clear() { forest_.nodes.clear(); }

    // ── the session drift partition (the shared time grain; §9) ────────────────
    // Seeded on load from the forest's drift-root windows (else a single region
    // over [0,tEnd]); `partitionReady()` is false when the .res could not be read
    // (no res times → the partition edits are no-ops and the forest is left as
    // loaded, never silently wiped).
    const neurosuite::drift::Partition& partition() const { return partition_; }
    bool   partitionReady() const { return partitionReady_; }
    double tEnd() const { return tEnd_; }

    // Replace the partition and re-grain every class onto it (tile with
    // placeholders; re-bin pooled drift spikes; re-parent leaves).  No-op if not ready.
    void setPartition(const neurosuite::drift::Partition& p);
    // Split the region containing `tSec` at `tSec`; re-grain.  False if not ready / invalid.
    bool splitAt(double tSec);
    // Delete interior boundary `i` (merge the two regions it separates); re-grain.
    bool deleteBoundary(int i);
    // Move interior boundary `i` to `tSec` (clamped strictly between neighbours); re-grain.
    bool moveBoundary(int i, double tSec);

    // Ensure `classId` has a drift-root in every region (all-placeholder if new).
    void ensureClassTiled(int classId);
    // Set the (classId, region) drift-root's spikes to `spikes` RESTRICTED to that
    // region's time window — the time-restricted, class-scoped template edit.
    // Returns the region root's node id, or -1 if not ready / out of range.
    int  setRegionSpikes(int classId, int region, const std::vector<int64_t>& spikes);
    // Add a `kind` leaf under the (classId, region) drift-root from `spikes`
    // restricted to the region.  Returns the leaf node id, or -1.
    int  addLeaf(int classId, int region, const std::string& kind,
                 const std::vector<int64_t>& spikes);

    // ── commit ─────────────────────────────────────────────────────────────────
    // Persist the forest to .wtl and render the library (.wti v2 + .wtf) via the
    // shared neurosuite-core engine (reads the group's .spk).  Returns the engine
    // Result (ok/err + rows) for the caller to report; `wtlPath`/`wtiPath`, when
    // non-null, receive the written paths.
    neurosuite::templategen::Result commit(std::string* wtlPath = nullptr,
                                           std::string* wtiPath = nullptr);

private:
    int   indexOf(int nodeId) const;                 // position in forest_.nodes, or -1
    int   nextNodeId() const;                        // max existing id + 1 (0 if empty)
    void  retile();                                  // regrain the forest onto partition_
    int   regionRootId(int classId, int region) const;   // (class,region) drift-root id, or -1
    std::vector<int64_t> restrictToRegion(const std::vector<int64_t>& spikes, int region) const;

    neurofileio::WtlForest forest_;
    std::string base_, stage_, spkVariant_, spkTag_;
    int         group_     = 0;
    int         nSamples_  = 0;
    int         nChannels_ = 0;
    double      sr_        = 0.0;
    bool        loaded_    = false;

    neurosuite::drift::Partition partition_;
    std::vector<int64_t>         times_;             // per-spike res times (samples)
    double                       tEnd_          = 0.0;   // session end (seconds)
    bool                         partitionReady_ = false;
};

#endif // TEMPLATELINEAGESTORE_H
