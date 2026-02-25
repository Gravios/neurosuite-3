/***************************************************************************
 * neuroscopevideoinfo.h
 *
 * Moved from ndmanager/src to libklustersshared.
 *
 * Copyright (C) 2004 Lynn Hazan
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include "libklustersshared_export.h"

#include <QString>

/**
 * @brief Stores the neuroscope/video display settings from a parameter file.
 */
class KLUSTERSSHARED_EXPORT NeuroscopeVideoInfo
{
public:
    NeuroscopeVideoInfo() : flip(0), rotation(0), trajectory(0) {}
    ~NeuroscopeVideoInfo() = default;

    int     getRotation()        const { return rotation; }
    int     getFlip()            const { return flip; }
    int     getTrajectory()      const { return trajectory; }
    QString getBackgroundImage() const { return backgroundImage; }

    void setRotation(int a)                    { rotation = a; }
    void setFlip(int f)                        { flip = f; }
    void setTrajectory(int t)                  { trajectory = t; }
    void setBackgroundImage(const QString& bg) { backgroundImage = bg; }

private:
    int     flip;
    int     rotation;
    int     trajectory;
    QString backgroundImage;
};
