/***************************************************************************
 * radiallayout.cpp — see radiallayout.h.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "radiallayout.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>

namespace radiallayout {

namespace {
constexpr int kVersion = 1;

QString tr(const char* s) { return QCoreApplication::translate("RadialLayout", s); }

QJsonArray toArray(const RadialLayout& nodes)
{
    QJsonArray arr;
    for (const RadialLayoutNode& n : nodes) {
        QJsonObject o;
        switch (n.kind) {
        case RadialLayoutNode::Kind::Group:
            o.insert(QStringLiteral("group"), n.label);
            o.insert(QStringLiteral("items"), toArray(n.children));
            break;
        case RadialLayoutNode::Kind::Action:
            o.insert(QStringLiteral("action"), n.id);
            if (!n.label.isEmpty()) o.insert(QStringLiteral("label"), n.label);
            break;
        case RadialLayoutNode::Kind::Value:
            o.insert(QStringLiteral("value"), n.id);
            if (!n.label.isEmpty()) o.insert(QStringLiteral("label"), n.label);
            break;
        }
        arr.append(o);
    }
    return arr;
}

RadialLayout fromArray(const QJsonArray& arr)
{
    RadialLayout out;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("group"))) {
            out.append(RadialLayoutNode::group(o.value(QStringLiteral("group")).toString(),
                                               fromArray(o.value(QStringLiteral("items")).toArray())));
        } else if (o.contains(QStringLiteral("action"))) {
            const QString id = o.value(QStringLiteral("action")).toString();
            if (!id.isEmpty())
                out.append(RadialLayoutNode::action(id, o.value(QStringLiteral("label")).toString()));
        } else if (o.contains(QStringLiteral("value"))) {
            const QString id = o.value(QStringLiteral("value")).toString();
            if (!id.isEmpty())
                out.append(RadialLayoutNode::value(id, o.value(QStringLiteral("label")).toString()));
        }
        // anything else: a kind this version does not know -- skipped
    }
    return out;
}
} // namespace

RadialLayout defaults()
{
    using N = RadialLayoutNode;
    return {
        N::group(tr("Tools"), {
            N::action(QStringLiteral("menu:Tools/New Cluster")),
            N::action(QStringLiteral("menu:Tools/Split Clusters")),
            N::action(QStringLiteral("menu:Tools/Multi-Lasso")),
            N::action(QStringLiteral("menu:Tools/Split by Lassos")),
            N::action(QStringLiteral("menu:Tools/Delete Artifact Spikes")),
            N::action(QStringLiteral("menu:Tools/Delete Noisy Spikes")),
        }),
        N::group(tr("Clusters"), {
            N::action(QStringLiteral("action.deleteArtifact")),
            N::action(QStringLiteral("action.deleteNoisy")),
            N::action(QStringLiteral("action.groupClusters")),
            N::action(QStringLiteral("action.purgeSmallClusters")),
        }),
        N::group(tr("Recluster"), {
            N::action(QStringLiteral("recluster.recluster")),
            N::action(QStringLiteral("recluster.median")),
            N::action(QStringLiteral("recluster.splitByKnn")),
            N::action(QStringLiteral("recluster.dipSplit")),
            N::action(QStringLiteral("action.watershed")),
            N::action(QStringLiteral("action.stripOutliers")),
            N::action(QStringLiteral("action.stripWaveformOutliers")),
            N::action(QStringLiteral("action.realignSpikes")),
        }),
        N::action(QStringLiteral("edit.undo")),
        N::action(QStringLiteral("edit.redo")),
        N::group(tr("Hierarchy"), {
            N::action(QStringLiteral("hierarchy.mergeParents")),
            N::action(QStringLiteral("hierarchy.promoteChild")),
            N::action(QStringLiteral("hierarchy.groupChildren")),
            N::action(QStringLiteral("hierarchy.dissolveParent")),
            N::action(QStringLiteral("hierarchy.dropChildNoise")),
        }),
        N::group(tr("Sort"), {
            N::action(QStringLiteral("sort.bySimilarity"),     tr("Similarity")),
            N::action(QStringLiteral("sort.bySpikeCount"),     tr("Count")),
            N::action(QStringLiteral("sort.byTime"),           tr("Time")),
            N::action(QStringLiteral("sort.byContamination"),  tr("Contam.")),
            N::action(QStringLiteral("sort.bySnr"),            tr("SNR")),
            N::action(QStringLiteral("sort.byAmplitude"),      tr("Amplitude")),
            N::action(QStringLiteral("menu:Actions/Sort Clusters/Sort Clusters by Largest Amplitude by Channel"),
                      tr("Amp. / channel")),
            N::action(QStringLiteral("sort.byErrorPval"),      tr("Error p")),
            N::action(QStringLiteral("sort.byResidualGated"),  tr("Residual")),
            N::action(QStringLiteral("sort.byWaveformNN"),     tr("Waveform NN")),
            N::action(QStringLiteral("sort.byWaveformSpectral"), tr("Spectral")),
        }),
        N::value(QStringLiteral("dimX")),
        N::value(QStringLiteral("dimY")),
    };
}

QString toJson(const RadialLayout& layout)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), kVersion);
    root.insert(QStringLiteral("items"), toArray(layout));
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool fromJson(const QString& text, RadialLayout* out, QString* error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = pe.error != QJsonParseError::NoError ? pe.errorString()
                                                                 : QStringLiteral("not a JSON object");
        return false;
    }
    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(-1);
    if (version != kVersion) {
        if (error) *error = QStringLiteral("unsupported layout version %1").arg(version);
        return false;
    }
    if (out) *out = fromArray(root.value(QStringLiteral("items")).toArray());
    return true;
}

RadialLayout fromSettings(const QString& text)
{
    RadialLayout layout;
    if (text.trimmed().isEmpty() || !fromJson(text, &layout)) return defaults();
    return layout;
}

int ringsNeeded(const RadialLayout& layout)
{
    int deepest = layout.isEmpty() ? 0 : 1;
    for (const RadialLayoutNode& n : layout)
        if (n.kind == RadialLayoutNode::Kind::Group && !n.children.isEmpty())
            deepest = std::max(deepest, 1 + ringsNeeded(n.children));
    return deepest;
}

} // namespace radiallayout
