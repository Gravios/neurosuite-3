/***************************************************************************
 * prefradialmenu.cpp — see prefradialmenu.h.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "prefradialmenu.h"

#include "radialmenu.h"

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace {
constexpr int kPreviewRings = 2;   ///< root ring + one ring out (fits the page)
constexpr int kKindGroup  = static_cast<int>(RadialLayoutNode::Kind::Group);
constexpr int kKindAction = static_cast<int>(RadialLayoutNode::Kind::Action);
constexpr int kKindValue  = static_cast<int>(RadialLayoutNode::Kind::Value);
constexpr int kKindFolder = -1;    ///< a catalogue menu heading (not placeable)

QString valueName(const QString& id)
{
    if (id == QLatin1String("dimX")) return PrefRadialMenu::tr("X axis feature dimension (mouse wheel)");
    if (id == QLatin1String("dimY")) return PrefRadialMenu::tr("Y axis feature dimension (mouse wheel)");
    if (id == QLatin1String("nFeat")) return PrefRadialMenu::tr("Features auto-selected for reclustering, N feat (mouse wheel)");
    return id;
}
QString valuePrefix(const QString& id)
{
    if (id == QLatin1String("dimY")) return QStringLiteral("Y");
    if (id == QLatin1String("nFeat")) return QStringLiteral("N");
    return QStringLiteral("X");
}

// An action's icon, or a transparent one of the same size, so rows without an
// icon line their text up with the rows that have one.
QIcon rowIcon(const QAction* a)
{
    if (a && !a->icon().isNull()) return a->icon();
    static const QIcon blank = [] { QPixmap pm(16, 16); pm.fill(Qt::transparent); return QIcon(pm); }();
    return blank;
}
} // namespace

PrefRadialMenu::PrefRadialMenu(QWidget* parent)
    : QWidget(parent)
{
    auto* top = new QVBoxLayout(this);

    auto* intro = new QLabel(tr(
        "Right-click in a view opens this wheel. The wheel's top-level rows form the root "
        "ring, clockwise from 12 o'clock; a group opens its rows as the next ring out. "
        "Drag rows to reorder them or move them into a group; double-click a group to "
        "rename it, or a wedge label to shorten an action's name on the wheel."), this);
    intro->setWordWrap(true);
    top->addWidget(intro);

    auto* split = new QSplitter(Qt::Horizontal, this);

    // ── Available actions ───────────────────────────────────────────────────
    auto* left = new QWidget(split);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel(tr("Available actions"), left));
    filterEdit = new QLineEdit(left);
    filterEdit->setPlaceholderText(tr("Filter…"));
    filterEdit->setClearButtonEnabled(true);
    leftLayout->addWidget(filterEdit);
    catalogTree = new QTreeWidget(left);
    catalogTree->setHeaderHidden(true);
    catalogTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    leftLayout->addWidget(catalogTree, 1);
    addButton = new QPushButton(tr("Add to Wheel"), left);
    addButton->setToolTip(tr("Add the selected actions after the selected wheel row, or into the selected group"));
    leftLayout->addWidget(addButton);

    // ── Wheel ───────────────────────────────────────────────────────────────
    auto* right = new QWidget(split);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel(tr("Wheel"), right));
    wheelTree = new QTreeWidget(right);
    wheelTree->setColumnCount(2);
    wheelTree->setHeaderLabels({ tr("Wedge"), tr("Wedge label") });
    wheelTree->header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    wheelTree->header()->setSectionResizeMode(LabelColumn, QHeaderView::ResizeToContents);
    wheelTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    wheelTree->setDragDropMode(QAbstractItemView::InternalMove);
    wheelTree->setDefaultDropAction(Qt::MoveAction);
    wheelTree->setEditTriggers(QAbstractItemView::EditKeyPressed);   // + double-click below
    rightLayout->addWidget(wheelTree, 1);
    auto* buttons = new QHBoxLayout;
    groupButton    = new QPushButton(tr("New Group"), right);
    removeButton   = new QPushButton(tr("Remove"), right);
    upButton       = new QPushButton(tr("Up"), right);
    downButton     = new QPushButton(tr("Down"), right);
    defaultsButton = new QPushButton(tr("Restore Default"), right);
    for (QPushButton* b : { groupButton, removeButton, upButton, downButton }) buttons->addWidget(b);
    buttons->addStretch(1);
    buttons->addWidget(defaultsButton);
    rightLayout->addLayout(buttons);

    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    top->addWidget(split, 1);

    statusLabel = new QLabel(this);
    statusLabel->setWordWrap(true);
    top->addWidget(statusLabel);

    // ── Preview ─────────────────────────────────────────────────────────────
    preview = new RadialMenu(this, RadialMenu::Mode::Embedded);
    preview->setDepthLimit(kPreviewRings);
    auto* previewRow = new QHBoxLayout;
    previewRow->addStretch(1);
    previewRow->addWidget(preview);
    previewRow->addStretch(1);
    top->addLayout(previewRow);

    // ── Wiring ──────────────────────────────────────────────────────────────
    connect(filterEdit, &QLineEdit::textChanged, this, &PrefRadialMenu::applyFilter);
    connect(addButton, &QPushButton::clicked, this, &PrefRadialMenu::addSelectedCatalogItems);
    connect(catalogTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
        if (it && it->data(0, KindRole).toInt() != kKindFolder) addSelectedCatalogItems();
    });
    connect(catalogTree, &QTreeWidget::itemSelectionChanged, this, &PrefRadialMenu::updateButtons);
    connect(groupButton,    &QPushButton::clicked, this, &PrefRadialMenu::addGroup);
    connect(removeButton,   &QPushButton::clicked, this, &PrefRadialMenu::removeSelected);
    connect(upButton,       &QPushButton::clicked, this, [this]{ moveSelected(-1); });
    connect(downButton,     &QPushButton::clicked, this, [this]{ moveSelected(+1); });
    connect(defaultsButton, &QPushButton::clicked, this, &PrefRadialMenu::restoreDefaults);
    connect(wheelTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
        if (!it) return;
        // A group is renamed in its own column; an action / value gets a wedge label.
        wheelTree->editItem(it, it->data(0, KindRole).toInt() == kKindGroup ? NameColumn : LabelColumn);
    });
    connect(wheelTree, &QTreeWidget::itemSelectionChanged, this, [this] {
        updateButtons();
        // Open the selected row's group (or its parent group) in the preview.
        QList<QAction*> path;
        QTreeWidgetItem* it = wheelTree->currentItem();
        if (it && it->data(0, KindRole).toInt() != kKindGroup) it = it->parent();
        for (; it; it = it->parent())
            if (QAction* a = previewGroupAction.value(it)) path.prepend(a);
        preview->setOpenPath(path);
    });
    QAbstractItemModel* model = wheelTree->model();
    connect(model, &QAbstractItemModel::rowsInserted, this, &PrefRadialMenu::edited);
    connect(model, &QAbstractItemModel::rowsRemoved,  this, &PrefRadialMenu::edited);
    connect(model, &QAbstractItemModel::rowsMoved,    this, &PrefRadialMenu::edited);
    connect(model, &QAbstractItemModel::dataChanged,  this, &PrefRadialMenu::edited);

    updateButtons();
}

// --------------------------------------------------------------------------
// catalogue
// --------------------------------------------------------------------------

void PrefRadialMenu::setCatalog(const QList<ActionCatalogEntry>& entries)
{
    catalog = entries;
    catalogIndex.clear();
    for (int i = 0; i < catalog.size(); ++i) {
        const ActionCatalogEntry& e = catalog.at(i);
        catalogIndex.insert(e.id, i);
        if (e.action) catalogIndex.insert(actionid::menuPathId(e.path, e.action->text()), i);
    }
    fillCatalog();
}

void PrefRadialMenu::fillCatalog()
{
    catalogTree->clear();
    QHash<QString, QTreeWidgetItem*> folders;   // "Actions/Sort Clusters" -> heading
    auto folderFor = [&](const QStringList& path) -> QTreeWidgetItem* {
        QTreeWidgetItem* parent = nullptr;
        QString key;
        for (const QString& title : path) {
            key += QLatin1Char('/') + title;
            QTreeWidgetItem* f = folders.value(key);
            if (!f) {
                f = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(catalogTree);
                f->setText(0, title);
                f->setData(0, KindRole, kKindFolder);
                f->setFlags(Qt::ItemIsEnabled);
                QFont bold = f->font(0); bold.setBold(true); f->setFont(0, bold);
                folders.insert(key, f);
            }
            parent = f;
        }
        return parent;
    };
    for (const ActionCatalogEntry& e : catalog) {
        QTreeWidgetItem* f = folderFor(e.path);
        auto* it = f ? new QTreeWidgetItem(f) : new QTreeWidgetItem(catalogTree);
        it->setText(0, e.text);
        it->setIcon(0, rowIcon(e.action));
        it->setToolTip(0, e.id);
        it->setData(0, KindRole, kKindAction);
        it->setData(0, IdRole, e.id);
    }
    QTreeWidgetItem* axes = folderFor({ tr("Features (mouse wheel)") });
    for (const char* id : { "dimX", "dimY", "nFeat" }) {
        auto* it = new QTreeWidgetItem(axes);
        it->setText(0, valueName(QString::fromLatin1(id)));
        it->setIcon(0, rowIcon(nullptr));
        it->setData(0, KindRole, kKindValue);
        it->setData(0, IdRole, QString::fromLatin1(id));
    }
    applyFilter(filterEdit->text());
}

void PrefRadialMenu::applyFilter(const QString& text)
{
    const QString needle = text.trimmed();
    std::function<bool(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* it) -> bool {
        if (it->data(0, KindRole).toInt() == kKindFolder) {
            bool any = false;
            for (int i = 0; i < it->childCount(); ++i) any = visit(it->child(i)) || any;
            it->setHidden(!any);
            it->setExpanded(!needle.isEmpty() && any);
            return any;
        }
        const bool match = needle.isEmpty()
            || it->text(0).contains(needle, Qt::CaseInsensitive)
            || it->data(0, IdRole).toString().contains(needle, Qt::CaseInsensitive);
        it->setHidden(!match);
        return match;
    };
    for (int i = 0; i < catalogTree->topLevelItemCount(); ++i) visit(catalogTree->topLevelItem(i));
}

void PrefRadialMenu::addSelectedCatalogItems()
{
    const QList<QTreeWidgetItem*> sel = catalogTree->selectedItems();
    for (const QTreeWidgetItem* c : sel) {
        const int kind = c->data(0, KindRole).toInt();
        if (kind == kKindFolder) continue;
        const QString id = c->data(0, IdRole).toString();
        insertIntoWheel(makeWheelItem(kind == kKindValue ? RadialLayoutNode::value(id)
                                                         : RadialLayoutNode::action(id)));
    }
}

QString PrefRadialMenu::catalogText(const QString& id) const
{
    const int i = catalogIndex.value(id, -1);
    return i >= 0 ? catalog.at(i).text : id;
}

// --------------------------------------------------------------------------
// wheel tree
// --------------------------------------------------------------------------

QTreeWidgetItem* PrefRadialMenu::makeWheelItem(const RadialLayoutNode& node) const
{
    auto* it = new QTreeWidgetItem;
    it->setData(0, KindRole, static_cast<int>(node.kind));
    it->setData(0, IdRole, node.id);
    switch (node.kind) {
    case RadialLayoutNode::Kind::Group:
        it->setText(NameColumn, node.label);
        for (const RadialLayoutNode& c : node.children) it->addChild(makeWheelItem(c));
        break;
    case RadialLayoutNode::Kind::Action:
        it->setText(NameColumn, catalogText(node.id));
        it->setText(LabelColumn, node.label);
        break;
    case RadialLayoutNode::Kind::Value:
        it->setText(NameColumn, valueName(node.id));
        it->setText(LabelColumn, node.label);
        break;
    }
    decorate(it);
    return it;
}

void PrefRadialMenu::decorate(QTreeWidgetItem* it) const
{
    const int kind = it->data(0, KindRole).toInt();
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled
                    | Qt::ItemIsEditable;
    if (kind == kKindGroup) {
        f |= Qt::ItemIsDropEnabled;            // only a group takes children
        it->setIcon(NameColumn, style()->standardIcon(QStyle::SP_DirIcon));
        QFont bold = it->font(NameColumn); bold.setBold(true); it->setFont(NameColumn, bold);
        it->setToolTip(NameColumn, tr("A group: opens its rows as the next ring out"));
    } else if (kind == kKindAction) {
        const QString id = it->data(0, IdRole).toString();
        const int i = catalogIndex.value(id, -1);
        it->setIcon(NameColumn, rowIcon(i >= 0 ? catalog.at(i).action : nullptr));
        if (i >= 0) {
            it->setToolTip(NameColumn, id);
        } else {
            it->setForeground(NameColumn, QColor(200, 40, 40));
            it->setToolTip(NameColumn, tr("%1 — no such action in this version; not shown on the wheel").arg(id));
        }
        it->setToolTip(LabelColumn, tr("Short label on the wheel (empty: the action's own text)"));
    } else {
        it->setIcon(NameColumn, rowIcon(nullptr));
        it->setToolTip(NameColumn, tr("Hover this wedge and turn the mouse wheel to step the axis"));
        it->setToolTip(LabelColumn, tr("Prefix before the number (empty: %1)").arg(valuePrefix(it->data(0, IdRole).toString())));
    }
    it->setFlags(f);
}

void PrefRadialMenu::setLayout(const RadialLayout& l)
{
    loading = true;
    wheelTree->clear();
    for (const RadialLayoutNode& n : l) wheelTree->addTopLevelItem(makeWheelItem(n));
    wheelTree->expandAll();
    loading = false;
    refresh();
}

RadialLayout PrefRadialMenu::readItems(const QTreeWidgetItem* parent) const
{
    RadialLayout out;
    for (int i = 0; i < parent->childCount(); ++i) {
        const QTreeWidgetItem* it = parent->child(i);
        const QString id = it->data(0, IdRole).toString();
        switch (it->data(0, KindRole).toInt()) {
        case kKindGroup: {
            QString name = it->text(NameColumn).trimmed();
            if (name.isEmpty()) name = tr("Group");
            out.append(RadialLayoutNode::group(name, readItems(it)));
            break;
        }
        case kKindAction:
            out.append(RadialLayoutNode::action(id, it->text(LabelColumn).trimmed()));
            break;
        case kKindValue:
            out.append(RadialLayoutNode::value(id, it->text(LabelColumn).trimmed()));
            break;
        default: break;
        }
    }
    return out;
}

RadialLayout PrefRadialMenu::layout() const
{
    return readItems(wheelTree->invisibleRootItem());
}

void PrefRadialMenu::restoreDefaults()
{
    setLayout(radiallayout::defaults());
    Q_EMIT changed();
}

void PrefRadialMenu::insertIntoWheel(QTreeWidgetItem* item)
{
    QTreeWidgetItem* cur = wheelTree->currentItem();
    if (cur && cur->data(0, KindRole).toInt() == kKindGroup) {
        cur->addChild(item);                       // into the selected group
        cur->setExpanded(true);
    } else if (cur) {
        QTreeWidgetItem* parent = cur->parent() ? cur->parent() : wheelTree->invisibleRootItem();
        parent->insertChild(parent->indexOfChild(cur) + 1, item);   // after the selected row
    } else {
        wheelTree->addTopLevelItem(item);
    }
    wheelTree->setCurrentItem(item);
}

void PrefRadialMenu::addGroup()
{
    QTreeWidgetItem* it = makeWheelItem(RadialLayoutNode::group(tr("New group"), {}));
    insertIntoWheel(it);
    wheelTree->editItem(it, NameColumn);
}

void PrefRadialMenu::removeSelected()
{
    // Remove only the outermost selected rows (a selected group takes its rows along).
    const QList<QTreeWidgetItem*> sel = wheelTree->selectedItems();
    const QSet<QTreeWidgetItem*> selected(sel.begin(), sel.end());
    for (QTreeWidgetItem* it : sel) {
        bool ancestorSelected = false;
        for (QTreeWidgetItem* p = it->parent(); p; p = p->parent())
            if (selected.contains(p)) { ancestorSelected = true; break; }
        if (!ancestorSelected) delete it;
    }
}

void PrefRadialMenu::moveSelected(int delta)
{
    QTreeWidgetItem* it = wheelTree->currentItem();
    if (!it) return;
    QTreeWidgetItem* parent = it->parent() ? it->parent() : wheelTree->invisibleRootItem();
    const int from = parent->indexOfChild(it);
    const int to = from + delta;
    if (to < 0 || to >= parent->childCount()) return;
    const bool expanded = it->isExpanded();
    parent->takeChild(from);
    parent->insertChild(to, it);
    it->setExpanded(expanded);
    wheelTree->setCurrentItem(it);
}

void PrefRadialMenu::edited()
{
    if (loading) return;
    Q_EMIT changed();
    if (refreshPending) return;
    refreshPending = true;
    // Coalesce: a drag-move is a remove + an insert, an edit a few dataChanged().
    QTimer::singleShot(0, this, [this] { refreshPending = false; refresh(); });
}

void PrefRadialMenu::refresh()
{
    // Rebuild the preview's menu tree from the rows.  Actions are the app's own
    // QActions (live enabled state, icons); groups and the value wedges are owned by
    // the preview tree.  addAction(QAction*) never takes ownership.
    delete previewTree;
    previewTree = new QMenu(this);
    previewGroupAction.clear();
    int unavailable = 0;
    std::function<void(QMenu*, const QTreeWidgetItem*)> fill =
        [&](QMenu* m, const QTreeWidgetItem* parent) {
        for (int i = 0; i < parent->childCount(); ++i) {
            const QTreeWidgetItem* it = parent->child(i);
            const QString id = it->data(0, IdRole).toString();
            const QString label = it->text(LabelColumn).trimmed();
            switch (it->data(0, KindRole).toInt()) {
            case kKindGroup: {
                QString name = it->text(NameColumn).trimmed();
                if (name.isEmpty()) name = tr("Group");
                QMenu* sub = m->addMenu(name);
                previewGroupAction.insert(it, sub->menuAction());
                fill(sub, it);
                break;
            }
            case kKindAction: {
                const int ci = catalogIndex.value(id, -1);
                QAction* a = ci >= 0 ? catalog.at(ci).action : nullptr;
                if (!a) { ++unavailable; break; }
                m->addAction(a);
                preview->setLabel(a, label);
                break;
            }
            case kKindValue: {
                QAction* a = m->addAction(QString());
                preview->setAdjustable(a);
                preview->setLabel(a, tr("%1: 1").arg(label.isEmpty() ? valuePrefix(id) : label));
                break;
            }
            default: break;
            }
        }
    };
    fill(previewTree, wheelTree->invisibleRootItem());
    preview->setMenu(previewTree);
    Q_EMIT wheelTree->itemSelectionChanged();   // re-open the selected group's ring

    // What the user should know about this layout.
    const RadialLayout l = layout();
    QStringList notes;
    const int rings = radiallayout::ringsNeeded(l);
    if (l.isEmpty())
        notes << tr("The wheel is empty: right-click will open nothing.");
    else
        notes << tr("Root ring: %n wedge(s).", nullptr, wheelTree->topLevelItemCount());
    if (wheelTree->topLevelItemCount() > 12)
        notes << tr("More than about 12 root wedges are hard to hit by direction.");
    if (rings > radiallayout::kMaxRings)
        notes << QStringLiteral("<span style='color:#c82828'>%1</span>")
                     .arg(tr("Groups nested more than %1 rings deep do not open.")
                              .arg(radiallayout::kMaxRings));
    if (unavailable > 0)
        notes << QStringLiteral("<span style='color:#c82828'>%1</span>")
                     .arg(tr("%n row(s) name an action that does not exist and are skipped.",
                             nullptr, unavailable));
    if (rings > kPreviewRings)
        notes << tr("The preview shows two rings; deeper groups open in the real menu.");
    statusLabel->setText(notes.join(QStringLiteral("  ")));
    updateButtons();
}

void PrefRadialMenu::updateButtons()
{
    bool placeable = false;
    for (const QTreeWidgetItem* c : catalogTree->selectedItems())
        if (c->data(0, KindRole).toInt() != kKindFolder) { placeable = true; break; }
    addButton->setEnabled(placeable);
    const bool any = !wheelTree->selectedItems().isEmpty();
    removeButton->setEnabled(any);
    QTreeWidgetItem* cur = wheelTree->currentItem();
    const QTreeWidgetItem* parent = cur ? (cur->parent() ? cur->parent() : wheelTree->invisibleRootItem())
                                        : nullptr;
    upButton->setEnabled(cur && parent->indexOfChild(cur) > 0);
    downButton->setEnabled(cur && parent->indexOfChild(cur) < parent->childCount() - 1);
}
