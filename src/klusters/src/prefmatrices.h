#ifndef PREFMATRICES_H
#define PREFMATRICES_H

#include "prefmatriceslayout.h"

// Preferences ▸ Views ▸ Matrices: when the matrix displays are computed, the
// error matrix's compute options, the template matrix and the drift matrix.
class PrefMatrices : public PrefMatricesLayout
{
    Q_OBJECT
public:
    explicit PrefMatrices(QWidget* parent = nullptr);
    ~PrefMatrices() override = default;

    void   setAutoUpdateMatricesAfterMerge(bool b);
    bool   getAutoUpdateMatricesAfterMerge() const;
    void   setErrorMatrixIncremental(bool b);
    bool   getErrorMatrixIncremental() const;
    void   setErrorMatrixLowPrecision(bool b);
    bool   getErrorMatrixLowPrecision() const;
    void setAutoShowMatricesOnOpen(bool checked);
    bool getAutoShowMatricesOnOpen() const;
    void   setTemplateThresholdMin(double v);
    double getTemplateThresholdMin() const;
    void   setTemplateThresholdMax(double v);
    double getTemplateThresholdMax() const;
    void setTemplateXcorrMetric(int i);
    int  getTemplateXcorrMetric() const;
    void setDriftSliderMaxClusters(int n);
    int  getDriftSliderMaxClusters() const;

public Q_SLOTS:
    // Grey the template matrix and drift controls when Templates mode
    // (Display page) is off; PrefDialog connects that switch here.
    void setTemplateControlsEnabled(bool on);
};

#endif
