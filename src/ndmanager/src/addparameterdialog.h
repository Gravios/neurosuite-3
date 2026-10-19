/* addparameterdialog.h -- the Add button of a plugin's parameter page: picks
 * parameters the plugin reads (ParameterCatalog) that the page does not list
 * yet, or asks for a parameter of another name.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef NDMANAGER_ADDPARAMETERDIALOG_H
#define NDMANAGER_ADDPARAMETERDIALOG_H

#include "parametercatalog.h"

#include <QDialog>
#include <QList>

class QLineEdit;
class QPushButton;
class QTextBrowser;
class QTreeWidget;

class AddParameterDialog : public QDialog
{
    Q_OBJECT
public:
    /** @p available: the parameters to offer (the page's are left out by the caller). */
    AddParameterDialog(const QString& program, const QList<ParameterCatalogEntry>& available,
                       QWidget* parent = nullptr);

    /** The chosen parameters, in the list's order. */
    QList<ParameterCatalogEntry> selected() const;
    /** True when the dialog closed through "Other Name…": add an empty row. */
    bool otherNameRequested() const { return otherName; }

    /** Text for a tooltip: @p help as wrapped paragraphs (rich text, so Qt wraps it). */
    static QString helpToolTip(const QString& help);

private:
    void filter(const QString& text);
    void currentChanged();

    QList<ParameterCatalogEntry> entries;
    QLineEdit* filterEdit;
    QTreeWidget* list;
    QTextBrowser* helpView;
    QPushButton* addButton;
    bool otherName = false;
};

#endif
