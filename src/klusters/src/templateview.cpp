// TemplateView — see templateview.h

#include "templateview.h"
#include "klustersdoc.h"
#include "klustersview.h"
#include "data.h"                          // Data::clusterSpkIndices / totalNbOfSpikes

#include "neurosuite/core/custody.hpp"     // parseAnchor / untaggedPath / stagePath

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QListWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QPainter>
#include <QFileInfo>
#include <QPen>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QDate>
#include <QFrame>

#include <algorithm>
#include <cmath>

// ── TemplateWavePanel ───────────────────────────────────────────────────────
TemplateWavePanel::TemplateWavePanel(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(320, 220);
    setAutoFillBackground(false);
}

void TemplateWavePanel::setData(int nSamp, int nChan,
                                std::vector<std::vector<int16_t>> waves, int cur)
{
    nSamples  = nSamp;
    nChannels = nChan;
    series    = std::move(waves);
    current   = cur;
    update();
}

double TemplateWavePanel::globalMaxAbs() const
{
    double m = 1.0;                                   // avoid /0 on a flat series
    for (const auto& w : series)
        for (int16_t v : w) m = std::max(m, std::fabs(static_cast<double>(v)));
    return m;
}

void TemplateWavePanel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), background);
    if (nSamples <= 1 || nChannels <= 0 || series.empty()) {
        p.setPen(QColor(160, 160, 160));
        p.drawText(rect(), Qt::AlignCenter, tr("No template waveforms for this selection."));
        return;
    }

    const int   W      = width();
    const int   H      = height();
    const int   margin = 8;
    const double laneH = static_cast<double>(H - 2 * margin) / nChannels;
    const double scale = (laneH * 0.45) / globalMaxAbs();   // shared across bins
    const double dx    = static_cast<double>(W - 2 * margin) / (nSamples - 1);
    const std::size_t rec = static_cast<std::size_t>(nSamples) * nChannels;

    auto drawWave = [&](const std::vector<int16_t>& w, const QColor& col) {
        if (w.size() < rec) return;
        p.setPen(QPen(col, 1));
        for (int c = 0; c < nChannels; ++c) {
            const double y0 = margin + (c + 0.5) * laneH;
            QPointF prev;
            for (int t = 0; t < nSamples; ++t) {
                const double v = static_cast<double>(w[static_cast<std::size_t>(t) * nChannels + c]);
                const QPointF pt(margin + t * dx, y0 - v * scale);
                if (t > 0) p.drawLine(prev, pt);
                prev = pt;
            }
        }
    };

    // Ghost the whole series faint, then the current bin solid on top.
    QColor ghost = accent; ghost.setAlpha(45);
    for (std::size_t i = 0; i < series.size(); ++i)
        if (static_cast<int>(i) != current) drawWave(series[i], ghost);
    if (current >= 0 && current < static_cast<int>(series.size()))
        drawWave(series[static_cast<std::size_t>(current)], accent);

    // Faint channel baselines.
    p.setPen(QColor(70, 70, 70));
    for (int c = 0; c < nChannels; ++c) {
        const double y0 = margin + (c + 0.5) * laneH;
        p.drawLine(QPointF(margin, y0), QPointF(W - margin, y0));
    }
}

// ── TemplateView ────────────────────────────────────────────────────────────
TemplateView::TemplateView(KlustersDoc& pDoc, KlustersView& pView,
                           const QColor& backgroundColor, QStatusBar* sb,
                           QWidget* parent)
    : QWidget(parent), doc(pDoc), klView(pView), statusBar(sb)
{
    // ── widgets ──────────────────────────────────────────────────────────────
    headerLabel = new QLabel(this);
    headerLabel->setWordWrap(true);

    unitList = new QListWidget(this);
    unitList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    unitList->setMaximumWidth(180);

    pinButton = new QPushButton(tr("Pin selected as oblique basis"), this);
    pinButton->setToolTip(tr("Pin the selected template units as the fixed axes of "
                             "the oblique (template-axis) projection (two or more)."));

    // ── template-class panel (.eap/.tcl) ───────────────────────────────────────
    classHeader = new QLabel(tr("Template classes"), this);
    classHeader->setWordWrap(true);
    classList = new QListWidget(this);
    classList->setSelectionMode(QAbstractItemView::SingleSelection);
    classList->setMaximumWidth(220);
    classList->setToolTip(tr("Stable template-class ids (the .eap columns): each row is "
                             "#id, label and member-spike count; the primary is marked *."));

    newClassButton    = new QPushButton(tr("New"), this);
    updateClassButton = new QPushButton(tr("Update"), this);
    mergeClassButton  = new QPushButton(tr("Merge → primary"), this);
    deleteClassButton = new QPushButton(tr("Delete"), this);
    renameClassButton = new QPushButton(tr("Rename…"), this);
    setPrimaryButton  = new QPushButton(tr("Set primary"), this);
    regenButton       = new QPushButton(tr("Regenerate waveforms"), this);
    newClassButton->setToolTip(tr("Create a new template class from the spikes of the "
                                  "currently selected unit cluster(s); it becomes the primary."));
    updateClassButton->setToolTip(tr("Re-set the PRIMARY class's membership to the spikes "
                                     "of the current cluster selection."));
    mergeClassButton->setToolTip(tr("Fold the selected class into the primary (the primary "
                                    "keeps its offsets; the id is retired as merged)."));
    deleteClassButton->setToolTip(tr("Tombstone the selected class; its id is never reused."));
    renameClassButton->setToolTip(tr("Relabel the selected class."));
    setPrimaryButton->setToolTip(tr("Make the selected class the primary (the update target)."));
    regenButton->setToolTip(tr("Rerun fiber-template so the template waveform series reflects "
                               "the current class membership."));

    linkCombo = new QComboBox(this);
    binSlider = new QSlider(Qt::Horizontal, this);
    binSlider->setMinimum(0);
    binSlider->setMaximum(0);
    infoLabel = new QLabel(this);
    infoLabel->setMinimumWidth(240);

    panel = new TemplateWavePanel(this);
    panel->accent     = QColor(80, 170, 255);
    panel->background = backgroundColor.isValid() ? backgroundColor : QColor(20, 20, 20);

    // ── layout ───────────────────────────────────────────────────────────────
    auto* left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("Template units"), this));
    left->addWidget(unitList, 1);
    left->addWidget(pinButton);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    left->addWidget(sep);
    left->addWidget(classHeader);
    left->addWidget(classList, 1);
    auto* classBtns = new QGridLayout;
    classBtns->addWidget(newClassButton,    0, 0);
    classBtns->addWidget(updateClassButton, 0, 1);
    classBtns->addWidget(setPrimaryButton,  1, 0);
    classBtns->addWidget(mergeClassButton,  1, 1);
    classBtns->addWidget(renameClassButton, 2, 0);
    classBtns->addWidget(deleteClassButton, 2, 1);
    left->addLayout(classBtns);
    left->addWidget(regenButton);

    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Link:"), this));
    controls->addWidget(linkCombo);
    controls->addWidget(new QLabel(tr("Bin:"), this));
    controls->addWidget(binSlider, 1);
    controls->addWidget(infoLabel);

    auto* right = new QVBoxLayout;
    right->addWidget(headerLabel);
    right->addLayout(controls);
    right->addWidget(panel, 1);

    auto* root = new QHBoxLayout(this);
    root->addLayout(left);
    root->addLayout(right, 1);

    // ── signals (lambdas, so no declared slots / no extra moc surface) ─────────
    connect(unitList, &QListWidget::currentRowChanged, this, [this](int){ onUnitChanged(); });
    connect(linkCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int){ onLinkOrBinChanged(); });
    connect(binSlider, &QSlider::valueChanged, this, [this](int){ onLinkOrBinChanged(); });
    connect(pinButton, &QPushButton::clicked, this, [this](){ onPinClicked(); });

    connect(classList, &QListWidget::currentRowChanged, this, [this](int){ updateClassButtons(); });
    connect(newClassButton,    &QPushButton::clicked, this, [this](){ onNewClass(); });
    connect(updateClassButton, &QPushButton::clicked, this, [this](){ onUpdatePrimary(); });
    connect(mergeClassButton,  &QPushButton::clicked, this, [this](){ onMergeIntoPrimary(); });
    connect(deleteClassButton, &QPushButton::clicked, this, [this](){ onDeleteClass(); });
    connect(renameClassButton, &QPushButton::clicked, this, [this](){ onRenameClass(); });
    connect(setPrimaryButton,  &QPushButton::clicked, this, [this](){ onSetPrimary(); });
    connect(regenButton,       &QPushButton::clicked, this, [this](){ onRegenClicked(); });

    loadFromDisk();
    rebuildUnitList();
    loadClasses();
}

void TemplateView::loadFromDisk()
{
    loaded = false;
    const QString cluPath = doc.url();
    const QString spkPath = doc.origSpkFilePath();
    if (cluPath.isEmpty()) { headerLabel->setText(tr("No clustering is open.")); return; }

    const auto aClu = neurosuite::custody::parseAnchor(QFileInfo(cluPath).fileName().toStdString());
    const auto aSpk = neurosuite::custody::parseAnchor(QFileInfo(spkPath).fileName().toStdString());
    if (!aClu.ok) { headerLabel->setText(tr("Could not parse the session file name.")); return; }

    const QString dir = QFileInfo(cluPath).absolutePath() + QLatin1Char('/');
    base      = dir + QString::fromStdString(aClu.base);
    group     = aClu.group;
    variant   = aSpk.ok ? QString::fromStdString(aSpk.method) : QString();
    tag       = QString::fromStdString(aClu.suffix);            // "" = untagged stage
    nSamples  = doc.getNbSamplesBeforePeak() + doc.getNbSamplesAfterPeak() + 1;
    nChannels = doc.nbOfchannels();
    coordsResolved = true;        // base/group/tag/geometry are valid from here on
                                  // (the class store needs these even when no .wti exists)

    // .wti is SHARED across methods -> method-less path <base>.wti.<group>[.<tag>].
    std::string wtiPath = neurosuite::custody::untaggedPath(base.toStdString(), "wti", group);
    if (!tag.isEmpty()) wtiPath += "." + tag.toStdString();
    // .wtf is per-method -> <base>.wtf.<variant>.<group>[.<tag>].
    const std::string wtfPath =
        neurosuite::custody::stagePath(base.toStdString(), "wtf", variant.toStdString(),
                                       group, tag.toStdString());

    wti = neurofileio::readWti(wtiPath);
    if (!wti.ok) {
        headerLabel->setText(tr("No template index (.wti) for group %1%2.\n"
                                "Mark units as templates and save to build the library.")
                                 .arg(group)
                                 .arg(tag.isEmpty() ? QString() : QStringLiteral(" / stage \"%1\"").arg(tag)));
        return;
    }
    // The .wtf waveforms (int16, one record per .wti row).  A geometry from the
    // .wti wins when it is present, so the view does not depend on the open
    // document's geometry matching the stored one.
    const int wNs = wti.nSamples  > 0 ? wti.nSamples  : nSamples;
    const int wNc = wti.nChannels > 0 ? wti.nChannels : nChannels;
    nSamples = wNs; nChannels = wNc;
    wtf = neurofileio::readSpk(wtfPath, nSamples, nChannels);

    loaded = true;
    const QString wtfState = wtf.ok
        ? tr("%1 waveforms").arg(static_cast<long long>(wtf.nSpikes))
        : tr("no .wtf for method \"%1\" (showing index only)").arg(variant);
    headerLabel->setText(tr("Template library — group %1%2, method \"%3\" — %4 rows, %5.")
        .arg(group)
        .arg(tag.isEmpty() ? QString() : QStringLiteral(" / stage \"%1\"").arg(tag))
        .arg(variant.isEmpty() ? tr("(none)") : variant)
        .arg(static_cast<int>(wti.rows.size()))
        .arg(wtfState));
}

void TemplateView::rebuildUnitList()
{
    unitList->clear();
    if (!loaded) { pinButton->setEnabled(false); return; }
    const std::vector<int> units = neurofileio::wtiUnits(wti);
    for (int u : units) {
        // bins = rows across all links for this unit (just a hint count).
        int nBins = 0;
        for (const auto& r : wti.rows) if (r.unitId == u) ++nBins;
        auto* item = new QListWidgetItem(tr("Unit %1  (%2 bins)").arg(u).arg(nBins), unitList);
        item->setData(Qt::UserRole, u);
    }
    pinButton->setEnabled(unitList->count() > 0);
    if (unitList->count() > 0) unitList->setCurrentRow(0);   // triggers onUnitChanged
}

int TemplateView::shownUnit() const
{
    const QListWidgetItem* it = unitList->currentItem();
    return it ? it->data(Qt::UserRole).toInt() : -1;
}

void TemplateView::onUnitChanged()
{
    const int u = shownUnit();
    linkCombo->blockSignals(true);
    linkCombo->clear();
    if (u >= 0) for (const std::string& lk : neurofileio::wtiLinks(wti, u))
        linkCombo->addItem(QString::fromStdString(lk));
    linkCombo->blockSignals(false);
    if (linkCombo->count() > 0) linkCombo->setCurrentIndex(0);
    onLinkOrBinChanged();
}

void TemplateView::onLinkOrBinChanged()
{
    const int u = shownUnit();
    if (u < 0 || linkCombo->count() == 0) {
        panel->setData(nSamples, nChannels, {}, -1);
        infoLabel->clear();
        return;
    }
    const std::string link = linkCombo->currentText().toStdString();
    const std::vector<neurofileio::WtiRow> ser = neurofileio::wtiSeries(wti, u, link);

    // Keep the slider range in step with the series length.
    binSlider->blockSignals(true);
    binSlider->setMaximum(ser.empty() ? 0 : static_cast<int>(ser.size()) - 1);
    if (binSlider->value() > binSlider->maximum()) binSlider->setValue(binSlider->maximum());
    binSlider->blockSignals(false);
    const int cur = std::min(binSlider->value(), binSlider->maximum());

    // Pull each bin's waveform out of the .wtf by its row (= record) index.
    const std::size_t rec = static_cast<std::size_t>(nSamples) * nChannels;
    std::vector<std::vector<int16_t>> waves;
    waves.reserve(ser.size());
    for (const auto& r : ser) {
        std::vector<int16_t> w;
        const std::size_t off = static_cast<std::size_t>(r.row) * rec;
        if (wtf.ok && off + rec <= wtf.samples.size())
            w.assign(wtf.samples.begin() + static_cast<std::ptrdiff_t>(off),
                     wtf.samples.begin() + static_cast<std::ptrdiff_t>(off + rec));
        waves.push_back(std::move(w));
    }
    panel->setData(nSamples, nChannels, std::move(waves), ser.empty() ? -1 : cur);

    if (!ser.empty() && cur >= 0 && cur < static_cast<int>(ser.size())) {
        const auto& r = ser[static_cast<std::size_t>(cur)];
        infoLabel->setText(tr("bin %1/%2   [%3, %4]   %5 spk")
            .arg(cur + 1).arg(static_cast<int>(ser.size()))
            .arg(r.a, 0, 'g', 4).arg(r.b, 0, 'g', 4)
            .arg(static_cast<long long>(r.nSpikes)));
    } else {
        infoLabel->clear();
    }
}

void TemplateView::onPinClicked()
{
    QList<int> ids;
    const auto sel = unitList->selectedItems();
    for (const QListWidgetItem* it : sel) {
        const int u = it->data(Qt::UserRole).toInt();
        if (!ids.contains(u)) ids.append(u);
    }
    if (ids.isEmpty() && shownUnit() >= 0) ids.append(shownUnit());   // fall back to the shown unit
    if (ids.size() < 2) {
        if (statusBar) statusBar->showMessage(
            tr("Select two or more template units to pin as the oblique basis."), 5000);
        return;
    }
    klView.requestPinObliqueBasis(ids);     // the active cluster view validates + stores
}

// ── template classes (.eap/.tcl) ─────────────────────────────────────────────
void TemplateView::showStatus(const QString& msg)
{
    if (statusBar) statusBar->showMessage(msg, 6000);
}

void TemplateView::loadClasses()
{
    classesLoaded = false;
    if (coordsResolved && !base.isEmpty()) {
        // N for the matrix is the session/group spike count (the .spk/.res length,
        // = the .eap row count).  An existing .eap (normally preconstructed by
        // process_initeap from the YAML nCells) wins; otherwise the store builds a
        // fresh 128-wide all-absent matrix.  A tagged stage with no .eap of its own
        // inherits the untagged base membership (TemplateClassStore::load).
        const int64_t nSpk = static_cast<int64_t>(doc.data().totalNbOfSpikes());
        classStore.load(base.toStdString(), group, tag.toStdString(), nSpk, 128);
        classesLoaded = classStore.ok();
    }
    rebuildClassList();
}

void TemplateView::rebuildClassList()
{
    if (!classList) return;
    const int keep = selectedClassCol();
    classList->clear();
    if (classesLoaded) {
        const int prim = classStore.primary();
        for (int col : classStore.activeClasses()) {
            const std::string lb = classStore.label(col);
            const int n = static_cast<int>(classStore.members(col).size());
            QString txt = tr("%1#%2  %3  (%4 spk)")
                .arg(col == prim ? QStringLiteral("* ") : QString())
                .arg(col)
                .arg(lb.empty() ? tr("(unnamed)") : QString::fromStdString(lb))
                .arg(n);
            auto* item = new QListWidgetItem(txt, classList);
            item->setData(Qt::UserRole, col);
            if (col == keep) classList->setCurrentItem(item);
        }
        const QString pstr = (prim >= 0) ? tr("primary #%1").arg(prim) : tr("no primary");
        classHeader->setText(tr("Template classes — %1 active, %2")
            .arg(static_cast<int>(classStore.activeClasses().size())).arg(pstr));
    } else {
        classHeader->setText(tr("Template classes — unavailable (no open group)."));
    }
    updateClassButtons();
}

void TemplateView::updateClassButtons()
{
    const bool on   = classesLoaded;
    const int  sel  = selectedClassCol();
    const int  prim = on ? classStore.primary() : -1;
    newClassButton->setEnabled(on);
    updateClassButton->setEnabled(on && prim >= 0);
    deleteClassButton->setEnabled(on && sel >= 0);
    renameClassButton->setEnabled(on && sel >= 0);
    setPrimaryButton->setEnabled(on && sel >= 0);
    mergeClassButton->setEnabled(on && sel >= 0 && prim >= 0 && sel != prim);
    regenButton->setEnabled(on);
}

void TemplateView::persistClasses()
{
    if (!classStore.save())
        showStatus(tr("Could not write the .eap / .tcl class files to disk."));
}

int TemplateView::selectedClassCol() const
{
    const QListWidgetItem* it = classList ? classList->currentItem() : nullptr;
    return it ? it->data(Qt::UserRole).toInt() : -1;
}

int TemplateView::firstSelectedUnit() const
{
    int best = -1;
    for (int cid : klView.clusters())
        if (cid > 1 && (best < 0 || cid < best)) best = cid;      // skip noise(0)/artifact(1)
    return best;
}

std::vector<int64_t> TemplateView::selectionSpikeIndices() const
{
    std::vector<int64_t> out;
    for (int cid : klView.clusters()) {
        if (cid <= 1) continue;                                    // skip noise(0)/artifact(1)
        const QVector<int> idx = doc.data().clusterSpkIndices(cid);  // 0-based .spk ids
        out.reserve(out.size() + static_cast<std::size_t>(idx.size()));
        for (int s : idx) if (s >= 0) out.push_back(static_cast<int64_t>(s));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void TemplateView::onNewClass()
{
    if (!classesLoaded) return;
    const std::vector<int64_t> spk = selectionSpikeIndices();
    if (spk.empty()) {
        showStatus(tr("Select one or more unit clusters (id > 1) to make a template class."));
        return;
    }
    const int provClu = firstSelectedUnit();
    const QString label = (provClu >= 0) ? tr("clu %1").arg(provClu) : QString();
    const int col = classStore.createClass(
        spk, label.toStdString(), provClu, tag.toStdString(),
        QDate::currentDate().toString(Qt::ISODate).toStdString());
    if (col < 0) { showStatus(tr("Could not create a template class.")); return; }
    persistClasses();
    rebuildClassList();
    showStatus(tr("Created class #%1 from %2 spikes (now primary). "
                  "Click “Regenerate waveforms” to refresh its series.")
                   .arg(col).arg(static_cast<int>(spk.size())));
}

void TemplateView::onUpdatePrimary()
{
    if (!classesLoaded) return;
    const int prim = classStore.primary();
    if (prim < 0) {
        showStatus(tr("No primary class — select a class and click “Set primary” first."));
        return;
    }
    const std::vector<int64_t> spk = selectionSpikeIndices();
    if (spk.empty()) {
        showStatus(tr("Select one or more unit clusters (id > 1) to update the primary class."));
        return;
    }
    if (!classStore.updateClass(prim, spk)) { showStatus(tr("Could not update the primary class.")); return; }
    persistClasses();
    rebuildClassList();
    showStatus(tr("Updated primary class #%1 to %2 spikes.").arg(prim).arg(static_cast<int>(spk.size())));
}

void TemplateView::onMergeIntoPrimary()
{
    if (!classesLoaded) return;
    const int survivor = classStore.primary();
    const int victim   = selectedClassCol();
    if (survivor < 0) { showStatus(tr("No primary class — “Set primary” on the merge target first.")); return; }
    if (victim < 0 || victim == survivor) {
        showStatus(tr("Select a different (non-primary) class to merge into the primary."));
        return;
    }
    if (!classStore.mergeClasses(survivor, victim)) { showStatus(tr("Merge failed.")); return; }
    persistClasses();
    rebuildClassList();
    showStatus(tr("Merged class #%1 into primary #%2.").arg(victim).arg(survivor));
}

void TemplateView::onDeleteClass()
{
    if (!classesLoaded) return;
    const int col = selectedClassCol();
    if (col < 0) return;
    const QString lb = QString::fromStdString(classStore.label(col));
    const auto ans = QMessageBox::question(this, tr("Delete template class"),
        tr("Delete template class #%1%2?\nThe id is retired and never reused.")
            .arg(col).arg(lb.isEmpty() ? QString() : QStringLiteral(" “%1”").arg(lb)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes) return;
    if (!classStore.deleteClass(col)) { showStatus(tr("Delete failed.")); return; }
    persistClasses();
    rebuildClassList();
    showStatus(tr("Deleted class #%1.").arg(col));
}

void TemplateView::onRenameClass()
{
    if (!classesLoaded) return;
    const int col = selectedClassCol();
    if (col < 0) return;
    bool ok = false;
    const QString cur = QString::fromStdString(classStore.label(col));
    const QString nw = QInputDialog::getText(this, tr("Rename template class"),
        tr("Label for class #%1:").arg(col), QLineEdit::Normal, cur, &ok);
    if (!ok) return;
    if (!classStore.rename(col, nw.toStdString())) { showStatus(tr("Rename failed.")); return; }
    persistClasses();
    rebuildClassList();
}

void TemplateView::onSetPrimary()
{
    if (!classesLoaded) return;
    const int col = selectedClassCol();
    if (col < 0) return;
    classStore.setPrimary(col);         // primary is session UI state, not persisted to .tcl
    rebuildClassList();
    showStatus(tr("Primary class is now #%1.").arg(col));
}

void TemplateView::onRegenClicked()
{
    if (!classesLoaded) return;
    showStatus(tr("Regenerating template waveforms (fiber-template)…"));
    klView.requestTemplateRegen();      // reruns fiber-template for the marked units
}
