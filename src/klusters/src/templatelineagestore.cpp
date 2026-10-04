// templatelineagestore.cpp — see templatelineagestore.h.

#include "templatelineagestore.h"

namespace tg = neurosuite::templategen;

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
