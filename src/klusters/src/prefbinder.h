/***************************************************************************
 * prefbinder.h -- the Preferences pages and the preference table, joined.
 *
 * For every row of Configuration::prefTable() that names a control
 * (PrefSpec::widget) and whose control is under the root given, the binder
 *   load()             shows the configuration's value in the control,
 *   apply()            gives the control's value to the configuration,
 *   restoreDefaults()  shows the default,
 * and emits changed() when the user alters one.  A page then needs no code
 * for such a setting: the control in its .ui and the name in the row.
 *
 * What a control holds:
 *   QCheckBox (any checkable button)   a flag
 *   QSpinBox / QDoubleSpinBox           a whole / a real number
 *   QComboBox                           the index of the entry (a number), or for a
 *                                       flag its first entry = off, second = on
 *                                       with PrefSpec::choices, the value its entry stands for
 *   QLineEdit                           a text; or a number typed in (what cannot be
 *                                       read as one is the default)
 *   QColorButton                        a colour
 *   radio buttons ("a|b|c")             the index of the one checked
 * Anything else stays the page's own: a setting whose control does more when it
 * is set (the recluster engine and its chunking), or one built in code without a name.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef PREFBINDER_H
#define PREFBINDER_H

#include <QList>
#include <QObject>
#include <QStringList>

#include "prefspec.h"

class QWidget;

class PrefBinder : public QObject {
    Q_OBJECT
  public:
    /// Binds the rows whose control is @p root or under it.
    explicit PrefBinder(QWidget* root, QObject* parent = nullptr);

    void load(const Configuration& config);
    void apply(Configuration& config) const;
    void restoreDefaults();

    /// The keys bound, and the rows that name a control @p root does not have (a slip in a name).
    QStringList boundKeys() const;
    QStringList unboundRows() const { return missing; }

  Q_SIGNALS:
    void changed();

  private:
    struct Binding { PrefSpec spec; QList<QWidget*> widgets; };   // several: radio buttons
    QList<Binding> bindings;
    QStringList    missing;
};

#endif // PREFBINDER_H
