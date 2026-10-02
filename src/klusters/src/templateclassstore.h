#ifndef TEMPLATECLASSSTORE_H
#define TEMPLATECLASSSTORE_H

// TemplateClassStore — the Klusters-side state model for the EAP template-class
// layer (claude/eap-template-class-design.md phase 4).  Holds the open group +
// stage's .eap membership matrix and .tcl registry in memory, wraps the unit-
// tested CRUD engine (neurosuite::templateclass) with the app-level concerns the
// engine deliberately leaves out: path resolution, base->stage inheritance,
// pool-exhaustion growth, persistence, and a "primary" class.
//
// Qt-free on purpose (plain std types over neurofileio) so its logic is harness-
// testable and the TemplateView GUI (phase 4b) is thin wiring.  A class id IS its
// .eap column; members are a column's present cells; `primary` is the id the
// "update" command acts on (TemplateView sets it to the first-selected class).

#include "neurosuite/core/neurofileio.h"

#include <string>
#include <vector>
#include <cstdint>

class TemplateClassStore {
public:
    // Load the class layer for `base`/`group`/`stage` (stage = the open clu tag,
    // "" for the untagged canonical).  The .eap is per-stage and method-less
    // (<base>.eap.<group>[.<stage>]); the .tcl is stage-independent
    // (<base>.tcl.<group>).  Resolution, in order:
    //   .eap: the stage file; else INHERIT the untagged base file; else a fresh
    //         all-absent matrix sized nSpikes × defaultNCells.
    //   .tcl: its file; else a fresh registry of the matrix's nClasses free slots.
    // save() always writes the .eap to the STAGE path, so editing a stage never
    // clobbers the base.  Returns true (always usable afterward); ok() reflects it.
    bool load(const std::string& base, int group, const std::string& stage,
              int64_t nSpikes, int defaultNCells = 128);
    bool ok() const { return loaded_; }

    // Persist the in-memory matrix (to the stage .eap path) and registry.
    bool save() const;

    // ── queries ──────────────────────────────────────────────────────────────
    std::vector<int>     activeClasses() const;        // active column ids, ascending
    std::string          label(int col) const;         // "" if none / out of range
    std::vector<int64_t> members(int col) const;        // present spikes of the column
    int   nClasses() const { return eap_.nClasses; }
    int   primary() const { return primary_; }
    void  setPrimary(int col) { primary_ = col; }
    const neurofileio::EapFile&     eap() const { return eap_; }
    const neurofileio::TclRegistry& tcl() const { return tcl_; }
    const std::string& stage() const { return stage_; }

    // ── CRUD (mutate memory; the caller save()s + regenerates templates) ──────
    // Allocate a new ACTIVE class from `spikes` (offset 0) and make it primary;
    // GROWS the matrix+registry by defaultNCells when the free pool is exhausted,
    // so a create never silently fails for lack of a column.  Returns the new
    // class id, or -1 only on an internal inconsistency.
    int  createClass(const std::vector<int64_t>& spikes, const std::string& label,
                     int provClu, const std::string& provStage, const std::string& created);
    // Re-set an ACTIVE class's membership to EXACTLY `spikes` (each at `offset`),
    // clearing any prior membership of that column — the "update primary with the
    // current selection" operation.  Returns false unless `col` is an active class.
    bool updateClass(int col, const std::vector<int64_t>& spikes, int8_t offset = 0);
    // Fold `victim` into `survivor` (survivor kept primary); returns false unless
    // both are distinct active classes.
    bool mergeClasses(int survivor, int victim);
    // Tombstone `col` (id never reused) and clear its column; clears primary if it
    // pointed there.  Returns false unless `col` is an active class.
    bool deleteClass(int col);
    // Relabel an active class.  Returns false unless `col` is active.
    bool rename(int col, const std::string& label);

private:
    void growTo(int newT);                 // widen eap_ + extend tcl_ with free slots

    neurofileio::EapFile     eap_;
    neurofileio::TclRegistry tcl_;
    std::string stageEapPath_;             // save target (per stage)
    std::string tclPath_;                  // stage-independent
    std::string stage_;
    int  defaultNCells_ = 128;
    int  primary_ = -1;
    bool loaded_ = false;
};

#endif // TEMPLATECLASSSTORE_H
