#ifndef PREFSPLITTING_H
#define PREFSPLITTING_H

#include "prefsplittinglayout.h"
#include "configuration.h"

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

// Preferences ▸ Curation ▸ Splitting: DipSplit and the KNN voting split.
class PrefSplitting : public PrefSplittingLayout
{
    Q_OBJECT
public:
    explicit PrefSplitting(QWidget* parent = nullptr);
    ~PrefSplitting() override = default;


    /** Split Off Spurious Spikes: the group built in the constructor. */
    void setCoreCut(const Configuration::CoreCut& c);
    Configuration::CoreCut getCoreCut() const;

Q_SIGNALS:
    /** A Split Off Spurious Spikes knob was edited. */
    void coreCutChanged();

private:
    QDoubleSpinBox* coreCutOnsetSpin = nullptr;
    QDoubleSpinBox* coreCutRiseSpin = nullptr;
    QDoubleSpinBox* coreCutFlatStartSpin = nullptr;
    QDoubleSpinBox* coreCutFlatEndSpin = nullptr;
    QDoubleSpinBox* coreCutFallSpin = nullptr;
    QDoubleSpinBox* coreCutShiftSpin = nullptr;
    QSpinBox*       coreCutMaxOrderedSpin = nullptr;
    QSpinBox*       coreCutMinSpikesSpin = nullptr;
    QCheckBox*      coreCutGpuCheck = nullptr;
};

#endif
