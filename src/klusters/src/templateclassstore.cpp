// TemplateClassStore — implementation.  See templateclassstore.h.
//
// Thin app-level layer over the unit-tested CRUD engine
// (neurosuite::templateclass) and the neurofileio .eap/.tcl primitives: it owns
// path resolution, base->stage inheritance, pool growth, and persistence, and
// delegates every membership/registry mutation to the engine so the two are
// never re-implemented.

#include "templateclassstore.h"

#include "neurosuite/core/custody.hpp"
#include "neurosuite/core/templateclass.hpp"

#include <algorithm>

namespace nf  = neurofileio;
namespace tc  = neurosuite::templateclass;

bool TemplateClassStore::load(const std::string& base, int group,
                              const std::string& stage, int64_t nSpikes,
                              int defaultNCells)
{
    stage_         = stage;
    defaultNCells_ = (defaultNCells > 0) ? defaultNCells : 128;
    primary_       = -1;

    // Method-less paths.  The .eap is per-stage; the .tcl is stage-independent so
    // a class id denotes the same column across every stage of the session/group.
    const std::string baseEap = neurosuite::custody::untaggedPath(base, "eap", group);
    stageEapPath_ = baseEap + (stage.empty() ? std::string() : ("." + stage));
    tclPath_      = neurosuite::custody::untaggedPath(base, "tcl", group);

    // .eap resolution: this stage's file; else INHERIT the untagged base file (so
    // a freshly-tagged stage starts from the canonical membership); else a fresh
    // all-absent matrix sized nSpikes × defaultNCells.
    eap_ = nf::readEap(stageEapPath_);
    if (!eap_.ok && !stage.empty())
        eap_ = nf::readEap(baseEap);
    if (!eap_.ok) {
        eap_.nSpikes  = (nSpikes < 0) ? 0 : nSpikes;
        eap_.nClasses = defaultNCells_;
        eap_.group    = group;
        eap_.flags    = 0;
        eap_.cells.assign(static_cast<std::size_t>(eap_.nSpikes) * defaultNCells_,
                          nf::EAP_ABSENT);
        eap_.ok = true;
    }

    // .tcl resolution: its file; else a fresh registry matching the matrix width.
    tcl_ = nf::readTcl(tclPath_);
    if (!tcl_.ok)
        tcl_ = nf::initTcl(eap_.nClasses);

    // Reconcile widths: a class id must be a valid column in BOTH structures.  If
    // another stage grew the shared pool, the .tcl records more classes than this
    // stage's (possibly inherited) .eap — widen the matrix to match, and vice
    // versa — so engine calls that require reg.nClasses == eap.nClasses hold.
    const int T = std::max(eap_.nClasses, tcl_.nClasses);
    if (eap_.nClasses < T)
        eap_ = nf::growEap(eap_, T);
    while (static_cast<int>(tcl_.entries.size()) < T) {
        nf::TclEntry e;
        e.col    = static_cast<int>(tcl_.entries.size());
        e.status = nf::TclStatus::Free;
        tcl_.entries.push_back(e);
    }
    tcl_.nClasses = T;

    loaded_ = true;
    return true;
}

bool TemplateClassStore::save() const
{
    if (!loaded_) return false;
    const bool a = nf::writeEap(stageEapPath_, eap_.nSpikes, eap_.nClasses,
                                eap_.group, eap_.flags, eap_.cells);
    const bool b = nf::writeTcl(tclPath_, tcl_);
    return a && b;
}

std::vector<int> TemplateClassStore::activeClasses() const
{
    return tc::activeCols(tcl_);
}

std::string TemplateClassStore::label(int col) const
{
    if (col < 0 || col >= static_cast<int>(tcl_.entries.size())) return std::string();
    return tcl_.entries[static_cast<std::size_t>(col)].label;
}

std::vector<int64_t> TemplateClassStore::members(int col) const
{
    return tc::members(eap_, col);
}
int TemplateClassStore::provClu(int col) const
{
    if (col < 0 || col >= static_cast<int>(tcl_.entries.size())) return -1;
    return tcl_.entries[static_cast<std::size_t>(col)].provenanceClu;
}
std::string TemplateClassStore::provStage(int col) const
{
    if (col < 0 || col >= static_cast<int>(tcl_.entries.size())) return std::string();
    return tcl_.entries[static_cast<std::size_t>(col)].provenanceStage;
}
std::string TemplateClassStore::created(int col) const
{
    if (col < 0 || col >= static_cast<int>(tcl_.entries.size())) return std::string();
    return tcl_.entries[static_cast<std::size_t>(col)].created;
}

void TemplateClassStore::growTo(int newT)
{
    if (newT <= eap_.nClasses) return;
    eap_ = nf::growEap(eap_, newT);
    while (static_cast<int>(tcl_.entries.size()) < newT) {
        nf::TclEntry e;
        e.col    = static_cast<int>(tcl_.entries.size());
        e.status = nf::TclStatus::Free;
        tcl_.entries.push_back(e);
    }
    tcl_.nClasses = newT;
}

int TemplateClassStore::createClass(const std::vector<int64_t>& spikes,
                                    const std::string& label, int provClu,
                                    const std::string& provStage,
                                    const std::string& created)
{
    if (!loaded_) return -1;
    if (tc::firstFree(tcl_) < 0)                     // pool exhausted -> grow, never fail
        growTo(eap_.nClasses + defaultNCells_);
    const int col = tc::createClass(eap_, tcl_, spikes, /*offset=*/0,
                                    label, provClu, provStage, created);
    if (col >= 0) primary_ = col;                    // a new class becomes primary
    return col;
}

bool TemplateClassStore::updateClass(int col, const std::vector<int64_t>& spikes,
                                     int8_t offset)
{
    if (!loaded_) return false;
    if (col < 0 || col >= static_cast<int>(tcl_.entries.size())) return false;
    if (tcl_.entries[static_cast<std::size_t>(col)].status != nf::TclStatus::Active)
        return false;
    return tc::setMembership(eap_, col, spikes, offset);
}

bool TemplateClassStore::mergeClasses(int survivor, int victim)
{
    if (!loaded_) return false;
    if (!tc::mergeClasses(eap_, tcl_, survivor, victim)) return false;
    primary_ = survivor;                             // survivor stays the update target
    return true;
}

bool TemplateClassStore::deleteClass(int col)
{
    if (!loaded_) return false;
    if (!tc::deleteClass(eap_, tcl_, col)) return false;
    if (primary_ == col) primary_ = -1;
    return true;
}

bool TemplateClassStore::rename(int col, const std::string& label)
{
    if (!loaded_) return false;
    if (col < 0 || col >= static_cast<int>(tcl_.entries.size())) return false;
    nf::TclEntry& t = tcl_.entries[static_cast<std::size_t>(col)];
    if (t.status != nf::TclStatus::Active) return false;
    t.label = label;
    return true;
}
