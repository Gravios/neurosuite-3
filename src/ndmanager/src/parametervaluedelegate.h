/* parametervaluedelegate.h -- the editor for a parameter table's Value cells.
 *
 * Picks the editor from the row's declared type (parametertype.h): a list for
 * bool and choice parameters, a line edit that only takes a number for int and
 * float ones, the plain line edit otherwise.  Never returns a null editor (Qt 6
 * crashes on one); a cell that must not be edited is made read-only through
 * its item flags instead.
 */
#ifndef NDMANAGER_PARAMETERVALUEDELEGATE_H
#define NDMANAGER_PARAMETERVALUEDELEGATE_H

#include <QStyledItemDelegate>

class ParameterValueDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override;
};

#endif // NDMANAGER_PARAMETERVALUEDELEGATE_H
