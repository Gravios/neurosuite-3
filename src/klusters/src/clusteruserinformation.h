/***************************************************************************
 *   Copyright (C) 2003 by Lynn Hazan                                      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef CLUSTERUSERINFORMATION_H
#define CLUSTERUSERINFORMATION_H

#include <QString>

/**
 * Value type holding user-supplied metadata for a single cluster.
 * Used as the value in QMap<int, ClusterUserInformation>.
 *
 * All fields are public; use designated initialisation or the
 * positional constructor for creation.
 */
struct ClusterUserInformation {
    int     group   {0};
    int     cluster {0};
    QString structure;
    QString type;
    QString ID;
    QString quality;
    QString notes;

    // Default constructor — all fields zero/empty.
    ClusterUserInformation() = default;

    // Positional constructor matching the original API.
    ClusterUserInformation(int pGroup, int pCluster,
                           const QString& pStructure = {},
                           const QString& pType      = {},
                           const QString& pID        = {},
                           const QString& pQuality   = {},
                           const QString& pNotes     = {})
        : group(pGroup), cluster(pCluster)
        , structure(pStructure), type(pType)
        , ID(pID), quality(pQuality), notes(pNotes)
    {}

    // Accessors kept for call-site compatibility.
    [[nodiscard]] int     getGroup()     const noexcept { return group; }
    [[nodiscard]] int     getCluster()   const noexcept { return cluster; }
    [[nodiscard]] QString getStructure() const { return structure; }
    [[nodiscard]] QString getType()      const { return type; }
    [[nodiscard]] QString getId()        const { return ID; }
    [[nodiscard]] QString getQuality()   const { return quality; }
    [[nodiscard]] QString getNotes()     const { return notes; }

    void setGroup(int g)               noexcept { group = g; }
};

#endif // CLUSTERUSERINFORMATION_H
