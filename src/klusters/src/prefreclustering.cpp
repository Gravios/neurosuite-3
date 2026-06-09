#include "prefreclustering.h"
#include <QIcon>
#include <QFileDialog>

PrefReclustering::PrefReclustering(QWidget* parent)
    : PrefReclusteringLayout(parent)
{
    connect(reclusteringExecutableButton, &QAbstractButton::clicked,
            this, &PrefReclustering::updateReclusteringExecutable);
    reclusteringExecutableButton->setIcon(QIcon(":/shared-icons/folder-open"));
}

void PrefReclustering::setReclusteringExecutable(const QString& e)  { reclusteringExecutableLineEdit->setText(e); }
void PrefReclustering::setReclusteringArguments(const QString& a)   { reclusteringArgsLineEdit->setText(a); }
void PrefReclustering::setAutoSelectFeatures(bool c)                { autoSelectFeaturesCheckBox->setChecked(c); }
void PrefReclustering::setAutoSelectNFeatures(int n)                { autoSelectNFeaturesSpinBox->setValue(n); }
void PrefReclustering::setReclusterMeanSubtractedSubdim(bool c)     { reclusterMeanSubtractedSubdimCheckBox->setChecked(c); }
void PrefReclustering::setReclusterChannelVariance(bool c)         { reclusterChannelVarianceCheckBox->setChecked(c); }

QString PrefReclustering::getReclusteringExecutable() const    { return reclusteringExecutableLineEdit->text(); }
QString PrefReclustering::getReclusteringArguments() const     { return reclusteringArgsLineEdit->text(); }
bool    PrefReclustering::getAutoSelectFeatures() const        { return autoSelectFeaturesCheckBox->isChecked(); }
int     PrefReclustering::getAutoSelectNFeatures() const       { return autoSelectNFeaturesSpinBox->value(); }
bool    PrefReclustering::getReclusterMeanSubtractedSubdim() const { return reclusterMeanSubtractedSubdimCheckBox->isChecked(); }
bool    PrefReclustering::getReclusterChannelVariance() const     { return reclusterChannelVarianceCheckBox->isChecked(); }

void PrefReclustering::updateReclusteringExecutable()
{
    const QString executable = QFileDialog::getOpenFileName(this, tr("Select the Reclustering executable..."));
    if (!executable.isEmpty()) setReclusteringExecutable(executable);
}
