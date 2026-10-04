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

    neurofileio::WtlForest forest_;
    std::string base_, stage_, spkVariant_, spkTag_;
    int         group_     = 0;
    int         nSamples_  = 0;
    int         nChannels_ = 0;
    double      sr_        = 0.0;
    bool        loaded_    = false;
};

#endif // TEMPLATELINEAGESTORE_H
