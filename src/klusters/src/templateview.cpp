// TemplateView — see templateview.h

#include "templateview.h"
#include "klustersdoc.h"
#include "klustersview.h"

#include "neurosuite/core/custody.hpp"     // parseAnchor / untaggedPath / stagePath

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QListWidget>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QPainter>
#include <QFileInfo>
#include <QPen>

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

    loadFromDisk();
    rebuildUnitList();
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
