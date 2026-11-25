#include "prefmatrices.h"

PrefMatrices::PrefMatrices(QWidget* parent)
    : PrefMatricesLayout(parent)
{
}

void PrefMatrices::setTemplateControlsEnabled(bool on)
{
    groupBox_templateMatrix->setEnabled(on);
    groupBoxTemplateMatrix->setEnabled(on);
    groupBoxDriftMatrix->setEnabled(on);
}

