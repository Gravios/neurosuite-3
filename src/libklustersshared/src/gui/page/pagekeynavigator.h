/***************************************************************************
 * pagekeynavigator.h -- a paged dialog worked from the keyboard, in two levels.
 *
 *   The list level: the page list and the dialog's buttons.
 *     Up / Down      another page
 *     Enter          into the page shown; on a group without settings, open
 *                    the group and go to its first page
 *     Tab            list -> buttons -> list (the page is not on this round)
 *     Esc            closes the dialog, as before
 *   Ctrl+Enter accepts the dialog from either level (QExtendDialog's own).
 *
 *   The page level: the settings of the page shown.
 *     Tab / Shift+Tab   next / previous setting, round and round within the
 *                       page; the page scrolls to it
 *     Space             toggles a check box (Qt's own)
 *     Enter             toggles a check box, presses a button, opens a combo
 *                       box, commits a number; it never presses OK from here
 *     Esc               back to the page list
 *
 * A key editor (QKeySequenceEdit and the chord editors of the Input page)
 * keeps Enter and Esc: they are keys it can record.  Tab leaves it.
 *
 * Attach one to a QPageDialog; it lives as long as the dialog.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef PAGEKEYNAVIGATOR_H
#define PAGEKEYNAVIGATOR_H

#include <QList>
#include <QObject>

#include "libklustersshared_export.h"

class QAbstractItemView;
class QPageDialog;
class QWidget;

class KLUSTERSSHARED_EXPORT PageKeyNavigator : public QObject {
    Q_OBJECT
  public:
    /// @p withHint: a line under the pages that names the keys.
    explicit PageKeyNavigator(QPageDialog* dialog, bool withHint = true);

    /// The page list (null before the dialog has one).
    QAbstractItemView* pageList() const;
    /// The settings of the page shown that Tab stops at, in order.
    QList<QWidget*> pageStops() const;
    /// The page list and the dialog's buttons, in order.
    QList<QWidget*> listStops() const;
    /// True when @p w is in the page shown.
    bool inPage(const QWidget* w) const;

    /// Focus the first setting of the page shown.  False when it has none.
    bool enterPage();
    /// Focus the page list.
    void leavePage();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QWidget* pageRoot() const;
    bool handleKey(QWidget* focus, int key, Qt::KeyboardModifiers mods);
    void step(const QList<QWidget*>& stops, QWidget* from, int by);

    QPageDialog* dialog;
};

#endif // PAGEKEYNAVIGATOR_H
