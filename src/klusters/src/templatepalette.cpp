// templatepalette.cpp — see templatepalette.h.

#include "templatepalette.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QPixmap>
#include <QIcon>
#include <QDate>

TemplatePalette::TemplatePalette(QWidget* parent)
    : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(2, 2, 2, 2);
    v->setSpacing(2);

    header_ = new QLabel(tr("Template classes"), this);
    v->addWidget(header_);

    list_ = new QListWidget(this);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);   // `s` can pin several
    list_->setToolTip(tr("The template classes (.eap columns).  Select one to make it the "
                         "active/primary class the Shift+E lineage overlay edits; press s to "
                         "pin the selected class(es) as the oblique basis.  New builds a class "
                         "from the shown clusters."));
    v->addWidget(list_, 1);

    auto* btns = new QHBoxLayout;
    newBtn_ = new QPushButton(tr("New"), this);
    delBtn_ = new QPushButton(tr("Delete"), this);
    newBtn_->setToolTip(tr("Create a template class from the currently shown clusters (made primary)."));
    delBtn_->setToolTip(tr("Tombstone the selected class (its id is never reused)."));
    btns->addWidget(newBtn_);
    btns->addWidget(delBtn_);
    v->addLayout(btns);

    scaleAbs_ = new QCheckBox(tr("Absolute waveform scale"), this);
    scaleAbs_->setToolTip(tr("Template preview in the waveform view: checked = absolute (data gain, "
                             "matches the clusters), unchecked = best-fit (each template's peak fills a channel)."));
    scaleAbs_->setChecked(true);      // default: absolute, so the template matches the clusters' scale
    v->addWidget(scaleAbs_);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int){ onRowChanged(); });
    connect(newBtn_, &QPushButton::clicked, this, &TemplatePalette::onNewClicked);
    connect(delBtn_, &QPushButton::clicked, this, &TemplatePalette::onDeleteClicked);
    connect(scaleAbs_, &QCheckBox::toggled, this, [this](bool on){ Q_EMIT scaleAbsoluteToggled(on); });

    setFocusPolicy(Qt::StrongFocus);
    setFocusProxy(list_);               // Tab ring focuses the class list directly
    updateButtons();
}

QColor TemplatePalette::classColor(int classId)
{
    const int h = ((classId * 47) % 360 + 360) % 360;   // matches ClusterView::lineageClassColor
    return QColor::fromHsv(h, 200, 255);
}

void TemplatePalette::reload(const std::string& base, int group, const std::string& stage, int64_t nSpikes)
{
    base_ = base; group_ = group; stage_ = stage; nSpikes_ = nSpikes;
    loaded_ = (!base.empty() && nSpikes >= 0) && classStore_.load(base, group, stage, nSpikes, 128);
    rebuild();
}

void TemplatePalette::refreshFromDisk()
{
    if (base_.empty()) return;
    loaded_ = classStore_.load(base_, group_, stage_, nSpikes_, 128);
    rebuild();
}

void TemplatePalette::clearClasses()
{
    loaded_ = false;
    base_.clear();
    rebuild();
}

int TemplatePalette::selectedClass() const
{
    const QListWidgetItem* it = list_ ? list_->currentItem() : nullptr;
    return it ? it->data(Qt::UserRole).toInt() : -1;
}

void TemplatePalette::setFocusToList()
{
    if (list_) list_->setFocus(Qt::OtherFocusReason);
}

void TemplatePalette::rebuild()
{
    if (!list_) return;
    const int keep = selectedClass();
    list_->blockSignals(true);
    list_->clear();
    if (loaded_) {
        const int prim = classStore_.primary();
        for (int col : classStore_.activeClasses()) {
            const std::string lb = classStore_.label(col);
            const int n = static_cast<int>(classStore_.members(col).size());
            const QString txt = tr("%1#%2  %3  (%4 spk)")
                .arg(col == prim ? QStringLiteral("★ ") : QString())   // ★ primary
                .arg(col)
                .arg(lb.empty() ? tr("(unnamed)") : QString::fromStdString(lb))
                .arg(n);
            auto* item = new QListWidgetItem(txt, list_);
            item->setData(Qt::UserRole, col);
            QPixmap sw(12, 12); sw.fill(classColor(col));
            item->setIcon(QIcon(sw));
            if (col == keep) list_->setCurrentItem(item);
        }
        const int nActive = static_cast<int>(classStore_.activeClasses().size());
        header_->setText(tr("Template classes — %1").arg(nActive == 0 ? tr("none yet") : tr("%1 active").arg(nActive)));
    } else {
        header_->setText(tr("Template classes — no open group"));
    }
    list_->blockSignals(false);
    updateButtons();
}

void TemplatePalette::updateButtons()
{
    newBtn_->setEnabled(loaded_);
    delBtn_->setEnabled(loaded_ && selectedClass() >= 0);
}

void TemplatePalette::onRowChanged()
{
    updateButtons();
    const int col = selectedClass();
    if (col < 0) return;
    classStore_.setPrimary(col);        // the palette's selection IS the active/primary class
    rebuild();                          // refresh the ★ marker
    Q_EMIT classSelected(col);
}

void TemplatePalette::onNewClicked()
{
    // KlustersApp gathers the shown-cluster spikes + provenance and calls
    // createClassFromSpikes (it owns the document + selection).
    Q_EMIT newClassRequested();
}

int TemplatePalette::createClassFromSpikes(const std::vector<int64_t>& spikes, int provClu, const std::string& label)
{
    if (!loaded_ || spikes.empty()) return -1;
    const int col = classStore_.createClass(
        spikes, label, provClu, stage_,
        QDate::currentDate().toString(Qt::ISODate).toStdString());
    if (col < 0) return -1;
    classStore_.save();
    rebuild();
    // Select the new class so it becomes active/primary.
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toInt() == col) { list_->setCurrentRow(i); break; }
    Q_EMIT classesChanged();
    return col;
}

void TemplatePalette::onDeleteClicked()
{
    if (!loaded_) return;
    const int col = selectedClass();
    if (col < 0) return;
    if (!classStore_.deleteClass(col)) return;
    classStore_.save();
    rebuild();
    Q_EMIT classDeleted(col);          // the lineage overlay drops this class's nodes + repaints
    Q_EMIT classesChanged();
}

void TemplatePalette::keyPressEvent(QKeyEvent* e)
{
    // `s` pins the selected class(es) as the oblique basis (KlustersApp maps the
    // columns to their provenance clusters and validates).
    if (loaded_ && (e->key() == Qt::Key_S) && e->modifiers() == Qt::NoModifier) {
        QList<int> cols;
        const auto sel = list_->selectedItems();
        for (const QListWidgetItem* it : sel) {
            const int c = it->data(Qt::UserRole).toInt();
            if (!cols.contains(c)) cols.append(c);
        }
        if (cols.isEmpty() && selectedClass() >= 0) cols.append(selectedClass());
        if (!cols.isEmpty()) { Q_EMIT obliqueRequested(cols); e->accept(); return; }
    }
    QWidget::keyPressEvent(e);
}
