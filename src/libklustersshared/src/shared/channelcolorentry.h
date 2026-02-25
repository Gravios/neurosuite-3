/***************************************************************************
 * channelcolorentry.h
 *
 * Per-channel colour/id value type.  This is the "small struct" used by
 * ndmanager, distinct from the ChannelColors container class in
 * channelcolors.h which maps channel ids to colours for neuroscope/klusters.
 *
 * Copyright (C) 2004 Lynn Hazan
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include "libklustersshared_export.h"

#include <QColor>
#include <QString>

/**
 * @brief Holds the three display colours for one channel.
 *
 * Used by ndmanager's reader/writer and by neuroscope's channel-colour
 * display (previously called ChannelDescription in sessionInformation.h).
 */
class KLUSTERSSHARED_EXPORT ChannelColorEntry
{
public:
    ChannelColorEntry()
        : id(0), color(Qt::black), groupColor(Qt::black), spikeGroupColor(Qt::black)
    {}

    ChannelColorEntry(int id_, const QString& colorName)
        : id(id_), groupColor(Qt::black), spikeGroupColor(Qt::black)
    { setColor(colorName); }

    ~ChannelColorEntry() = default;

    void setId(int i)                          { id = i; }
    void setColor(const QString& name)         { color = QColor(name); }
    void setGroupColor(const QString& name)    { groupColor = QColor(name); }
    void setSpikeGroupColor(const QString& name) { spikeGroupColor = QColor(name); }

    int    getId()             const { return id; }
    QColor getColor()          const { return color; }
    QColor getGroupColor()     const { return groupColor; }
    QColor getSpikeGroupColor() const { return spikeGroupColor; }

private:
    int    id;
    QColor color;
    QColor groupColor;
    QColor spikeGroupColor;
};
