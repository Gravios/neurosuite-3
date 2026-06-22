#include "spectralinspector.h"
#include "spectralview.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>

using neuroscope::spectral::SpectralMode;
using neuroscope::spectral::Colormap;
using neuroscope::spectral::SpectralBackend;

namespace {
void addLabeled(QHBoxLayout* lay, const QString& text, QWidget* w)
{
    QLabel* l = new QLabel(text);
    lay->addWidget(l);
    lay->addWidget(w);
}
}

SpectralInspector::SpectralInspector(SpectralView* view, QWidget* parent)
    : QWidget(parent), view(view)
{
    QHBoxLayout* lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(6);

    const neuroscope::spectral::SpectralParams& p = view->spectralParams();

    modeCombo = new QComboBox;
    modeCombo->addItem(tr("time-freq"));   // index 0: single channel
    modeCombo->addItem(tr("band-chan"));   // index 1: across channels
    modeCombo->setCurrentIndex(p.mode == SpectralMode::FrequencyAcrossChannels ? 1 : 0);

    channelSpin = new QSpinBox;
    channelSpin->setRange(0, 1023);
    channelSpin->setValue(p.singleChannel);
    channelSpin->setToolTip(tr("channel row (time-freq mode)"));

    nwSpin = new QDoubleSpinBox;
    nwSpin->setRange(1.0, 10.0); nwSpin->setSingleStep(0.5); nwSpin->setDecimals(1);
    nwSpin->setValue(p.nw);
    nwSpin->setToolTip(tr("time-bandwidth NW"));

    taperSpin = new QSpinBox;
    taperSpin->setRange(1, 20); taperSpin->setValue(p.nTapers);
    taperSpin->setToolTip(tr("number of tapers K"));

    windowSpin = new QSpinBox;
    windowSpin->setRange(16, 8192); windowSpin->setValue(p.windowSamples);
    windowSpin->setToolTip(tr("window length (samples)"));

    nfftSpin = new QSpinBox;
    nfftSpin->setRange(16, 16384); nfftSpin->setValue(p.nfft);
    nfftSpin->setToolTip(tr("FFT length"));

    stepSpin = new QSpinBox;
    stepSpin->setRange(1, 8192); stepSpin->setValue(p.stepSamples);
    stepSpin->setToolTip(tr("hop (samples)"));

    freqLowSpin = new QDoubleSpinBox;
    freqLowSpin->setRange(0.0, 100000.0); freqLowSpin->setDecimals(0);
    freqLowSpin->setValue(p.freqLow);
    freqLowSpin->setToolTip(tr("band low (Hz)"));

    freqHighSpin = new QDoubleSpinBox;
    freqHighSpin->setRange(0.0, 100000.0); freqHighSpin->setDecimals(0);
    freqHighSpin->setValue(p.freqHigh);
    freqHighSpin->setToolTip(tr("band high (Hz); 0 = Nyquist"));

    whitenCheck = new QCheckBox(tr("whiten"));
    whitenCheck->setChecked(p.whiten);

    colormapCombo = new QComboBox;
    colormapCombo->addItem(tr("gray"));
    colormapCombo->addItem(tr("viridis"));
    colormapCombo->addItem(tr("inferno"));
    colormapCombo->setCurrentIndex(1);

    dynRangeSpin = new QDoubleSpinBox;
    dynRangeSpin->setRange(6.0, 160.0); dynRangeSpin->setDecimals(0);
    dynRangeSpin->setValue(60.0);
    dynRangeSpin->setToolTip(tr("dynamic range (dB)"));

    backendCombo = new QComboBox;
    backendCombo->addItem(tr("CPU"));
    backendCombo->addItem(tr("GPU"));
    backendCombo->setCurrentIndex(0);

    addLabeled(lay, tr("mode"), modeCombo);
    addLabeled(lay, tr("ch"), channelSpin);
    addLabeled(lay, tr("NW"), nwSpin);
    addLabeled(lay, tr("K"), taperSpin);
    addLabeled(lay, tr("win"), windowSpin);
    addLabeled(lay, tr("nfft"), nfftSpin);
    addLabeled(lay, tr("step"), stepSpin);
    addLabeled(lay, tr("f.lo"), freqLowSpin);
    addLabeled(lay, tr("f.hi"), freqHighSpin);
    lay->addWidget(whitenCheck);
    addLabeled(lay, tr("cmap"), colormapCombo);
    addLabeled(lay, tr("dB"), dynRangeSpin);
    addLabeled(lay, tr("backend"), backendCombo);
    lay->addStretch(1);

    // Wire controls to the view (after initial values are set above).
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i){ this->view->setSpectralMode(i == 1 ? SpectralMode::FrequencyAcrossChannels
                                                              : SpectralMode::TimeFrequencySingleChannel); });
    connect(channelSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            view, &SpectralView::setSingleChannelRow);
    connect(nwSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            view, &SpectralView::setTimeBandwidth);
    connect(taperSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            view, &SpectralView::setNumTapers);
    connect(windowSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            view, &SpectralView::setWindowSamples);
    connect(nfftSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            view, &SpectralView::setNfft);
    connect(stepSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            view, &SpectralView::setStep);
    connect(whitenCheck, &QCheckBox::toggled, view, &SpectralView::setWhitening);
    connect(dynRangeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            view, &SpectralView::setDynamicRangeDb);

    auto applyFreq = [this]{ this->view->setFrequencyRange(freqLowSpin->value(), freqHighSpin->value()); };
    connect(freqLowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [applyFreq](double){ applyFreq(); });
    connect(freqHighSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [applyFreq](double){ applyFreq(); });

    connect(colormapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i){ this->view->setColormap(static_cast<Colormap>(i)); });
    connect(backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i){ this->view->setBackend(i == 1 ? SpectralBackend::Cuda
                                                         : SpectralBackend::Cpu); });
}

void SpectralInspector::setChannelCount(int n)
{
    if (n < 1) n = 1;
    channelSpin->setRange(0, n - 1);
}
