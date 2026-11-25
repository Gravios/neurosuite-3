#include "prefdisplay.h"

PrefDisplay::PrefDisplay(QWidget* parent)
    : PrefDisplayLayout(parent)
{
    // Live-grey the template-specific controls as the master switch is toggled.
    connect(templatesModeCheckBox, &QAbstractButton::toggled,
            this, &PrefDisplay::setTemplateControlsEnabled);
    setTemplateControlsEnabled(templatesModeCheckBox->isChecked());
}


void PrefDisplay::setTemplateControlsEnabled(bool on)
{
    autoShowTemplateLibraryOnOpenCheckBox->setEnabled(on);
}






