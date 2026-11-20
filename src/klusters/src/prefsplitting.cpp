#include "prefsplitting.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QSpinBox>

PrefSplitting::PrefSplitting(QWidget* parent)
    : PrefSplittingLayout(parent)
{
    // ── Split Off Spurious Spikes (Actions menu, Ctrl+Space) ──
    QGroupBox* box = new QGroupBox(tr("Split Off Spurious Spikes"), this);
    box->setToolTip(tr(
        "Actions > Split Off Spurious Spikes: a cluster's spikes are put in the order in which each next one\n"
        "changes the cumulative mean waveform least, and the order is cut where a channel's cumulative\n"
        "kurtosis starts to rise.  The spikes past the cut become a new cluster."));
    QFormLayout* form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    const auto ms = [box](double lo, double hi) {
        QDoubleSpinBox* s = new QDoubleSpinBox(box);
        s->setRange(lo, hi); s->setDecimals(3); s->setSingleStep(0.01); s->setSuffix(tr(" ms"));
        return s;
    };
    coreCutOnsetSpin = new QDoubleSpinBox(box);
    coreCutOnsetSpin->setRange(1.1, 1000.0); coreCutOnsetSpin->setDecimals(1); coreCutOnsetSpin->setSingleStep(0.5);
    coreCutOnsetSpin->setPrefix(QStringLiteral("\u00d7 "));
    coreCutOnsetSpin->setToolTip(tr(
        "How sharp the rise of the kurtosis has to be to count as the cut: this many times its usual size\n"
        "over the first half of the order.  Lower cuts earlier and splits off more; higher cuts later, or not at all."));
    form->addRow(tr("Cut sensitivity"), coreCutOnsetSpin);

    QHBoxLayout* win = new QHBoxLayout();
    coreCutRiseSpin = ms(-5.0, 0.0); coreCutFlatStartSpin = ms(-5.0, 5.0); coreCutFlatEndSpin = ms(-5.0, 5.0); coreCutFallSpin = ms(0.0, 5.0);
    for (QDoubleSpinBox* s : {coreCutRiseSpin, coreCutFlatStartSpin, coreCutFlatEndSpin, coreCutFallSpin}) win->addWidget(s);
    win->addStretch(1);
    const QString winTip = tr(
        "The part of the waveform that is compared, relative to the point where the falling flank crosses half\n"
        "the trough depth: where the window starts to rise, where it is fully open, where it starts to fall,\n"
        "where it is closed.  The four must be in that order; otherwise the default window is used.");
    for (QDoubleSpinBox* s : {coreCutRiseSpin, coreCutFlatStartSpin, coreCutFlatEndSpin, coreCutFallSpin}) s->setToolTip(winTip);
    form->addRow(tr("Window: rise, open, fall, closed"), win);

    coreCutShiftSpin = ms(0.0, 2.0);
    coreCutShiftSpin->setToolTip(tr("A spike is moved at most this far when it is centred on its half crossing.  0: no centring."));
    form->addRow(tr("Largest centring shift"), coreCutShiftSpin);

    coreCutMaxOrderedSpin = new QSpinBox(box);
    coreCutMaxOrderedSpin->setRange(1000, 200000); coreCutMaxOrderedSpin->setSingleStep(5000); coreCutMaxOrderedSpin->setSuffix(tr(" spikes"));
    coreCutMaxOrderedSpin->setToolTip(tr(
        "The ordering takes time with the square of the spike count.  A larger cluster has this many evenly\n"
        "spaced spikes ordered and cut, and the others follow them."));
    form->addRow(tr("Order at most"), coreCutMaxOrderedSpin);

    coreCutMinSpikesSpin = new QSpinBox(box);
    coreCutMinSpikesSpin->setRange(50, 200000); coreCutMinSpikesSpin->setSingleStep(50); coreCutMinSpikesSpin->setSuffix(tr(" spikes"));
    coreCutMinSpikesSpin->setToolTip(tr("A cluster with fewer spikes is left alone."));
    form->addRow(tr("Smallest cluster"), coreCutMinSpikesSpin);

    coreCutGpuCheck = new QCheckBox(tr("Order on the GPU when there is one"), box);
    coreCutGpuCheck->setToolTip(tr("CUDA builds, clusters of 4,000 spikes or more.  The status bar says when the GPU ordered."));
    form->addRow(QString(), coreCutGpuCheck);

    // Under the page's other groups, above its trailing stretch.
    if (QBoxLayout* rows = qobject_cast<QBoxLayout*>(layout())) {
        int at = rows->count();
        while (at > 0 && rows->itemAt(at - 1) && rows->itemAt(at - 1)->spacerItem()) --at;
        rows->insertWidget(at, box);
    } else if (QGridLayout* grid = qobject_cast<QGridLayout*>(layout())) {
        grid->addWidget(box, grid->rowCount(), 0, 1, -1);
    }

    setCoreCut(configuration().getCoreCutDefault());
    for (QDoubleSpinBox* s : {coreCutOnsetSpin, coreCutRiseSpin, coreCutFlatStartSpin, coreCutFlatEndSpin, coreCutFallSpin, coreCutShiftSpin})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this](double) { Q_EMIT coreCutChanged(); });
    for (QSpinBox* s : {coreCutMaxOrderedSpin, coreCutMinSpikesSpin})
        connect(s, &QSpinBox::valueChanged, this, [this](int) { Q_EMIT coreCutChanged(); });
    connect(coreCutGpuCheck, &QCheckBox::toggled, this, [this](bool) { Q_EMIT coreCutChanged(); });
}

void PrefSplitting::setCoreCut(const Configuration::CoreCut& c)
{
    const QSignalBlocker b1(coreCutOnsetSpin), b2(coreCutRiseSpin), b3(coreCutFlatStartSpin), b4(coreCutFlatEndSpin),
                         b5(coreCutFallSpin), b6(coreCutShiftSpin), b7(coreCutMaxOrderedSpin), b8(coreCutMinSpikesSpin), b9(coreCutGpuCheck);
    coreCutOnsetSpin->setValue(c.onsetFactor);
    coreCutRiseSpin->setValue(c.riseStartMs);
    coreCutFlatStartSpin->setValue(c.flatStartMs);
    coreCutFlatEndSpin->setValue(c.flatEndMs);
    coreCutFallSpin->setValue(c.fallEndMs);
    coreCutShiftSpin->setValue(c.maxShiftMs);
    coreCutMaxOrderedSpin->setValue(c.maxOrdered);
    coreCutMinSpikesSpin->setValue(c.minSpikes);
    coreCutGpuCheck->setChecked(c.useGpu);
}

Configuration::CoreCut PrefSplitting::getCoreCut() const
{
    Configuration::CoreCut c;
    c.onsetFactor = coreCutOnsetSpin->value();
    c.riseStartMs = coreCutRiseSpin->value();
    c.flatStartMs = coreCutFlatStartSpin->value();
    c.flatEndMs   = coreCutFlatEndSpin->value();
    c.fallEndMs   = coreCutFallSpin->value();
    c.maxShiftMs  = coreCutShiftSpin->value();
    c.maxOrdered  = coreCutMaxOrderedSpin->value();
    c.minSpikes   = coreCutMinSpikesSpin->value();
    c.useGpu      = coreCutGpuCheck->isChecked();
    return c;
}

void   PrefSplitting::setDipSplitMinSize(int n)         { dipSplitMinSizeSpinBox->setValue(n); }
void   PrefSplitting::setDipSplitBloatFactor(double v)  { dipSplitBloatFactorSpinBox->setValue(v); }
void   PrefSplitting::setDipSplitValleyThresh(double v) { dipSplitValleyThreshSpinBox->setValue(v); }
int    PrefSplitting::getDipSplitMinSize()      const { return dipSplitMinSizeSpinBox->value(); }
double PrefSplitting::getDipSplitBloatFactor()  const { return dipSplitBloatFactorSpinBox->value(); }
double PrefSplitting::getDipSplitValleyThresh() const { return dipSplitValleyThreshSpinBox->value(); }
void   PrefSplitting::setKnnK(int n)             { knnKSpinBox->setValue(n); }
void   PrefSplitting::setKnnThreshold(double v)  { knnThresholdSpinBox->setValue(v); }
void   PrefSplitting::setKnnMinNew(int n)        { knnMinNewSpinBox->setValue(n); }
void   PrefSplitting::setKnnMinRef(int n)        { knnMinRefSpinBox->setValue(n); }
int    PrefSplitting::getKnnK()         const { return knnKSpinBox->value(); }
double PrefSplitting::getKnnThreshold() const { return knnThresholdSpinBox->value(); }
int    PrefSplitting::getKnnMinNew()    const { return knnMinNewSpinBox->value(); }
int    PrefSplitting::getKnnMinRef()    const { return knnMinRefSpinBox->value(); }
