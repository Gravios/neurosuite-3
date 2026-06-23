#ifndef NEUROSCOPE_SPECTRALINSPECTOR_H
#define NEUROSCOPE_SPECTRALINSPECTOR_H

// Spectral parameter panel, shown as a tab in the channel-selection panel.
// One shared instance is retargeted to the active display's SpectralView via
// setView(); each control drives the currently bound view's setter, and the
// controls reload from the view whenever the target changes.

#include <QWidget>

class SpectralView;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;

class SpectralInspector : public QWidget {
    Q_OBJECT

public:
    explicit SpectralInspector(SpectralView* view = nullptr, QWidget* parent = nullptr);

    /// Bind the panel to a SpectralView (or nullptr to disable it). The
    /// controls reload from the new view and all subsequent edits drive it.
    void setView(SpectralView* view);

    /// Update the channel-row spin range when the displayed channels change.
    void setChannelCount(int n);

private:
    /// Reload every control from the bound view without firing its setters.
    void reloadFromView();

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
    QCheckBox*       lockCheck = nullptr;
    QSpinBox*        spanSpin = nullptr;
    QCheckBox*       whitenCheck = nullptr;
    QCheckBox*       decimCheck = nullptr;
    QComboBox*       colormapCombo = nullptr;
    QCheckBox*       autoScaleCheck = nullptr;
    QDoubleSpinBox*  dynRangeSpin = nullptr;
    QComboBox*       backendCombo = nullptr;
};

#endif // NEUROSCOPE_SPECTRALINSPECTOR_H
