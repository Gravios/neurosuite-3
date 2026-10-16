/* parametervaluedelegate.cpp -- see parametervaluedelegate.h. */
#include "parametervaluedelegate.h"
#include "parametertype.h"

#include <QComboBox>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

namespace {

ParameterType typeOf(const QModelIndex& index)
{
    return ParameterType::parse(
        index.siblingAtColumn(0).data(ParameterType::TypeRole).toString());
}

/** The words a list editor offers for @p t, with an empty entry first for an
 *  optional parameter left unset and the current text kept if it is not one
 *  of them (an old file may hold a value the description no longer lists). */
QStringList listWords(const ParameterType& t, const QString& current)
{
    QStringList words = t.kind == ParameterType::Bool ? ParameterType::boolWords(current)
                                                      : t.choices;
    if (!current.isEmpty() && !words.contains(current)) words.prepend(current);
    words.prepend(QString());
    return words;
}

} // namespace

QWidget* ParameterValueDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option,
                                              const QModelIndex& index) const
{
    const ParameterType t = typeOf(index);
    switch (t.kind) {
    case ParameterType::Bool:
    case ParameterType::Choice: {
        auto* combo = new QComboBox(parent);
        combo->addItems(listWords(t, index.data(Qt::EditRole).toString()));
        // Picking an entry commits it: the list closes and Tab moves on.
        connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, combo]() {
            emit const_cast<ParameterValueDelegate*>(this)->commitData(combo);
        });
        return combo;
    }
    case ParameterType::Int: {
        auto* edit = new QLineEdit(parent);
        edit->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("([+-]?\\d+)?")), edit));
        return edit;
    }
    case ParameterType::Float: {
        // QDoubleValidator follows the locale's decimal separator; plugins
        // read a C-locale number, so the pattern is spelled out.  Empty is
        // acceptable (unset); a partial "-" or "1e" is not, so it is never
        // committed (the delegate commits only acceptable input).
        auto* edit = new QLineEdit(parent);
        edit->setValidator(new QRegularExpressionValidator(
            QRegularExpression(QStringLiteral("([+-]?(\\d+\\.?\\d*|\\.\\d+)([eE][+-]?\\d+)?)?")),
            edit));
        return edit;
    }
    case ParameterType::Text:
        break;
    }
    return QStyledItemDelegate::createEditor(parent, option, index);
}

void ParameterValueDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        const int i = combo->findText(index.data(Qt::EditRole).toString());
        combo->setCurrentIndex(i >= 0 ? i : 0);
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void ParameterValueDelegate::setModelData(QWidget* editor, QAbstractItemModel* model,
                                          const QModelIndex& index) const
{
    if (auto* combo = qobject_cast<QComboBox*>(editor)) {
        if (model->data(index, Qt::EditRole).toString() != combo->currentText())
            model->setData(index, combo->currentText(), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}
