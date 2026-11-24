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
#include <QVariantList>

#include <functional>

class Configuration;

struct PrefSpec {
    QString  group;      ///< the group of the settings file ("General", "clusterView", "waveformView")
    QString  key;        ///< the key in that group; never changes once released
    QVariant def;        ///< the value when the key is absent, and what Defaults restores
    std::function<QVariant(const Configuration&)>      get;   ///< the value write() stores
    std::function<void(Configuration&, const QVariant&)> set; ///< takes a stored value, within the preference's limits
    /// The object name of the control that shows it in Preferences (a check box, a spin
    /// box, a combo box, a line edit of a page's .ui).  PrefBinder loads, applies and
    /// resets every row that names one; empty: the page does it by hand, or no page shows it.
    QString  widget;

    /// For a combo box whose entries stand for these values, in order (0, 2, 4 for "off, x2, x4").
    QVariantList choices;

    /// The row, shown by the control named @p objectName.  Radio buttons: their names in
    /// the order of the values 0, 1, 2…, separated by '|'.
    PrefSpec on(const char* objectName) const { PrefSpec s = *this; s.widget = QLatin1String(objectName); return s; }
    /// The row, shown by a combo box whose entries stand for @p values.
    PrefSpec on(const char* objectName, const QVariantList& values) const
    { PrefSpec s = on(objectName); s.choices = values; return s; }
};

#endif // PREFSPEC_H
