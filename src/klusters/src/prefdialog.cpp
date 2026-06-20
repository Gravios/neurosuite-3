/***************************************************************************
                          prefdialog.cpp  -  description
                             -------------------
    begin                : Thu Dec 12 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                :

    Patch 0067: General tab split into five grouped tabs —
    Display, Session, Reclustering, Refinement, Auto-Merge.
 ***************************************************************************/

#include <QCheckBox>
#include <QLayout>
#include <QLabel>
#include <QMessageBox>

#include "prefdialog.h"

#include "configuration.h"
#include "prefdisplay.h"
#include "prefsession.h"
#include "prefreclustering.h"
#include "prefrefinement.h"
#include "prefautomerge.h"
#include "prefwaveformview.h"
#include "prefclusterview.h"
#include "channellist.h"
#include "config-klusters.h"
#include <qhelpviewer.h>

PrefDialog::PrefDialog(QWidget *parent, int nbChannels)
 : QPageDialog(parent)
{
    setButtons(Help | Default | Ok | Apply | Cancel);
    setDefaultButton(Ok);
    setFaceType(List);
    setWindowTitle(tr("Preferences"));
    setHelp("settings", "klusters");

    // ── Display ────────────────────────────────────────────────────
    QWidget* w = new QWidget(this);
    prefDisplay = new PrefDisplay(w);
    QPageWidgetItem* item = new QPageWidgetItem(prefDisplay, tr("Display"));
    item->setHeader(tr("Display Configuration"));
    item->setIcon(QIcon(":/icons/display"));
    addPage(item);

    // ── Session (crash recovery + undo) ────────────────────────────
    w = new QWidget(this);
    prefSession = new PrefSession(w);
    item = new QPageWidgetItem(prefSession, tr("Session"));
    item->setHeader(tr("Session Configuration"));
    item->setIcon(QIcon(":/icons/session"));
    addPage(item);

    // ── Reclustering (KlustaKwik) ──────────────────────────────────
    w = new QWidget(this);
    prefReclustering = new PrefReclustering(w);
    item = new QPageWidgetItem(prefReclustering, tr("Reclustering"));
    item->setHeader(tr("Reclustering Configuration"));
    item->setIcon(QIcon(":/icons/reclustering"));
    addPage(item);

    // ── Refinement (realign + DipSplit + KNN) ──────────────────────
    w = new QWidget(this);
    prefRefinement = new PrefRefinement(w);
    item = new QPageWidgetItem(prefRefinement, tr("Refinement"));
    item->setHeader(tr("Cluster Refinement Configuration"));
    item->setIcon(QIcon(":/icons/refinement"));
    addPage(item);

    // ── Auto-Merge (placeholder; populated in patch 0068) ──────────
    w = new QWidget(this);
    prefAutoMerge = new PrefAutoMerge(w);
    item = new QPageWidgetItem(prefAutoMerge, tr("Auto-Merge"));
    item->setHeader(tr("Auto-Merge Configuration"));
    item->setIcon(QIcon(":/icons/automerge"));
    addPage(item);

    // ── Cluster view ───────────────────────────────────────────────
    w = new QWidget(this);
    prefclusterView = new PrefClusterView(w);
    item = new QPageWidgetItem(prefclusterView, tr("Cluster view"));
    item->setHeader(tr("Cluster View configuration"));
    item->setIcon(QIcon(":/icons/clusterview"));
    addPage(item);

    // ── Waveform view ──────────────────────────────────────────────
    w = new QWidget(this);
    prefWaveformView = new PrefWaveformView(w, nbChannels);
    item = new QPageWidgetItem(prefWaveformView, tr("Waveform view"));
    item->setHeader(tr("Waveform View configuration"));
    item->setIcon(QIcon(":/icons/waveformview"));
    addPage(item);

    // ── Wire enableApply on every interactive widget ───────────────
    // Session
    connect(prefSession->crashRecoveryCheckBox,  &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefSession->crashRecoveryComboBox,  &QComboBox::activated,     this, &PrefDialog::enableApply);
    connect(prefSession->undoSpinBox,            &QSpinBox::valueChanged,   this, &PrefDialog::enableApply);

    // Reclustering
    connect(prefReclustering->reclusteringExecutableLineEdit,  &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefReclustering->reclusteringArgsLineEdit,        &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefReclustering->autoSelectFeaturesCheckBox,      &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefReclustering->autoSelectNFeaturesSpinBox,      &QSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefReclustering->reclusterMeanSubtractedSubdimCheckBox,
            &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefReclustering->reclusterChannelVarianceCheckBox,
            &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefReclustering->reclusterMedianWaveformResidualCheckBox,
            &QAbstractButton::clicked, this, &PrefDialog::enableApply);

    // Refinement (realign + dipsplit + knn)
    connect(prefRefinement->realignThresholdSpinBox,    &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefRefinement->realignIterationsSpinBox,   &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefRefinement->realignMaxShiftSpinBox,     &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefRefinement->realignModeOffRadio,        &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRefinement->curationLoggingCheckBox,    &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRefinement->autoRealignAfterMergeCheckBox, &QAbstractButton::toggled,  this, &PrefDialog::enableApply);
    connect(prefRefinement->autoRenumberAfterMergeCheckBox, &QAbstractButton::toggled,  this, &PrefDialog::enableApply);
    connect(prefRefinement->autoUpdateMatricesAfterMergeCheckBox, &QAbstractButton::toggled,  this, &PrefDialog::enableApply);
    connect(prefRefinement->realignModePcaRadio,        &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRefinement->realignModeRmsRadio,        &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRefinement->dipSplitMinSizeSpinBox,     &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefRefinement->dipSplitBloatFactorSpinBox, &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefRefinement->dipSplitValleyThreshSpinBox,&QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefRefinement->knnKSpinBox,                &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefRefinement->knnThresholdSpinBox,        &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefRefinement->knnMinNewSpinBox,           &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefRefinement->knnMinRefSpinBox,           &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);

    // Display
    connect(prefDisplay->backgroundColorButton,        &QColorButton::colorChanged,    this, &PrefDialog::enableApply);
    connect(prefDisplay->markerSizeSpinBox,            &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefDisplay->selectionLineWidthSpinBox,    &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefDisplay->autoscaleMarginSpinBox,       &QDoubleSpinBox::valueChanged,  this, &PrefDialog::enableApply);
    connect(prefDisplay->useWhiteColorPrinting,        &QAbstractButton::clicked,      this, &PrefDialog::enableApply);
    connect(prefDisplay->autoShowMatricesOnOpenCheckBox,&QAbstractButton::clicked,     this, &PrefDialog::enableApply);
    connect(prefDisplay->templateThresholdMinSpinBox,  &QDoubleSpinBox::valueChanged,  this, &PrefDialog::enableApply);
    connect(prefDisplay->templateThresholdMaxSpinBox,  &QDoubleSpinBox::valueChanged,  this, &PrefDialog::enableApply);
    connect(prefDisplay->templateXcorrMetricComboBox,  &QComboBox::currentIndexChanged, this, &PrefDialog::enableApply);

    // Auto-Merge (patch 0068)
    connect(prefAutoMerge->algoMeanRadio,             &QAbstractButton::toggled,      this, &PrefDialog::enableApply);
    connect(prefAutoMerge->algoMedianRadio,           &QAbstractButton::toggled,      this, &PrefDialog::enableApply);
    connect(prefAutoMerge->medianKSpinBox,            &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefAutoMerge->scoreThresholdSpinBox,     &QDoubleSpinBox::valueChanged,  this, &PrefDialog::enableApply);
    connect(prefAutoMerge->maxShiftSpinBox,           &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefAutoMerge->taperSpinBox,              &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefAutoMerge->minClusterSizeSpinBox,     &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefAutoMerge->scopeSelectedRadio,        &QAbstractButton::toggled,      this, &PrefDialog::enableApply);
    connect(prefAutoMerge->scopeAllActiveRadio,       &QAbstractButton::toggled,      this, &PrefDialog::enableApply);
    connect(prefAutoMerge->previewBeforeApplyCheckBox,&QAbstractButton::clicked,      this, &PrefDialog::enableApply);

    // Cluster + Waveform views (unchanged)
    connect(prefclusterView->intervalSpinBox,  &QSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefWaveformView->gainSpinBox,     &QSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefWaveformView, &PrefWaveformView::positionsChanged,      this, &PrefDialog::enableApply);

    connect(this, &QExtendDialog::applyClicked,   this, &PrefDialog::slotApply);
    connect(this, &QExtendDialog::defaultClicked, this, &PrefDialog::slotDefault);
    connect(this, &QExtendDialog::helpClicked,    this, &PrefDialog::slotHelp);

    applyEnable = false;
}

void PrefDialog::slotHelp()
{
    QHelpViewer* helpDialog = new QHelpViewer(this);
    helpDialog->setHtml(KLUSTER_DOC_PATH + QLatin1String("index.html"));
    helpDialog->setAttribute(Qt::WA_DeleteOnClose);
    helpDialog->show();
}

void PrefDialog::updateDialog()
{
    // Session
    prefSession->setCrashRecovery(configuration().isCrashRecovery());
    prefSession->setCrashRecoveryIndex(configuration().crashRecoveryIntervalIndex());
    prefSession->setNbUndo(configuration().getNbUndo());

    // Reclustering
    prefReclustering->setReclusteringExecutable(configuration().getReclusteringExecutable());
    prefReclustering->setReclusteringArguments(configuration().getReclusteringArguments());
    prefReclustering->setAutoSelectFeatures(configuration().getAutoSelectFeatures());
    prefReclustering->setAutoSelectNFeatures(configuration().getAutoSelectNFeatures());
    prefReclustering->setReclusterMeanSubtractedSubdim(configuration().getReclusterMeanSubtractedSubdim());
    prefReclustering->setReclusterChannelVariance(configuration().getReclusterChannelVariance());
    prefReclustering->setReclusterMedianWaveformResidual(configuration().getReclusterMedianWaveformResidual());

    // Refinement
    prefRefinement->setRealignThreshold(configuration().getRealignThreshold());
    prefRefinement->setRealignIterations(configuration().getRealignIterations());
    prefRefinement->setRealignMaxShift(configuration().getRealignMaxShift());
    prefRefinement->setRealignMode(configuration().getRealignMode());
    prefRefinement->setCurationLogging(configuration().getCurationLogging());
    prefRefinement->setRealignVerbose(configuration().getRealignVerbose());
    prefRefinement->setAutoRealignAfterMerge(configuration().getAutoRealignAfterMerge());
    prefRefinement->setAutoRenumberAfterMerge(configuration().getAutoRenumberAfterMerge());
    prefRefinement->setAutoUpdateMatricesAfterMerge(configuration().getAutoUpdateMatricesAfterMerge());
    prefRefinement->setDipSplitMinSize(configuration().getDipSplitMinSize());
    prefRefinement->setDipSplitBloatFactor(configuration().getDipSplitBloatFactor());
    prefRefinement->setDipSplitValleyThresh(configuration().getDipSplitValleyThresh());
    prefRefinement->setKnnK(configuration().getKnnK());
    prefRefinement->setKnnThreshold(configuration().getKnnThreshold());
    prefRefinement->setKnnMinNew(configuration().getKnnMinNew());
    prefRefinement->setKnnMinRef(configuration().getKnnMinRef());

    // Display
    prefDisplay->setBackgroundColor(configuration().getBackgroundColor());
    prefDisplay->setMarkerSize(configuration().getMarkerSize());
    prefDisplay->setSelectionLineWidth(configuration().getSelectionLineWidth());
    prefDisplay->setAutoscaleMarginPercent(configuration().getAutoscaleMarginPercent());
    prefDisplay->setUseWhiteColorDuringPrinting(configuration().getUseWhiteColorDuringPrinting());
    prefDisplay->setAutoShowMatricesOnOpen(configuration().getAutoShowMatricesOnOpen());
    prefDisplay->setTemplateThresholdMin(configuration().getTemplateThresholdMin());
    prefDisplay->setTemplateThresholdMax(configuration().getTemplateThresholdMax());
    prefDisplay->setTemplateXcorrMetric(configuration().getTemplateXcorrMetric());

    // Auto-Merge (patch 0068)
    prefAutoMerge->setAlgorithm(configuration().getAutoMergeAlgorithm());
    prefAutoMerge->setMedianK(configuration().getAutoMergeMedianK());
    prefAutoMerge->setScoreThreshold(configuration().getAutoMergeScoreThreshold());
    prefAutoMerge->setMaxShift(configuration().getAutoMergeMaxShift());
    prefAutoMerge->setTaperSamples(configuration().getAutoMergeTaperSamples());
    prefAutoMerge->setMinClusterSize(configuration().getAutoMergeMinClusterSize());
    prefAutoMerge->setScope(configuration().getAutoMergeScope());
    prefAutoMerge->setPreviewBeforeApply(configuration().getAutoMergePreviewBeforeApply());

    // Cluster + Waveform views
    prefclusterView->setTimeInterval(configuration().getTimeInterval());
    prefWaveformView->setGain(configuration().getGain());

    enableButtonApply(false);
    applyEnable = false;
}

void PrefDialog::updateConfiguration()
{
    // Session
    configuration().setCrashRecovery(prefSession->isCrashRecovery());
    configuration().setCrashRecoveryIndex(prefSession->crashRecoveryIntervalIndex());
    configuration().setNbUndo(prefSession->getNbUndo());

    // Reclustering
    configuration().setReclusteringExecutable(prefReclustering->getReclusteringExecutable());
    configuration().setReclusteringArguments(prefReclustering->getReclusteringArguments());
    configuration().setAutoSelectFeatures(prefReclustering->getAutoSelectFeatures());
    configuration().setAutoSelectNFeatures(prefReclustering->getAutoSelectNFeatures());
    configuration().setReclusterMeanSubtractedSubdim(prefReclustering->getReclusterMeanSubtractedSubdim());
    configuration().setReclusterChannelVariance(prefReclustering->getReclusterChannelVariance());
    configuration().setReclusterMedianWaveformResidual(prefReclustering->getReclusterMedianWaveformResidual());

    // Refinement
    configuration().setRealignThreshold(prefRefinement->getRealignThreshold());
    configuration().setRealignIterations(prefRefinement->getRealignIterations());
    configuration().setRealignMaxShift(prefRefinement->getRealignMaxShift());
    configuration().setRealignMode(prefRefinement->getRealignMode());
    configuration().setCurationLogging(prefRefinement->getCurationLogging());
    configuration().setRealignVerbose(prefRefinement->getRealignVerbose());
    configuration().setAutoRealignAfterMerge(prefRefinement->getAutoRealignAfterMerge());
    configuration().setAutoRenumberAfterMerge(prefRefinement->getAutoRenumberAfterMerge());
    configuration().setAutoUpdateMatricesAfterMerge(prefRefinement->getAutoUpdateMatricesAfterMerge());
    configuration().setDipSplitMinSize(prefRefinement->getDipSplitMinSize());
    configuration().setDipSplitBloatFactor(prefRefinement->getDipSplitBloatFactor());
    configuration().setDipSplitValleyThresh(prefRefinement->getDipSplitValleyThresh());
    configuration().setKnnK(prefRefinement->getKnnK());
    configuration().setKnnThreshold(prefRefinement->getKnnThreshold());
    configuration().setKnnMinNew(prefRefinement->getKnnMinNew());
    configuration().setKnnMinRef(prefRefinement->getKnnMinRef());

    // Display
    configuration().setBackgroundColor(prefDisplay->getBackgroundColor());
    configuration().setMarkerSize(prefDisplay->getMarkerSize());
    configuration().setSelectionLineWidth(prefDisplay->getSelectionLineWidth());
    configuration().setAutoscaleMarginPercent(prefDisplay->getAutoscaleMarginPercent());
    configuration().setUseWhiteColorDuringPrinting(prefDisplay->useWhiteColorDuringPrinting());
    configuration().setAutoShowMatricesOnOpen(prefDisplay->getAutoShowMatricesOnOpen());
    configuration().setTemplateThresholdMin(prefDisplay->getTemplateThresholdMin());
    configuration().setTemplateThresholdMax(prefDisplay->getTemplateThresholdMax());
    configuration().setTemplateXcorrMetric(prefDisplay->getTemplateXcorrMetric());

    // Auto-Merge (patch 0068)
    configuration().setAutoMergeAlgorithm(prefAutoMerge->getAlgorithm());
    configuration().setAutoMergeMedianK(prefAutoMerge->getMedianK());
    configuration().setAutoMergeScoreThreshold(prefAutoMerge->getScoreThreshold());
    configuration().setAutoMergeMaxShift(prefAutoMerge->getMaxShift());
    configuration().setAutoMergeTaperSamples(prefAutoMerge->getTaperSamples());
    configuration().setAutoMergeMinClusterSize(prefAutoMerge->getMinClusterSize());
    configuration().setAutoMergeScope(prefAutoMerge->getScope());
    configuration().setAutoMergePreviewBeforeApply(prefAutoMerge->getPreviewBeforeApply());

    // Cluster + Waveform views
    configuration().setTimeInterval(prefclusterView->getTimeInterval());
    configuration().setGain(prefWaveformView->getGain());
    configuration().setNbChannels(prefWaveformView->getNbChannels());
    configuration().setChannelPositions(prefWaveformView->getChannelPositions());

    enableButtonApply(false);
    applyEnable = false;
}

void PrefDialog::slotDefault()
{
    if (QMessageBox::question(this, tr("Set default options?"),
            tr("This will set the default options in ALL pages of the preferences dialog! Do you wish to continue?"),
            QMessageBox::RestoreDefaults | QMessageBox::Cancel) != QMessageBox::RestoreDefaults)
        return;

    prefSession->setCrashRecovery(configuration().isCrashRecoveryDefault());
    prefSession->setCrashRecoveryIndex(configuration().crashRecoveryIntervalIndexDefault());
    prefSession->setNbUndo(configuration().getNbUndoDefault());

    prefReclustering->setReclusteringExecutable(configuration().getReclusteringExecutableDefault());
    prefReclustering->setReclusteringArguments(configuration().getReclusteringArgumentsDefault());
    prefReclustering->setAutoSelectFeatures(configuration().getAutoSelectFeaturesDefault());
    prefReclustering->setAutoSelectNFeatures(configuration().getAutoSelectNFeaturesDefault());
    prefReclustering->setReclusterMeanSubtractedSubdim(configuration().getReclusterMeanSubtractedSubdimDefault());
    prefReclustering->setReclusterChannelVariance(configuration().getReclusterChannelVarianceDefault());
    prefReclustering->setReclusterMedianWaveformResidual(configuration().getReclusterMedianWaveformResidualDefault());

    prefRefinement->setRealignThreshold(configuration().getRealignThresholdDefault());
    prefRefinement->setRealignIterations(configuration().getRealignIterationsDefault());
    prefRefinement->setRealignMaxShift(configuration().getRealignMaxShiftDefault());
    prefRefinement->setRealignMode(configuration().getRealignModeDefault());
    prefRefinement->setCurationLogging(configuration().getCurationLoggingDefault());
    prefRefinement->setRealignVerbose(configuration().getRealignVerboseDefault());
    prefRefinement->setAutoRealignAfterMerge(configuration().getAutoRealignAfterMergeDefault());
    prefRefinement->setAutoRenumberAfterMerge(configuration().getAutoRenumberAfterMergeDefault());
    prefRefinement->setAutoUpdateMatricesAfterMerge(configuration().getAutoUpdateMatricesAfterMergeDefault());
    prefRefinement->setDipSplitMinSize(configuration().getDipSplitMinSizeDefault());
    prefRefinement->setDipSplitBloatFactor(configuration().getDipSplitBloatFactorDefault());
    prefRefinement->setDipSplitValleyThresh(configuration().getDipSplitValleyThreshDefault());
    prefRefinement->setKnnK(configuration().getKnnKDefault());
    prefRefinement->setKnnThreshold(configuration().getKnnThresholdDefault());
    prefRefinement->setKnnMinNew(configuration().getKnnMinNewDefault());
    prefRefinement->setKnnMinRef(configuration().getKnnMinRefDefault());

    prefDisplay->setBackgroundColor(configuration().getBackgroundColorDefault());
    prefDisplay->setMarkerSize(configuration().getMarkerSizeDefault());
    prefDisplay->setSelectionLineWidth(configuration().getSelectionLineWidthDefault());
    prefDisplay->setAutoscaleMarginPercent(configuration().getAutoscaleMarginPercentDefault());
    prefDisplay->setUseWhiteColorDuringPrinting(configuration().getUseWhiteColorDuringPrinting());
    prefDisplay->setAutoShowMatricesOnOpen(configuration().getAutoShowMatricesOnOpenDefault());

    // Auto-Merge (patch 0068) — defaults match KKE flag defaults.
    prefAutoMerge->setAlgorithm(configuration().getAutoMergeAlgorithmDefault());
    prefAutoMerge->setMedianK(configuration().getAutoMergeMedianKDefault());
    prefAutoMerge->setScoreThreshold(configuration().getAutoMergeScoreThresholdDefault());
    prefAutoMerge->setMaxShift(configuration().getAutoMergeMaxShiftDefault());
    prefAutoMerge->setTaperSamples(configuration().getAutoMergeTaperSamplesDefault());
    prefAutoMerge->setMinClusterSize(configuration().getAutoMergeMinClusterSizeDefault());
    prefAutoMerge->setScope(configuration().getAutoMergeScopeDefault());
    prefAutoMerge->setPreviewBeforeApply(configuration().getAutoMergePreviewBeforeApplyDefault());

    prefclusterView->setTimeInterval(configuration().getTimeIntervalDefault());
    prefWaveformView->setGain(configuration().getGainDefault());
    prefWaveformView->resetChannelList(configuration().getNbChannels());

    enableApply();
}

void PrefDialog::slotApply()
{
    updateConfiguration();
    emit settingsChanged();
    enableButtonApply(false);
}

void PrefDialog::enableApply()
{
    enableButtonApply(true);
    applyEnable = true;
}

void PrefDialog::syncAutoNFeatures(int n)
{
    prefReclustering->setAutoSelectNFeatures(n);
}

void PrefDialog::resetChannelList(int nbChannels)
{
    prefWaveformView->resetChannelList(nbChannels);
}

void PrefDialog::enableChannelSettings(bool state)
{
    prefWaveformView->enableChannelSettings(state);
}
