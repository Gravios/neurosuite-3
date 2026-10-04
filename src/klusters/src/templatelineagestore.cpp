// templatelineagestore.cpp — see templatelineagestore.h.

#include "templatelineagestore.h"
#include "neurosuite/core/custody.hpp"   // resolveAny for the SHARED .res

#include <algorithm>

namespace tg = neurosuite::templategen;
namespace dr = neurosuite::drift;

bool TemplateLineageStore::load(const std::string& base, int group, const std::string& stage,
                                const std::string& spkVariant, const std::string& spkTag,
                                int nSamples, int nChannels, double sr)
{
    base_ = base; group_ = group; stage_ = stage;
    spkVariant_ = spkVariant; spkTag_ = spkTag;
    nSamples_ = nSamples; nChannels_ = nChannels; sr_ = sr;

    // The lineage is per-stage and method-less, like .eap/.wti.
    const std::string wtlPath = tg::sessionPath(base_, "wtl", group_, "", stage_);
    neurofileio::WtlForest f = neurofileio::readWtl(wtlPath);
    forest_ = f.ok ? f : neurofileio::WtlForest{};   // absent/invalid -> empty forest
    forest_.version = 1;
    loaded_ = (nSamples_ > 0 && nChannels_ > 0 && !base_.empty());

    // The SHARED res (spike times, for the drift partition / re-grain).  Take
    // whichever method token wrote it, exactly like generateToFiles.
    times_.clear();
    tEnd_ = 0.0;
    const neurosuite::custody::Resolved rr =
        neurosuite::custody::resolveAny(base_, "res", group_, spkVariant_);
    if (rr.found) { bool rok = false; times_ = tg::readResAny(rr.path, &rok); if (!rok) times_.clear(); }
    if (!times_.empty() && sr_ > 0.0) {
        int64_t tmax = 0;
        for (int64_t t : times_) tmax = std::max(tmax, t);
        tEnd_ = static_cast<double>(tmax) / sr_;
    }
    partitionReady_ = (!times_.empty() && sr_ > 0.0 && tEnd_ > 0.0);

    if (partitionReady_) {
        // Seed the partition from the loaded forest's drift-root windows (the
        // tiling placeholders carry the grain); an empty/absent forest -> a single
        // session-spanning region, which the GUI may replace with the cluster
        // time-restricted mode's grain.
        std::vector<std::pair<double,double>> windows;
        for (const neurofileio::WtlNode& n : forest_.nodes)
            if (dr::detail::isDriftKind(n.kind) && n.parent < 0) windows.push_back({ n.a, n.b });
        partition_ = windows.empty() ? dr::uniformPartition(tEnd_, 1)
                                     : dr::partitionFromWindows(windows, tEnd_);
        retile();                       // normalise the forest to the seeded grain
    } else {
        partition_ = dr::Partition{};
    }
    return loaded_;
}

int TemplateLineageStore::indexOf(int nodeId) const
{
    for (std::size_t i = 0; i < forest_.nodes.size(); ++i)
        if (forest_.nodes[i].node == nodeId) return static_cast<int>(i);
    return -1;
}

const neurofileio::WtlNode* TemplateLineageStore::node(int nodeId) const
{
    const int i = indexOf(nodeId);
    return i < 0 ? nullptr : &forest_.nodes[static_cast<std::size_t>(i)];
}

int TemplateLineageStore::nextNodeId() const
{
    int mx = -1;
    for (const neurofileio::WtlNode& n : forest_.nodes) if (n.node > mx) mx = n.node;
    return mx + 1;
}

int TemplateLineageStore::addNode(int classId, const std::string& kind, int parent,
                                  double a, double b, const std::vector<int64_t>& spikes)
{
    neurofileio::WtlNode n;
    n.node = nextNodeId();
    n.classId = classId;
    n.kind = kind;
    n.parent = parent;
    n.a = a; n.b = b;
    n.spikes = spikes;
    forest_.nodes.push_back(std::move(n));
    return forest_.nodes.back().node;
}

bool TemplateLineageStore::removeNode(int nodeId)
{
    const int i = indexOf(nodeId);
    if (i < 0) return false;
    forest_.nodes.erase(forest_.nodes.begin() + i);
    // Orphan any children rather than deleting a subtree silently.
    for (neurofileio::WtlNode& n : forest_.nodes)
        if (n.parent == nodeId) n.parent = -1;
    return true;
}

bool TemplateLineageStore::setParent(int nodeId, int parent)
{
    const int i = indexOf(nodeId);
    if (i < 0) return false;
    forest_.nodes[static_cast<std::size_t>(i)].parent = parent;
    return true;
}

bool TemplateLineageStore::setKind(int nodeId, const std::string& kind)
{
    const int i = indexOf(nodeId);
    if (i < 0) return false;
    forest_.nodes[static_cast<std::size_t>(i)].kind = kind;
    return true;
}

bool TemplateLineageStore::setWindow(int nodeId, double a, double b)
{
    const int i = indexOf(nodeId);
    if (i < 0) return false;
    forest_.nodes[static_cast<std::size_t>(i)].a = a;
    forest_.nodes[static_cast<std::size_t>(i)].b = b;
    return true;
}

tg::Result TemplateLineageStore::commit(std::string* wtlPath, std::string* wtiPath)
{
    tg::LineageFileParams fp;
    fp.base = base_;
    fp.group = group_;
    fp.variants = { spkVariant_ };
    fp.stage = stage_;
    fp.spkTag = spkTag_;
    fp.nSamples = nSamples_;
    fp.nChannels = nChannels_;
    fp.sr = sr_;
    return tg::renderLineageToFiles(fp, forest_, wtlPath, wtiPath, nullptr);
}

// ── session drift partition ─────────────────────────────────────────────────
void TemplateLineageStore::retile()
{
    if (!partitionReady_) return;
    forest_ = dr::regrainForest(forest_, times_, sr_, partition_);
}

int TemplateLineageStore::regionRootId(int classId, int region) const
{
    for (const neurofileio::WtlNode& n : forest_.nodes) {
        if (n.classId != classId || !dr::detail::isDriftKind(n.kind) || n.parent >= 0) continue;
        if (partition_.regionOf(0.5 * (n.a + n.b)) == region) return n.node;
    }
    return -1;
}

std::vector<int64_t> TemplateLineageStore::restrictToRegion(const std::vector<int64_t>& spikes,
                                                            int region) const
{
    std::vector<int64_t> out;
    out.reserve(spikes.size());
    for (int64_t s : spikes) {
        if (s < 0 || s >= static_cast<int64_t>(times_.size())) continue;
        if (partition_.regionOf(static_cast<double>(times_[static_cast<std::size_t>(s)]) / sr_) == region)
            out.push_back(s);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void TemplateLineageStore::setPartition(const dr::Partition& p)
{
    if (!partitionReady_) return;
    partition_ = p;
    retile();
}

bool TemplateLineageStore::splitAt(double tSec)
{
    if (!partitionReady_) return false;
    const int r = partition_.regionOf(tSec);
    if (!dr::splitRegion(partition_, r, tSec)) return false;
    retile();
    return true;
}

bool TemplateLineageStore::deleteBoundary(int i)
{
    if (!partitionReady_) return false;
    if (!dr::mergeRegion(partition_, i)) return false;   // i indexes the interior boundary
    retile();
    return true;
}

bool TemplateLineageStore::moveBoundary(int i, double tSec)
{
    if (!partitionReady_) return false;
    if (i < 0 || i >= static_cast<int>(partition_.bounds.size())) return false;
    const double lo = (i == 0) ? 0.0 : partition_.bounds[static_cast<std::size_t>(i) - 1];
    const double hi = (i + 1 < static_cast<int>(partition_.bounds.size()))
                          ? partition_.bounds[static_cast<std::size_t>(i) + 1] : tEnd_;
    if (!(tSec > lo && tSec < hi)) return false;          // keep strictly ordered / inside
    partition_.bounds[static_cast<std::size_t>(i)] = tSec;
    retile();
    return true;
}

void TemplateLineageStore::ensureClassTiled(int classId)
{
    if (!partitionReady_) return;
    for (const neurofileio::WtlNode& n : forest_.nodes)
        if (n.classId == classId) return;                 // already present (load/retile tiled it)
    dr::tileClassDrift(forest_, classId, {}, times_, sr_, partition_);
}

int TemplateLineageStore::setRegionSpikes(int classId, int region, const std::vector<int64_t>& spikes)
{
    if (!partitionReady_ || region < 0 || region >= partition_.nRegions()) return -1;
    ensureClassTiled(classId);
    const int rid = regionRootId(classId, region);
    if (rid < 0) return -1;
    const int idx = indexOf(rid);
    if (idx < 0) return -1;
    forest_.nodes[static_cast<std::size_t>(idx)].spikes = restrictToRegion(spikes, region);
    return rid;
}

int TemplateLineageStore::addLeaf(int classId, int region, const std::string& kind,
                                  const std::vector<int64_t>& spikes)
{
    if (!partitionReady_ || region < 0 || region >= partition_.nRegions()) return -1;
    ensureClassTiled(classId);
    const int rid = regionRootId(classId, region);
    if (rid < 0) return -1;
    const auto ab = partition_.region(region);
    return addNode(classId, kind, rid, ab.first, ab.second, restrictToRegion(spikes, region));
}
