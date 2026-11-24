/***************************************************************************
 * prefspec.h -- one preference, described once.
 *
 * A row of Configuration::prefTable() (configuration_prefs.cpp): the key and
 * group it has in the settings file, its default, and how its value is taken
 * from and given to the Configuration.  read(), write() and the default
 * getters are loops and look-ups over the table, so a preference's default
 * and limits are written in one place.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef PREFSPEC_H
#define PREFSPEC_H

#include <QString>
#include <QVariant>

#include <functional>

class Configuration;

struct PrefSpec {
    QString  group;      ///< the group of the settings file ("General", "clusterView", "waveformView")
    QString  key;        ///< the key in that group; never changes once released
    QVariant def;        ///< the value when the key is absent, and what Defaults restores
    std::function<QVariant(const Configuration&)>      get;   ///< the value write() stores
    std::function<void(Configuration&, const QVariant&)> set; ///< takes a stored value, within the preference's limits
};

#endif // PREFSPEC_H
