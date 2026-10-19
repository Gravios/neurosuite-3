/* addparameterdialog.cpp -- see addparameterdialog.h.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "addparameterdialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
enum Column { NameColumn = 0, ValueColumn = 1, DescriptionColumn = 2 };
const int kIndexRole = Qt::UserRole + 1;

/** The first sentence of @p help (its first paragraph, cut at ". "). */
QString firstSentence(const QString& help)
{
    QString s = help.section(QStringLiteral("\n\n"), 0, 0).simplified();
    const int dot = s.indexOf(QStringLiteral(". "));
    if (dot > 0) s = s.left(dot + 1);
    if (s.size() > 140) s = s.left(137) + QStringLiteral("...");
    return s;
}
}  // namespace

QString AddParameterDialog::helpToolTip(const QString& help)
{
    if (help.isEmpty()) return QString();
    QStringList paras;
    for (const QString& p : help.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts))
        paras << QStringLiteral("<p>%1</p>").arg(p.toHtmlEscaped());
    return QStringLiteral("<qt>%1</qt>").arg(paras.join(QString()));
}

AddParameterDialog::AddParameterDialog(const QString& program,
                                       const QList<ParameterCatalogEntry>& available,
                                       QWidget* parent)
    : QDialog(parent), entries(available)
{
    setWindowTitle(tr("Add Parameters to %1").arg(program));
    QVBoxLayout* layout = new QVBoxLayout(this);

    QLabel* intro = new QLabel(this);
    intro->setWordWrap(true);
    if (entries.isEmpty())
        intro->setText(tr("The page already lists every parameter %1 reads. "
                          "<b>Other Name…</b> adds a row to type a name into.").arg(program));
    else
        intro->setText(tr("Parameters %1 reads that this page does not list. A parameter the "
                          "template sets is added with the template's value; one the template only "
                          "mentions is added empty, which means the plugin's default.").arg(program));
    layout->addWidget(intro);

    filterEdit = new QLineEdit(this);
    filterEdit->setPlaceholderText(tr("Filter by name or description"));
    filterEdit->setClearButtonEnabled(true);
    layout->addWidget(filterEdit);

    list = new QTreeWidget(this);
    list->setColumnCount(3);
    list->setHeaderLabels({tr("Parameter"), tr("Template value"), tr("Description")});
    list->setRootIsDecorated(false);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setAllColumnsShowFocus(true);
    list->header()->setSectionResizeMode(NameColumn, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(ValueColumn, QHeaderView::ResizeToContents);
    list->header()->setStretchLastSection(true);
    for (int i = 0; i < entries.size(); ++i) {
        const ParameterCatalogEntry& e = entries.at(i);
        QTreeWidgetItem* item = new QTreeWidgetItem(list);
        item->setText(NameColumn, e.name);
        if (e.inTemplate) {
            item->setText(ValueColumn, e.value);
        } else {
            item->setText(ValueColumn, tr("(not set)"));
            item->setToolTip(ValueColumn, e.value.isEmpty()
                ? tr("The template leaves this parameter out; it is added empty (the plugin's default).")
                : tr("The template leaves this parameter out (its commented-out line has %1); "
                     "it is added empty (the plugin's default).").arg(e.value));
            QFont f = item->font(ValueColumn);
            f.setItalic(true);
            item->setFont(ValueColumn, f);
        }
        item->setText(DescriptionColumn, firstSentence(e.help));
        item->setToolTip(NameColumn, helpToolTip(e.help));
        item->setToolTip(DescriptionColumn, helpToolTip(e.help));
        item->setData(NameColumn, kIndexRole, i);
    }
    layout->addWidget(list, 3);

    helpView = new QTextBrowser(this);
    helpView->setOpenLinks(false);
    layout->addWidget(helpView, 2);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    addButton = buttons->addButton(tr("Add"), QDialogButtonBox::AcceptRole);
    QPushButton* otherButton = buttons->addButton(tr("Other Name…"), QDialogButtonBox::ActionRole);
    otherButton->setToolTip(tr("Add an empty row and type the parameter's name"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(otherButton, &QPushButton::clicked, this, [this] {
        otherName = true;
        accept();
    });
    connect(list, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (!selected().isEmpty()) accept();
    });
    connect(list, &QTreeWidget::itemSelectionChanged, this, &AddParameterDialog::currentChanged);
    connect(list, &QTreeWidget::currentItemChanged, this, &AddParameterDialog::currentChanged);
    connect(filterEdit, &QLineEdit::textChanged, this, &AddParameterDialog::filter);

    if (QTreeWidgetItem* first = list->topLevelItem(0)) {
        list->setCurrentItem(first);
        first->setSelected(true);
    }
    currentChanged();
    addButton->setDefault(true);
    filterEdit->setFocus();
    resize(760, 520);
}

QList<ParameterCatalogEntry> AddParameterDialog::selected() const
{
    QList<ParameterCatalogEntry> out;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* item = list->topLevelItem(i);
        if (item->isSelected() && !item->isHidden())
            out << entries.at(item->data(NameColumn, kIndexRole).toInt());
    }
    return out;
}

void AddParameterDialog::filter(const QString& text)
{
    const QString t = text.trimmed();
    QTreeWidgetItem* firstShown = nullptr;
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = list->topLevelItem(i);
        const ParameterCatalogEntry& e = entries.at(item->data(NameColumn, kIndexRole).toInt());
        const bool show = t.isEmpty() || e.name.contains(t, Qt::CaseInsensitive)
                          || e.help.contains(t, Qt::CaseInsensitive);
        item->setHidden(!show);
        if (show && !firstShown) firstShown = item;
    }
    if (list->currentItem() == nullptr || list->currentItem()->isHidden()) {
        list->clearSelection();
        if (firstShown) {
            list->setCurrentItem(firstShown);
            firstShown->setSelected(true);
        }
    }
    currentChanged();
}

void AddParameterDialog::currentChanged()
{
    addButton->setEnabled(!selected().isEmpty());
    const QTreeWidgetItem* item = list->currentItem();
    if (!item || item->isHidden()) {
        helpView->clear();
        return;
    }
    const ParameterCatalogEntry& e = entries.at(item->data(NameColumn, kIndexRole).toInt());
    QString html = QStringLiteral("<p><b>%1</b></p>").arg(e.name.toHtmlEscaped());
    if (e.help.isEmpty())
        html += tr("<p><i>No description in the plugin's help.</i></p>");
    else
        for (const QString& p : e.help.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts))
            html += QStringLiteral("<p>%1</p>").arg(p.toHtmlEscaped());
    helpView->setHtml(html);
}
