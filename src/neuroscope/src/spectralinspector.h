#ifndef NEUROSCOPE_SPECTRALINSPECTOR_H
#define NEUROSCOPE_SPECTRALINSPECTOR_H

// Compact parameter strip shown below the channels while the spectral view is
// active. Each control drives a SpectralView setter directly; the widget is
// initialised from the view's current parameters.

#include <QWidget>

class SpectralView;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;

class SpectralInspector : public QWidget {
    Q_OBJECT

public:
    explicit SpectralInspector(SpectralView* view, QWidget* parent = nullptr);

    /// Update the channel-row spin range when the displayed channels change.
    void setChannelCount(int n);

private:
    SpectralView* view;

    QComboBox*       modeCombo = nullptr;
    QSpinBox*        channelSpin = nullptr;
    QDoubleSpinBox*  nwSpin = nullptr;
    QSpinBox*        taperSpin = nullptr;
    QSpinBox*        windowSpin = nullptr;
    QSpinBox*        nfftSpin = nullptr;
    QSpinBox*        stepSpin = nullptr;
    QDoubleSpinBox*  freqLowSpin = nullptr;
    QDoubleSpinBox*  freqHighSpin = nullptr;
    QCheckBox*       whitenCheck = nullptr;
    QComboBox*       colormapCombo = nullptr;
    QDoubleSpinBox*  dynRangeSpin = nullptr;
    QComboBox*       backendCombo = nullptr;
};

#endif // NEUROSCOPE_SPECTRALINSPECTOR_H
