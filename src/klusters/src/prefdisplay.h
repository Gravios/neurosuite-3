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

    void   setTemplateThresholdMin(double v);
    double getTemplateThresholdMin() const;
    void   setTemplateThresholdMax(double v);
    double getTemplateThresholdMax() const;

    void setTemplateXcorrPearson(bool b);
    bool getTemplateXcorrPearson() const;
};

#endif
