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

    void setAutoShowTemplateLibraryOnOpen(bool checked);
    bool getAutoShowTemplateLibraryOnOpen() const;

    void setTemplatesMode(bool checked);
    bool getTemplatesMode() const;

private:
    // Grey the Template Library auto-show when Templates mode is off (the
    // template matrix and drift controls on the Matrices page follow the same
    // switch, through PrefDialog).
    void setTemplateControlsEnabled(bool on);
};

#endif
