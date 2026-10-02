// templateclass.hpp — template-class CRUD on the in-memory .eap matrix + .tcl
// registry (claude/eap-template-class-design.md §8).  Header-only and Qt-free,
// like decollide.hpp / custody.hpp, so the GUI (Klusters TemplateView) is thin
// wiring over a unit-tested core and other tools can reuse it.
//
// A class id IS its .eap column.  These functions keep the two structures in
// step — a class is live iff its .tcl slot is Active, and its members are the
// present cells of its column.  They mutate the structs in memory; the caller
// persists with neurofileio writeEap/writeTcl.
#ifndef NEUROSUITE_CORE_TEMPLATECLASS_HPP
#define NEUROSUITE_CORE_TEMPLATECLASS_HPP

#include "neurosuite/core/neurofileio.h"

#include <algorithm>
#include <string>
#include <vector>

namespace neurosuite {
namespace templateclass {

using neurofileio::EapFile;
using neurofileio::TclEntry;
using neurofileio::TclRegistry;
using neurofileio::TclStatus;

// Active class ids (columns), ascending.
inline std::vector<int> activeCols(const TclRegistry& reg)
{
    std::vector<int> v;
    for (const TclEntry& e : reg.entries)
        if (e.status == TclStatus::Active) v.push_back(e.col);
    std::sort(v.begin(), v.end());
    return v;
}

// The next Free column (the id a create would take), or -1 if the pool is
// exhausted — the caller then grows (neurofileio::growEap + extend the registry).
inline int firstFree(const TclRegistry& reg)
{
    for (const TclEntry& e : reg.entries)
        if (e.status == TclStatus::Free) return e.col;
    return -1;
}

// Set column `col` to EXACTLY `spikes` (clearing any prior membership of that
// column), each at `offset`.  The create/update membership primitive.  Returns
// false on an out-of-range column.
inline bool setMembership(EapFile& e, int col,
                          const std::vector<int64_t>& spikes, int8_t offset = 0)
{
    if (col < 0 || col >= e.nClasses) return false;
    for (int64_t i = 0; i < e.nSpikes; ++i)
        e.cells[static_cast<std::size_t>(i) * e.nClasses + col] = neurofileio::EAP_ABSENT;
    for (int64_t s : spikes)
        if (s >= 0 && s < e.nSpikes)
            e.cells[static_cast<std::size_t>(s) * e.nClasses + col] = offset;
    return true;
}

// Member spike indices of class `col` (present cells), ascending.
inline std::vector<int64_t> members(const EapFile& e, int col)
{
    std::vector<int64_t> v;
    if (col < 0 || col >= e.nClasses) return v;
    for (int64_t i = 0; i < e.nSpikes; ++i)
        if (neurofileio::eapPresent(e.cells[static_cast<std::size_t>(i) * e.nClasses + col]))
            v.push_back(i);
    return v;
}

// Allocate the next Free column as a new ACTIVE class (filling its provenance)
// and set its membership to `spikes`@offset.  Returns the new class id (column),
// or -1 if the pool is full (grow first) or e/reg disagree on nClasses.
inline int createClass(EapFile& e, TclRegistry& reg,
                       const std::vector<int64_t>& spikes, int8_t offset,
                       const std::string& label, int provClu,
                       const std::string& provStage, const std::string& created)
{
    if (reg.nClasses != e.nClasses) return -1;
    const int col = firstFree(reg);
    if (col < 0 || col >= static_cast<int>(reg.entries.size()) || col >= e.nClasses)
        return -1;
    TclEntry& t = reg.entries[static_cast<std::size_t>(col)];
    t.status          = TclStatus::Active;
    t.label           = label;
    t.provenanceClu   = provClu;
    t.provenanceStage = provStage;
    t.created         = created;
    t.mergedInto      = -1;
    setMembership(e, col, spikes, offset);
    return col;
}

// Delete an Active class: tombstone its slot (the id is NEVER reused) and clear
// its .eap column.  Returns false unless `col` is an in-range Active class.
inline bool deleteClass(EapFile& e, TclRegistry& reg, int col)
{
    if (col < 0 || col >= static_cast<int>(reg.entries.size()) || col >= e.nClasses)
        return false;
    TclEntry& t = reg.entries[static_cast<std::size_t>(col)];
    if (t.status != TclStatus::Active) return false;
    t.status = TclStatus::Tomb;
    for (int64_t i = 0; i < e.nSpikes; ++i)
        e.cells[static_cast<std::size_t>(i) * e.nClasses + col] = neurofileio::EAP_ABSENT;
    return true;
}

// Merge `victim` into `survivor` (both Active): a spike present in victim is set
// in survivor ONLY where survivor is absent — the survivor keeps its own offset
// on a conflict, since .eap carries no amplitude to arbitrate (amplitudes live in
// .col); the victim column is cleared and its slot marked merged:<survivor>.
// Returns false unless both are distinct in-range Active classes.
inline bool mergeClasses(EapFile& e, TclRegistry& reg, int survivor, int victim)
{
    if (survivor == victim || survivor < 0 || victim < 0) return false;
    if (survivor >= e.nClasses || victim >= e.nClasses) return false;
    if (survivor >= static_cast<int>(reg.entries.size()) ||
        victim   >= static_cast<int>(reg.entries.size())) return false;
    TclEntry& sv = reg.entries[static_cast<std::size_t>(survivor)];
    TclEntry& vc = reg.entries[static_cast<std::size_t>(victim)];
    if (sv.status != TclStatus::Active || vc.status != TclStatus::Active) return false;
    for (int64_t i = 0; i < e.nSpikes; ++i) {
        const std::size_t base = static_cast<std::size_t>(i) * e.nClasses;
        const int8_t vv = e.cells[base + victim];
        if (vv != neurofileio::EAP_ABSENT) {
            if (e.cells[base + survivor] == neurofileio::EAP_ABSENT)
                e.cells[base + survivor] = vv;      // survivor absent -> adopt victim's offset
            e.cells[base + victim] = neurofileio::EAP_ABSENT;
        }
    }
    vc.status     = TclStatus::Merged;
    vc.mergedInto = survivor;
    return true;
}

}  // namespace templateclass
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_TEMPLATECLASS_HPP
