#include "prefsession.h"

PrefSession::PrefSession(QWidget* parent)
    : PrefSessionLayout(parent)
{
    connect(crashRecoveryCheckBox, &QCheckBox::stateChanged,
            this, &PrefSession::updateCrashRecoveryTimeInterval);
    // The interval follows the box from the start: a value loaded equal to the .ui's sends no signal.
    updateCrashRecoveryTimeInterval(crashRecoveryCheckBox->checkState());
}


void PrefSession::updateCrashRecoveryTimeInterval(int state)
{
    crashRecoveryComboBox->setEnabled(state == Qt::Checked);
}

