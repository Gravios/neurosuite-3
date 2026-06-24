#include "spectralinspector.h"
#include "spectralview.h"

#include <algorithm>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QShortcut>

using neuroscope::spectral::SpectralMode;
using neuroscope::spectral::Colormap;
using neuroscope::spectral::SpectralBackend;

SpectralInspector::SpectralInspector(SpectralView* initialView, QWidget* parent)
    : QWidget(parent), view(nullptr)
{
    // ── controls (default values; reloadFromView() fills them from a view) ──
    modeCombo = new QComboBox;
    modeCombo->addItem(tr("time-freq"));   // index 0: single channel
    modeCombo->addItem(tr("band-chan"));   // index 1: across channels

    channelSpin = new QSpinBox;
    channelSpin->setRange(0, 10000);
    channelSpin->setToolTip(tr("channel row (time-freq mode)"));

    nwSpin = new QDoubleSpinBox;
    nwSpin->setRange(1.0, 1000.0); nwSpin->setSingleStep(0.5); nwSpin->setDecimals(1);
    nwSpin->setToolTip(tr("time-bandwidth NW"));

    taperSpin = new QSpinBox;
    taperSpin->setRange(1, 1000);
    taperSpin->setToolTip(tr("number of tapers K"));

    windowSpin = new QSpinBox;
    windowSpin->setRange(16, 100000);
    windowSpin->setToolTip(tr("window length (samples)"));

    nfftSpin = new QSpinBox;
    nfftSpin->setRange(16, 100000);
    nfftSpin->setToolTip(tr("FFT length"));

    stepSpin = new QSpinBox;
    stepSpin->setRange(1, 100000);
    stepSpin->setToolTip(tr("hop (samples)"));

    freqLowSpin = new QDoubleSpinBox;
    freqLowSpin->setRange(0.0, 100000.0); freqLowSpin->setDecimals(0);
    freqLowSpin->setToolTip(tr("band low (Hz)"));

    freqHighSpin = new QDoubleSpinBox;
    freqHighSpin->setRange(0.0, 100000.0); freqHighSpin->setDecimals(0);
    freqHighSpin->setToolTip(tr("band high (Hz); 0 = Nyquist"));

    lockCheck = new QCheckBox(tr("lock to trace window"));
    lockCheck->setChecked(true);
    lockCheck->setToolTip(tr("use the trace window; uncheck to set an independent span"));

    spanSpin = new QSpinBox;
    spanSpin->setRange(1, 600000); spanSpin->setSingleStep(100); spanSpin->setSuffix(tr(" ms"));
    spanSpin->setValue(1000);
    spanSpin->setEnabled(false); // enabled when unlocked
    spanSpin->setToolTip(tr("spectral window width, centred on the trace centre"));

    whitenCheck = new QCheckBox(tr("whiten"));
    decimCheck = new QCheckBox(tr("decimate"));
    decimCheck->setToolTip(tr("anti-alias + downsample to the high-frequency edge "
                              "before the transform (faster for narrow low bands)"));

    carCheck = new QCheckBox(tr("CAR"));
    carCheck->setToolTip(tr("common-average reference: subtract the per-sample mean "
                            "of the selected channels from each"));
    refRegressCheck = new QCheckBox(tr("ref regress"));
    refRegressCheck->setToolTip(tr("regress every channel on the reference channel "
                                   "and keep the residual (removes that reference)"));
    refChannelSpin = new QSpinBox;
    refChannelSpin->setRange(0, 10000);
    refChannelSpin->setEnabled(false);   // enabled only while ref regress is on
    refChannelSpin->setToolTip(tr("reference channel row for ref regress"));

    colormapCombo = new QComboBox;
    colormapCombo->addItem(tr("gray"));
    colormapCombo->addItem(tr("viridis"));
    colormapCombo->addItem(tr("inferno"));
    colormapCombo->addItem(tr("jet"));
    colormapCombo->setCurrentIndex(1);

    autoScaleCheck = new QCheckBox(tr("auto colour scale"));
    autoScaleCheck->setChecked(true);
    autoScaleCheck->setToolTip(tr("auto colour scale (full range); uncheck for a fixed dB range"));

    dynRangeSpin = new QDoubleSpinBox;
    dynRangeSpin->setRange(6.0, 100000.0); dynRangeSpin->setDecimals(0);
    dynRangeSpin->setValue(60.0);
    dynRangeSpin->setEnabled(false);   // auto is on by default
    dynRangeSpin->setToolTip(tr("dynamic range (dB) when auto is off"));

    backendCombo = new QComboBox;
    backendCombo->addItem(tr("CPU"));
    backendCombo->addItem(tr("GPU"));
    backendCombo->setCurrentIndex(0);

    // ── grouped vertical layout (tab-friendly) ──────────────────────────────
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(8);

    QGroupBox* estBox = new QGroupBox(tr("Estimator"));
    QFormLayout* est = new QFormLayout(estBox);
    est->addRow(tr("Mode"), modeCombo);
    est->addRow(tr("Channel row"), channelSpin);
    est->addRow(tr("NW"), nwSpin);
    est->addRow(tr("Tapers (K)"), taperSpin);
    est->addRow(tr("Window"), windowSpin);
    est->addRow(tr("FFT length"), nfftSpin);
    est->addRow(tr("Step"), stepSpin);
    root->addWidget(estBox);

    QGroupBox* bandBox = new QGroupBox(tr("Band (Hz)"));
    QFormLayout* band = new QFormLayout(bandBox);
    band->addRow(tr("Low"), freqLowSpin);
    band->addRow(tr("High"), freqHighSpin);
    root->addWidget(bandBox);

    QGroupBox* winBox = new QGroupBox(tr("Time window"));
    QFormLayout* win = new QFormLayout(winBox);
    win->addRow(lockCheck);
    win->addRow(tr("Span"), spanSpin);
    root->addWidget(winBox);

    QGroupBox* dispBox = new QGroupBox(tr("Display"));
    QFormLayout* disp = new QFormLayout(dispBox);
    disp->addRow(whitenCheck);
    disp->addRow(decimCheck);
    disp->addRow(carCheck);
    disp->addRow(refRegressCheck);
    disp->addRow(tr("Ref row"), refChannelSpin);
    disp->addRow(tr("Colormap"), colormapCombo);
    disp->addRow(autoScaleCheck);
    disp->addRow(tr("Range (dB)"), dynRangeSpin);
    disp->addRow(tr("Backend"), backendCombo);
    root->addWidget(dispBox);

    root->addStretch(1);

    // ── wire controls to the bound view (guarded so a null target is safe and
    //    so the connections survive retargeting via setView()) ───────────────
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i){ if(view) view->setSpectralMode(i == 1 ? SpectralMode::FrequencyAcrossChannels
                                                                 : SpectralMode::TimeFrequencySingleChannel); });
    connect(channelSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v){ if(view) view->setSingleChannelRow(v); });
    connect(nwSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v){ if(view) view->setTimeBandwidth(v); });
    connect(taperSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v){ if(view) view->setNumTapers(v); });
    connect(windowSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v){ if(view) view->setWindowSamples(v); });
    connect(nfftSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v){ if(view) view->setNfft(v); });
    connect(stepSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v){ if(view) view->setStep(v); });
    connect(whitenCheck, &QCheckBox::toggled, this,
            [this](bool on){ if(view) view->setWhitening(on); });
    connect(decimCheck, &QCheckBox::toggled, this,
            [this](bool on){ if(view) view->setDecimate(on); });
    connect(carCheck, &QCheckBox::toggled, this,
            [this](bool on){ if(view) view->setCar(on); });
    connect(refRegressCheck, &QCheckBox::toggled, this,
            [this](bool on){ refChannelSpin->setEnabled(on); if(view) view->setRefRegress(on); });
    connect(refChannelSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int v){ if(view) view->setRefChannel(v); });
    connect(dynRangeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v){ if(view) view->setDynamicRangeDb(v); });

    auto applyFreq = [this]{ if(view) view->setFrequencyRange(freqLowSpin->value(), freqHighSpin->value()); };
    connect(freqLowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [applyFreq](double){ applyFreq(); });
    connect(freqHighSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [applyFreq](double){ applyFreq(); });

    connect(colormapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i){ if(view) view->setColormap(static_cast<Colormap>(i)); });
    connect(backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int i){ if(view) view->setBackend(i == 1 ? SpectralBackend::Cuda
                                                            : SpectralBackend::Cpu); });

    connect(lockCheck, &QCheckBox::toggled, this, [this](bool on){
        spanSpin->setEnabled(!on);
        if(view) view->setLockToTrace(on);
    });
    connect(spanSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int ms){ if(view) view->setSpan(static_cast<long>(ms)); });

    connect(autoScaleCheck, &QCheckBox::toggled, this, [this](bool on){
        dynRangeSpin->setEnabled(!on);
        if(view) view->setAutoScale(on);
    });

    // Bind (or disable when null).
    setView(initialView);
}

void SpectralInspector::reloadFromView()
{
    if (!view) return;
    const neuroscope::spectral::SpectralParams& p = view->spectralParams();

    // Block every control while we mirror the view's state so the setters
    // above are not re-invoked during the reload.
    const QSignalBlocker b0(modeCombo),  b1(channelSpin), b2(nwSpin),
                         b3(taperSpin),  b4(windowSpin),  b5(nfftSpin),
                         b6(stepSpin),   b7(freqLowSpin), b8(freqHighSpin),
                         b9(lockCheck),  b10(spanSpin),   b11(whitenCheck),
                         b12(decimCheck),b13(colormapCombo), b14(autoScaleCheck),
                         b15(dynRangeSpin), b16(backendCombo),
                         b17(carCheck), b18(refRegressCheck), b19(refChannelSpin);

    modeCombo->setCurrentIndex(p.mode == SpectralMode::FrequencyAcrossChannels ? 1 : 0);
    channelSpin->setValue(p.singleChannel);
    nwSpin->setValue(p.nw);
    taperSpin->setValue(p.nTapers);
    windowSpin->setValue(p.windowSamples);
    nfftSpin->setValue(p.nfft);
    stepSpin->setValue(p.stepSamples);
    freqLowSpin->setValue(p.freqLow);
    freqHighSpin->setValue(p.freqHigh);
    whitenCheck->setChecked(p.whiten);
    decimCheck->setChecked(p.decimate);
    carCheck->setChecked(p.car);
    refRegressCheck->setChecked(p.refRegress);
    refChannelSpin->setValue(p.refChannel);
    refChannelSpin->setEnabled(p.refRegress);
    backendCombo->setCurrentIndex(p.backend == SpectralBackend::Cuda ? 1 : 0);

    colormapCombo->setCurrentIndex(static_cast<int>(view->colormapValue()));
    const bool autoOn = view->isAutoScale();
    autoScaleCheck->setChecked(autoOn);
    dynRangeSpin->setValue(view->dynamicRange());
    dynRangeSpin->setEnabled(!autoOn);

    const bool locked = view->isLockedToTrace();
    lockCheck->setChecked(locked);
    if (view->spanMs() > 0)
        spanSpin->setValue(static_cast<int>(view->spanMs()));
    spanSpin->setEnabled(!locked);
}

void SpectralInspector::setView(SpectralView* v)
{
    view = v;
    setEnabled(v != nullptr);
    if (v)
        reloadFromView();
}

void SpectralInspector::setChannelCount(int n)
{
    if (n < 1) n = 1;
    channelSpin->setRange(0, std::max(n - 1, 10000));
    refChannelSpin->setRange(0, std::max(n - 1, 10000));
}
