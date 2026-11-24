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




void PrefDisplay::setUseWhiteColorDuringPrinting(bool b){ useWhiteColorPrinting->setChecked(b); }
bool PrefDisplay::useWhiteColorDuringPrinting() const   { return useWhiteColorPrinting->isChecked(); }

