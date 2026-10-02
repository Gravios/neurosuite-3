// decollide_eap.hpp — bridge the collision decomposition (decollide::Decomp) onto
// the .eap membership matrix + .tcl registry (claude/eap-template-class-design.md
// §9).  Header-only and Qt-free, like decollide.hpp / templateclass.hpp, so BOTH
// the ndmanager process_decomposecollisions tool and the Klusters in-GUI decollide
// commit reuse one tested primitive (the fiber-kit engine mirrors its logic).
//
// The decollide engines already produce, per collision spike, its two constituents
// (unit, tau, amp).  This records the PRESENCE + integer offset of each constituent
// in .eap (augmenting the matrix in place — one row per existing waveform, never
// growing rows), while the quantitative amplitude / fractional shift stay in .col.
// A constituent's unit id is mapped to a STABLE template-class column via the .tcl
// provenance (reused if that unit already has a class, else a fresh class is
// allocated); the integer tau drops straight into the int8 cell.
#ifndef NEUROSUITE_CORE_DECOLLIDE_EAP_HPP
#define NEUROSUITE_CORE_DECOLLIDE_EAP_HPP

#include "neurosuite/core/decollide.hpp"       // Decomp / Component
#include "neurosuite/core/templateclass.hpp"   // firstFree / createClass
#include "neurosuite/core/neurofileio.h"        // EapFile / TclRegistry / growEap / EAP_ABSENT

#include <string>
#include <vector>

namespace neurosuite {
namespace decollide {

// What applyDecompsToEap did (so a caller can log / assert).
struct EapApply {
    bool ok             = false;
    int  cellsWritten   = 0;   ///< membership cells set (<= 2 per decomp)
    int  classesCreated = 0;   ///< new .tcl classes allocated for previously-unseen units
    int  grows          = 0;   ///< times the column pool was widened on exhaustion
};

// The lowest-id Active class in `reg` whose provenanceClu == `unit`, or -1 if the
// unit has no class yet.  This is the "same unit -> same column" reuse rule.
inline int eapClassForUnit(const neurofileio::TclRegistry& reg, int unit)
{
    int found = -1;
    for (const neurofileio::TclEntry& t : reg.entries)
        if (t.status == neurofileio::TclStatus::Active && t.provenanceClu == unit &&
            (found < 0 || t.col < found))
            found = t.col;
    return found;
}

// Clamp an integer sample shift into the .eap int8 offset range, never emitting the
// ABSENT sentinel (-128): a shift that would land on -128 is pinned to -127.
inline int8_t eapClampOffset(int tau)
{
    if (tau >  127) tau =  127;
    if (tau < -127) tau = -127;                 // -128 == EAP_ABSENT, never a real offset
    return static_cast<int8_t>(tau);
}

// Apply `decomps` to the membership matrix `e` (+ registry `reg`): for each
// decomposition, set spike d.spikeIndex's cell for each constituent unit's class to
// that constituent's clamped integer shift.  A unit with no Active class yet gets a
// fresh one (provenance = that unit; stage/created as given).  The pool is grown by
// `growBy` columns when exhausted, so mapping a new unit never fails.  Cells of
// OTHER (spike,class) pairs are left untouched — this augments the matrix in place.
// No-op (ok=false) unless e/reg are loaded and agree on nClasses.
inline EapApply applyDecompsToEap(neurofileio::EapFile& e, neurofileio::TclRegistry& reg,
                                  const std::vector<Decomp>& decomps,
                                  const std::string& stage, const std::string& created,
                                  int growBy = 128)
{
    EapApply r;
    if (!e.ok || !reg.ok || reg.nClasses != e.nClasses) return r;
    if (growBy <= 0) growBy = 128;

    auto grow = [&]() {
        const int newT = e.nClasses + growBy;
        e = neurofileio::growEap(e, newT);
        while (static_cast<int>(reg.entries.size()) < newT) {
            neurofileio::TclEntry t;
            t.col    = static_cast<int>(reg.entries.size());
            t.status = neurofileio::TclStatus::Free;
            reg.entries.push_back(t);
        }
        reg.nClasses = newT;
        ++r.grows;
    };

    auto colForUnit = [&](int unit) -> int {
        int col = eapClassForUnit(reg, unit);
        if (col >= 0) return col;                              // reuse the unit's class
        if (templateclass::firstFree(reg) < 0) grow();         // pool exhausted -> widen
        col = templateclass::createClass(e, reg, /*spikes=*/{}, /*offset=*/0,
                                         /*label=*/std::string(), /*provClu=*/unit,
                                         stage, created);
        if (col >= 0) ++r.classesCreated;
        return col;
    };

    auto setCell = [&](int64_t spike, int unit, int tau) {
        if (spike < 0 || spike >= e.nSpikes) return;
        const int col = colForUnit(unit);
        if (col < 0 || col >= e.nClasses) return;
        e.cells[static_cast<std::size_t>(spike) * e.nClasses + col] = eapClampOffset(tau);
        ++r.cellsWritten;
    };

    for (const Decomp& d : decomps) {
        setCell(d.spikeIndex, d.c1.unit, d.c1.tau);
        setCell(d.spikeIndex, d.c2.unit, d.c2.tau);
    }
    r.ok = true;
    return r;
}

}  // namespace decollide
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_DECOLLIDE_EAP_HPP
