/***************************************************************************
 * driftgeometry.cpp — see driftgeometry.h.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "driftgeometry.h"

#include <klustersshared/parameteryamlreader_probes.h>
#include <yaml-cpp/yaml.h>

#include <QFile>
#include <QFileInfo>
#include <QLatin1Char>
#include <QMap>
#include <QVector>

// Load the flat per-site depth (y) list from one .probe file.  Empty on failure.
static QVector<float> readProbeSiteDepths(const QString& path)
{
    QVector<float> ys;
    if (path.isEmpty()) return ys;
    try {
        const YAML::Node doc  = YAML::LoadFile(path.toStdString());
        const YAML::Node geom = doc["probeFile"]["sites"]["geometry"];
        if (geom && geom.IsSequence()) {
            ys.reserve(static_cast<int>(geom.size()));
            for (const auto& xy : geom)
                ys.append((xy.IsSequence() && xy.size() >= 2)
                              ? static_cast<float>(xy[1].as<double>())   // [x, y] -> y
                              : 0.0f);
        }
    } catch (const YAML::Exception&) {
        ys.clear();
    }
    return ys;
}

std::vector<float> loadGroupChannelDepths(const QString& yamlPath,
                                          const QList<int>& groupChannels,
                                          QString* err)
{
    std::vector<float> depths;

    YAML::Node root;
    try {
        root = YAML::LoadFile(yamlPath.toStdString());
    } catch (const YAML::Exception& e) {
        if (err)
            *err = QStringLiteral("cannot parse session YAML: %1")
                       .arg(QString::fromStdString(e.what()));
        return depths;
    }

    QList<ProbeEntry> probes;
    QString           libPath;
    readProbesSection(root, probes, libPath);
    if (probes.isEmpty()) {
        if (err) *err = QStringLiteral("no probes section in session YAML");
        return depths;
    }

    const QString sessDir = QFileInfo(yamlPath).absolutePath();

    // Cache each probe's flat site-depth list.  A .probe file is resolved next
    // to the session first, then under probeLibraryPath.
    QMap<int, QVector<float>> depthByProbe;
    for (const ProbeEntry& pe : probes) {
        const QString local = sessDir + QLatin1Char('/') + pe.probeFile;
        QString path = QFile::exists(local)
                           ? local
                           : (libPath.isEmpty()
                                  ? QString()
                                  : libPath + QLatin1Char('/') + pe.probeFile);
        depthByProbe.insert(pe.id, readProbeSiteDepths(path));
    }

    // Map each channel to the site depth of the probe whose ADC range contains it.
    depths.reserve(static_cast<size_t>(groupChannels.size()));
    bool anyMissing = false;
    for (int c : groupChannels) {
        float y     = 0.0f;
        bool  found = false;
        for (const ProbeEntry& pe : probes) {
            const QVector<float>& ys  = depthByProbe.value(pe.id);
            const int             idx = c - pe.channelOffset;
            if (idx >= 0 && idx < ys.size()) {
                y     = ys.at(idx);
                found = true;
                break;
            }
        }
        if (!found) anyMissing = true;
        depths.push_back(y);
    }

    if (anyMissing) {
        if (err) *err = QStringLiteral("some channels had no matching probe geometry");
        depths.clear();   // partial geometry is unsafe for depth interpolation
    }
    return depths;
}
