#include "prefdisplay.h"

PrefDisplay::PrefDisplay(QWidget* parent)
    : PrefDisplayLayout(parent)
{
    // Live-grey the template-specific controls as the master switch is toggled.
    connect(templatesModeCheckBox, &QAbstractButton::toggled,
            this, &PrefDisplay::setTemplateControlsEnabled);
    setTemplateControlsEnabled(templatesModeCheckBox->isChecked());
}

void PrefDisplay::setTemplatesMode(bool c)
{
    templatesModeCheckBox->setChecked(c);
    setTemplateControlsEnabled(c);
}
bool PrefDisplay::getTemplatesMode() const { return templatesModeCheckBox->isChecked(); }

void PrefDisplay::setTemplateControlsEnabled(bool on)
{
    autoShowTemplateLibraryOnOpenCheckBox->setEnabled(on);
}

void   PrefDisplay::setBackgroundColor(const QColor& c) { backgroundColorButton->setColor(c); }
QColor PrefDisplay::getBackgroundColor() const          { return backgroundColorButton->color(); }

void PrefDisplay::setMarkerSize(int s)                  { markerSizeSpinBox->setValue(s); }
int  PrefDisplay::getMarkerSize() const                 { return markerSizeSpinBox->value(); }

void PrefDisplay::setSelectionLineWidth(int w)          { selectionLineWidthSpinBox->setValue(w); }
int  PrefDisplay::getSelectionLineWidth() const         { return selectionLineWidthSpinBox->value(); }

void   PrefDisplay::setAutoscaleMarginPercent(double p) { autoscaleMarginSpinBox->setValue(p); }
double PrefDisplay::getAutoscaleMarginPercent() const   { return autoscaleMarginSpinBox->value(); }

void PrefDisplay::setUseWhiteColorDuringPrinting(bool b){ useWhiteColorPrinting->setChecked(b); }
bool PrefDisplay::useWhiteColorDuringPrinting() const   { return useWhiteColorPrinting->isChecked(); }

void PrefDisplay::setAutoShowTemplateLibraryOnOpen(bool c) { autoShowTemplateLibraryOnOpenCheckBox->setChecked(c); }
bool PrefDisplay::getAutoShowTemplateLibraryOnOpen() const { return autoShowTemplateLibraryOnOpenCheckBox->isChecked(); }
