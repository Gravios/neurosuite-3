#ifndef PREFDISPLAY_H
#define PREFDISPLAY_H

#include "prefdisplaylayout.h"
#include <QColor>

class PrefDisplay : public PrefDisplayLayout
{
    Q_OBJECT
public:
    explicit PrefDisplay(QWidget* parent = nullptr);
    ~PrefDisplay() override = default;

    void   setBackgroundColor(const QColor& color);
    QColor getBackgroundColor() const;

    void setMarkerSize(int size);
    int  getMarkerSize() const;

    void setSelectionLineWidth(int w);
    int  getSelectionLineWidth() const;

    void   setAutoscaleMarginPercent(double p);
    double getAutoscaleMarginPercent() const;

    void setUseWhiteColorDuringPrinting(bool b);
    bool useWhiteColorDuringPrinting() const;

    void setAutoShowMatricesOnOpen(bool checked);
    bool getAutoShowMatricesOnOpen() const;

    void setAutoShowTemplateLibraryOnOpen(bool checked);
    bool getAutoShowTemplateLibraryOnOpen() const;

    void setTemplatesMode(bool checked);
    bool getTemplatesMode() const;

    void   setTemplateThresholdMin(double v);
    double getTemplateThresholdMin() const;
    void   setTemplateThresholdMax(double v);
    double getTemplateThresholdMax() const;

    void setTemplateXcorrMetric(int i);
    int  getTemplateXcorrMetric() const;

    void setDriftSliderMaxClusters(int n);
    int  getDriftSliderMaxClusters() const;

private:
    // Grey the template-specific controls (library auto-show, matrix tuning,
    // drift slider) when Templates mode is off.
    void setTemplateControlsEnabled(bool on);
};

#endif
