/***************************************************************************
    clusterlabel.h
    --------------
    How Klusters shows a cluster id: a reserved chunk cluster as R<chunk>
    (R0 .. R99, the fiber-kit chunk: cluster 16777130 holds chunk 14 and is
    shown as R14), every other cluster as its number.  The palettes, views,
    matrices, messages and dialogs use it.  Files, logs and plugin arguments
    keep the number.  The rules are reservedclusters::label / idFromLabel
    (Qt-free, tested by klusters_test_reservedclusters).

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef CLUSTERLABEL_H
#define CLUSTERLABEL_H

#include "reservedclusters.h"

#include <QString>
#include <QStringList>

/// The label shown for cluster @p id: "R<chunk>" for a reserved id, the number otherwise.
inline QString clusterLabel(long long id)
{
    return QString::fromStdString(reservedclusters::label(id));
}

/// The labels of @p ids, in their order (QList<int>, std::vector, ...).
template <class Ids>
QStringList clusterLabels(const Ids& ids)
{
    QStringList out;
    for (const auto& id : ids) out << clusterLabel(static_cast<long long>(id));
    return out;
}

/// The cluster id a typed label names ("R14", "r14" or a number; surrounding
/// spaces are ignored), or -1 when @p text is neither.
inline long long clusterIdFromLabel(const QString& text)
{
    return reservedclusters::idFromLabel(text.trimmed().toStdString());
}

#endif // CLUSTERLABEL_H
