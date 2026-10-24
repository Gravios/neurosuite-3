/***************************************************************************
                          prefdialog.cpp  -  description
                             -------------------
    begin                : Thu Dec 12 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                :

    The pages form a tree of three groups (General, Views, Curation); see
    the constructor.
 ***************************************************************************/

#include <QCheckBox>
#include <QComboBox>
#include <QLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>

#include "prefdialog.h"

#include "configuration.h"
#include "prefdisplay.h"
#include "prefsession.h"
#include "prefreclustering.h"
#include "prefrealignment.h"
#include "prefsplitting.h"
#include "prefmerging.h"
#include "prefclusterorder.h"
#include "prefmatrices.h"
#include "prefwaveformview.h"
#include "prefclusterview.h"
#include "input/prefinput.h"   // S6: relocated to libklustersshared
#include "prefradialmenu.h"
#include "radiallayout.h"
#include "channellist.h"
#include "config-klusters.h"
#include <qhelpviewer.h>

#include <klustersshared/theme.h>

PrefDialog::PrefDialog(QWidget *parent, int nbChannels)
 : QPageDialog(parent)
{
    setButtons(Help | Default | Ok | Apply | Cancel);
    setDefaultButton(Ok);
    setFaceType(Tree);
    setWindowTitle(tr("Preferences"));
    setHelp("settings", "klusters");

    // Three groups, each a short page listing what is under it:
    //   General  — Appearance, Session, Input, Radial menu
    //   Views    — Display, Cluster view, Waveform view, Matrices
    //   Curation — Realignment, Splitting, Merging, Cluster order, Reclustering
    auto groupPage = [this](const QString& name, const QString& text) {
        QLabel* label = new QLabel(text, this);
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        QPageWidgetItem* g = new QPageWidgetItem(label, name);
        g->setHeader(name);
        g->setIcon(QIcon(":/shared-icons/folder-open"));
        addPage(g);
        return g;
    };
    auto subPage = [this](QPageWidgetItem* parentItem, QWidget* page, const QString& name,
                          const QString& header, const QString& icon) {
        QPageWidgetItem* it = new QPageWidgetItem(page, name);
        it->setHeader(header);
        it->setIcon(QIcon(icon));
        addSubPage(parentItem, it);
        return it;
    };

    // ── General ────────────────────────────────────────────────────
    QPageWidgetItem* general = groupPage(tr("General"),
        tr("Settings for the program as a whole: its appearance, autosave, undo, saving and the "
           "curation log, and the keyboard, mouse and right-click wheel."));

    // Appearance (suite-wide light/dark/system theme)
    QWidget* appearance = new QWidget(this);
    QVBoxLayout* appearanceLayout = new QVBoxLayout(appearance);
    QHBoxLayout* themeRow = new QHBoxLayout;
    QLabel* themeLabel = new QLabel(tr("Theme:"), appearance);
    themeCombo = new QComboBox(appearance);
    themeCombo->addItem(neurosuite::themeDisplayName(neurosuite::Theme::System),
                        static_cast<int>(neurosuite::Theme::System));
    themeCombo->addItem(neurosuite::themeDisplayName(neurosuite::Theme::Light),
                        static_cast<int>(neurosuite::Theme::Light));
    themeCombo->addItem(neurosuite::themeDisplayName(neurosuite::Theme::Dark),
                        static_cast<int>(neurosuite::Theme::Dark));
    themeRow->addWidget(themeLabel);
    themeRow->addWidget(themeCombo);
    themeRow->addStretch(1);
    QLabel* themeHint = new QLabel(
        tr("Applies to all NeuroSuite programs. Takes effect when you click Apply or OK."),
        appearance);
    themeHint->setWordWrap(true);
    appearanceLayout->addLayout(themeRow);
    appearanceLayout->addWidget(themeHint);
    appearanceLayout->addStretch(1);
    subPage(general, appearance, tr("Appearance"), tr("Appearance"), ":/shared-icons/folder-open");

    prefSession = new PrefSession(this);
    subPage(general, prefSession, tr("Session"), tr("Autosave, Undo, Saving and the Curation Log"),
            ":/icons/session");

    prefInput = new PrefInput(configuration(), this);   // S6: pass the app's InputPrefsStore
    subPage(general, prefInput, tr("Input"), tr("Keyboard & Mouse Bindings"),
            ":/shared-icons/folder-open");

    prefRadialMenu = new PrefRadialMenu(this);
    subPage(general, prefRadialMenu, tr("Radial menu"), tr("Right-click Wheel"),
            ":/shared-icons/folder-open");

    // ── Views ──────────────────────────────────────────────────────
    QPageWidgetItem* views = groupPage(tr("Views"),
        tr("How the data are drawn: the common display settings, the cluster and waveform "
           "views, and the error, template and drift matrices."));

    prefDisplay = new PrefDisplay(this);
    subPage(views, prefDisplay, tr("Display"), tr("Display Configuration"), ":/icons/display");

    prefclusterView = new PrefClusterView(this);
    subPage(views, prefclusterView, tr("Cluster view"), tr("Cluster View Configuration"),
            ":/icons/cluster-view");

    prefWaveformView = new PrefWaveformView(this, nbChannels);
    subPage(views, prefWaveformView, tr("Waveform view"), tr("Waveform View Configuration"),
            ":/icons/waveform-view");

    prefMatrices = new PrefMatrices(this);
    subPage(views, prefMatrices, tr("Matrices"), tr("Error, Template and Drift Matrices"),
            ":/icons/display");
    // Templates mode (Display) greys the template-matrix and drift controls here.
    connect(prefDisplay->templatesModeCheckBox, &QAbstractButton::toggled,
            prefMatrices, &PrefMatrices::setTemplateControlsEnabled);
    prefMatrices->setTemplateControlsEnabled(prefDisplay->templatesModeCheckBox->isChecked());

    // ── Curation ───────────────────────────────────────────────────
    QPageWidgetItem* curation = groupPage(tr("Curation"),
        tr("The curation tools: realignment, splitting, merging, the order and numbering of "
           "clusters, and reclustering with KiloKlustaKwik."));

    prefRealignment = new PrefRealignment(this);
    subPage(curation, prefRealignment, tr("Realignment"), tr("Spike Realignment"), ":/icons/refinement");

    prefSplitting = new PrefSplitting(this);
    subPage(curation, prefSplitting, tr("Splitting"), tr("Cluster Splitting"), ":/icons/refinement");

    prefMerging = new PrefMerging(this);
    subPage(curation, prefMerging, tr("Merging"), tr("Recommended Merges and Auto-Merge"),
            ":/icons/auto-merge-settings");

    prefClusterOrder = new PrefClusterOrder(this);
    subPage(curation, prefClusterOrder, tr("Cluster order"), tr("Cluster Order and Numbering"),
            ":/icons/refinement");

    prefReclustering = new PrefReclustering(this);
    subPage(curation, prefReclustering, tr("Reclustering"), tr("Reclustering Configuration"),
            ":/icons/reclustering");

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
    connect(prefRealignment->realignThresholdSpinBox,    &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefRealignment->realignIterationsSpinBox,   &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefRealignment->realignMaxShiftSpinBox,     &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefRealignment->realignModeOffRadio,        &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefSession->curationLoggingCheckBox,    &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRealignment->syncFeatureSpaceOnSwitchCheckBox, &QAbstractButton::toggled, this, &PrefDialog::enableApply);
    connect(prefRealignment->realignFromAnySpaceCheckBox, &QAbstractButton::toggled, this, &PrefDialog::enableApply);
    connect(prefClusterOrder->reorderDisplayOnlyCheckBox,    &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRealignment->autoRealignAfterMergeCheckBox, &QAbstractButton::toggled,  this, &PrefDialog::enableApply);
    connect(prefClusterOrder->autoRenumberAfterMergeCheckBox, &QAbstractButton::toggled,  this, &PrefDialog::enableApply);
    connect(prefMatrices->autoUpdateMatricesAfterMergeCheckBox, &QAbstractButton::toggled,  this, &PrefDialog::enableApply);
    connect(prefMatrices->errorMatrixIncrementalCheckBox,  &QAbstractButton::toggled, this, &PrefDialog::enableApply);
    connect(prefMatrices->errorMatrixLowPrecisionCheckBox, &QAbstractButton::toggled, this, &PrefDialog::enableApply);
    connect(prefRealignment->realignModePcaRadio,        &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRealignment->realignModeRmsRadio,        &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRealignment->realignModeCentroidRadio,   &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRealignment->realignVerboseCheckBox,     &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefSession->reextractSpikesOnSaveCheckBox, &QAbstractButton::toggled,  this, &PrefDialog::enableApply);
    connect(prefClusterOrder->reorderMethodComboBox,         &QComboBox::currentIndexChanged, this, &PrefDialog::enableApply);
    connect(prefClusterOrder->waveformSortStatisticComboBox, &QComboBox::currentIndexChanged, this, &PrefDialog::enableApply);
    connect(prefMerging->mergeRecommendMaxSpinBox,          &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefMerging->mergeRecommendErrorFloorSpinBox,   &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefMerging->mergeRecommendQualityFloorSpinBox, &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefMerging->showMergeRecommendPanelCheckBox,   &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefMatrices->driftSliderMaxClustersSpinBox,     &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefSplitting->dipSplitMinSizeSpinBox,     &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefSplitting->dipSplitBloatFactorSpinBox, &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefSplitting->dipSplitValleyThreshSpinBox,&QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefSplitting->knnKSpinBox,                &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefSplitting->knnThresholdSpinBox,        &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefSplitting->knnMinNewSpinBox,           &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);
    connect(prefSplitting->knnMinRefSpinBox,           &QSpinBox::valueChanged,       this, &PrefDialog::enableApply);

    // Display
    connect(prefDisplay->backgroundColorButton,        &QColorButton::colorChanged,    this, &PrefDialog::enableApply);
    connect(prefDisplay->markerSizeSpinBox,            &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefDisplay->selectionLineWidthSpinBox,    &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefDisplay->autoscaleMarginSpinBox,       &QDoubleSpinBox::valueChanged,  this, &PrefDialog::enableApply);
    connect(prefDisplay->useWhiteColorPrinting,        &QAbstractButton::clicked,      this, &PrefDialog::enableApply);
    connect(prefMatrices->autoShowMatricesOnOpenCheckBox,&QAbstractButton::clicked,     this, &PrefDialog::enableApply);
    connect(prefDisplay->autoShowTemplateLibraryOnOpenCheckBox,&QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefDisplay->templatesModeCheckBox,        &QAbstractButton::clicked,      this, &PrefDialog::enableApply);
    connect(prefMatrices->templateThresholdMinSpinBox,  &QDoubleSpinBox::valueChanged,  this, &PrefDialog::enableApply);
    connect(prefMatrices->templateThresholdMaxSpinBox,  &QDoubleSpinBox::valueChanged,  this, &PrefDialog::enableApply);
    connect(prefMatrices->templateXcorrMetricComboBox,  &QComboBox::currentIndexChanged, this, &PrefDialog::enableApply);

    // Auto-Merge (patch 0068)
    connect(prefMerging->algoMeanRadio,             &QAbstractButton::toggled,      this, &PrefDialog::enableApply);
    connect(prefMerging->algoMedianRadio,           &QAbstractButton::toggled,      this, &PrefDialog::enableApply);
    connect(prefMerging->medianKSpinBox,            &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefMerging->scoreThresholdSpinBox,     &QDoubleSpinBox::valueChanged,  this, &PrefDialog::enableApply);
    connect(prefMerging->maxShiftSpinBox,           &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefMerging->taperSpinBox,              &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefMerging->minClusterSizeSpinBox,     &QSpinBox::valueChanged,        this, &PrefDialog::enableApply);
    connect(prefMerging->scopeSelectedRadio,        &QAbstractButton::toggled,      this, &PrefDialog::enableApply);
    connect(prefMerging->scopeAllActiveRadio,       &QAbstractButton::toggled,      this, &PrefDialog::enableApply);
    connect(prefMerging->previewBeforeApplyCheckBox,&QAbstractButton::clicked,      this, &PrefDialog::enableApply);
    connect(prefMerging->useErrorMatrixCheckBox,    &QAbstractButton::clicked,      this, &PrefDialog::enableApply);
    connect(prefMerging->errorProbThresholdSpinBox, &QDoubleSpinBox::valueChanged,  this, &PrefDialog::enableApply);

    // Cluster + Waveform views (unchanged)
    connect(prefclusterView->intervalSpinBox,  &QSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->tsneCapLineEdit,  &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->tsneStepLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->tsneStartPerpLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->tsneItersLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->tsneThetaLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->tsneDimsLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->tsneEtaLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->tsneExagLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->tsneExagItersLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->tsneSubsampleCheckBox, &QCheckBox::toggled, this, &PrefDialog::enableApply);
    connect(prefclusterView->tsneRandomSeedCheckBox, &QCheckBox::toggled, this, &PrefDialog::enableApply);
    // Template projection scope and the density heatmap: these settings were added to
    // the page without a change signal here, so editing only them left Apply disabled.
    connect(prefclusterView->projModeCombo,  &QComboBox::currentIndexChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->projScopeMinutesLineEdit, &QLineEdit::textChanged, this, &PrefDialog::enableApply);
    connect(prefclusterView->projHideOutOfScopeCheckBox, &QCheckBox::toggled, this, &PrefDialog::enableApply);
    connect(prefclusterView->showCollisionsCheckBox,     &QCheckBox::toggled, this, &PrefDialog::enableApply);
    connect(prefclusterView->showClassMembersCheckBox,   &QCheckBox::toggled, this, &PrefDialog::enableApply);
    connect(prefclusterView->densitySigmaSpinBox, &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefWaveformView->gainSpinBox,     &QSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefWaveformView, &PrefWaveformView::positionsChanged,      this, &PrefDialog::enableApply);

    // Input bindings: an edited shortcut lights Apply (PrefInput blocks signals
    // during its programmatic load, so this fires only on real user edits).
    connect(prefInput, &PrefInput::changed, this, &PrefDialog::enableApply);
    // Radial menu: any edit of the wheel lights Apply (setLayout() is silent).
    connect(prefRadialMenu, &PrefRadialMenu::changed, this, &PrefDialog::enableApply);

    // Appearance: activated fires only on user choice (not the programmatic
    // populate in updateDialog), so it lights Apply like the other controls.
    connect(themeCombo, &QComboBox::activated, this, &PrefDialog::enableApply);

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

void PrefDialog::setRadialCatalog(const QList<ActionCatalogEntry>& entries)
{
    prefRadialMenu->setCatalog(entries);
}

void PrefDialog::updateDialog()
{
    // Radial menu: the stored layout (or the shipped one).
    prefRadialMenu->setLayout(radiallayout::fromSettings(configuration().getRadialMenuLayout()));

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
    prefRealignment->setRealignThreshold(configuration().getRealignThreshold());
    prefRealignment->setRealignIterations(configuration().getRealignIterations());
    prefRealignment->setRealignMaxShift(configuration().getRealignMaxShift());
    prefRealignment->setRealignMode(configuration().getRealignMode());
    prefClusterOrder->setReorderMethod(configuration().getReorderMethod());
    prefClusterOrder->setWaveformSortMean(configuration().getWaveformSortMean());
    prefMerging->setMergeRecommendMax(configuration().getMergeRecommendMax());
    prefMerging->setMergeRecommendErrorFloor(configuration().getMergeRecommendErrorFloor());
    prefMerging->setMergeRecommendQualityFloor(configuration().getMergeRecommendQualityFloor());
    prefMerging->setShowMergeRecommendPanel(configuration().getShowMergeRecommendPanel());
    prefMatrices->setDriftSliderMaxClusters(configuration().getDriftSliderMaxClusters());
    prefSession->setCurationLogging(configuration().getCurationLogging());
    prefClusterOrder->setReorderDisplayOnly(configuration().getReorderDisplayOnly());
    prefRealignment->setRealignVerbose(configuration().getRealignVerbose());
    prefSession->setReextractSpikesOnSave(configuration().getReextractSpikesOnSave());
    prefRealignment->setSyncFeatureSpaceOnSwitch(configuration().getSyncFeatureSpaceOnSwitch());
    prefRealignment->setRealignFromAnySpace(configuration().getRealignFromAnySpace());
    prefRealignment->setAutoRealignAfterMerge(configuration().getAutoRealignAfterMerge());
    prefClusterOrder->setAutoRenumberAfterMerge(configuration().getAutoRenumberAfterMerge());
    prefMatrices->setAutoUpdateMatricesAfterMerge(configuration().getAutoUpdateMatricesAfterMerge());
    prefMatrices->setErrorMatrixIncremental(configuration().getErrorMatrixIncremental());
    prefMatrices->setErrorMatrixLowPrecision(configuration().getErrorMatrixLowPrecision());
    prefSplitting->setDipSplitMinSize(configuration().getDipSplitMinSize());
    prefSplitting->setDipSplitBloatFactor(configuration().getDipSplitBloatFactor());
    prefSplitting->setDipSplitValleyThresh(configuration().getDipSplitValleyThresh());
    prefSplitting->setKnnK(configuration().getKnnK());
    prefSplitting->setKnnThreshold(configuration().getKnnThreshold());
    prefSplitting->setKnnMinNew(configuration().getKnnMinNew());
    prefSplitting->setKnnMinRef(configuration().getKnnMinRef());

    // Display
    prefDisplay->setBackgroundColor(configuration().getBackgroundColor());
    prefDisplay->setMarkerSize(configuration().getMarkerSize());
    prefDisplay->setSelectionLineWidth(configuration().getSelectionLineWidth());
    prefDisplay->setAutoscaleMarginPercent(configuration().getAutoscaleMarginPercent());
    prefDisplay->setUseWhiteColorDuringPrinting(configuration().getUseWhiteColorDuringPrinting());
    prefMatrices->setAutoShowMatricesOnOpen(configuration().getAutoShowMatricesOnOpen());
    prefDisplay->setAutoShowTemplateLibraryOnOpen(configuration().getAutoShowTemplateLibraryOnOpen());
    prefDisplay->setTemplatesMode(configuration().getTemplatesMode());
    prefMatrices->setTemplateThresholdMin(configuration().getTemplateThresholdMin());
    prefMatrices->setTemplateThresholdMax(configuration().getTemplateThresholdMax());
    prefMatrices->setTemplateXcorrMetric(configuration().getTemplateXcorrMetric());

    // Auto-Merge (patch 0068)
    prefMerging->setAlgorithm(configuration().getAutoMergeAlgorithm());
    prefMerging->setMedianK(configuration().getAutoMergeMedianK());
    prefMerging->setScoreThreshold(configuration().getAutoMergeScoreThreshold());
    prefMerging->setUseErrorMatrix(configuration().getAutoMergeUseErrorMatrix());
    prefMerging->setErrorProbThreshold(configuration().getAutoMergeErrorProbThreshold());
    prefMerging->setMaxShift(configuration().getAutoMergeMaxShift());
    prefMerging->setTaperSamples(configuration().getAutoMergeTaperSamples());
    prefMerging->setMinClusterSize(configuration().getAutoMergeMinClusterSize());
    prefMerging->setScope(configuration().getAutoMergeScope());
    prefMerging->setPreviewBeforeApply(configuration().getAutoMergePreviewBeforeApply());

    // Cluster + Waveform views
    prefclusterView->setTimeInterval(configuration().getTimeInterval());
    prefclusterView->setTsneSpikeCap(configuration().getTsneSpikeCap());
    prefclusterView->setTsnePerplexityStep(configuration().getTsnePerplexityStep());
    prefclusterView->setTsneStartPerplexity(configuration().getTsneStartPerplexity());
    prefclusterView->setTsneIterations(configuration().getTsneIterations());
    prefclusterView->setTsneTheta(configuration().getTsneTheta());
    prefclusterView->setTsneMaxDimensions(configuration().getTsneMaxDimensions());
    prefclusterView->setTsneLearningRate(configuration().getTsneLearningRate());
    prefclusterView->setTsneExaggeration(configuration().getTsneExaggeration());
    prefclusterView->setTsneExaggerationIterations(configuration().getTsneExaggerationIterations());
    prefclusterView->setTsneSubsampleOverCap(configuration().getTsneSubsampleOverCap());
    prefclusterView->setTsneRandomSeed(configuration().getTsneRandomSeed());
    prefclusterView->setProjectionScopeMode(configuration().getProjectionScopeMode());
    prefclusterView->setProjectionScopeMinutes(configuration().getProjectionScopeMinutes());
    prefclusterView->setProjectionOutOfScopeHidden(configuration().getProjectionOutOfScopeHidden());
    prefclusterView->setShowEapCollisions(configuration().getShowEapCollisions());
    prefclusterView->setShowEapClassMembers(configuration().getShowEapClassMembers());
    prefclusterView->setDensitySigmaPx(configuration().getDensitySigmaPx());
    prefWaveformView->setGain(configuration().getGain());

    // Appearance: reflect the current suite-wide theme preference.
    {
        const int idx = themeCombo->findData(
            static_cast<int>(neurosuite::loadThemePreference()));
        themeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }

    // Input bindings: load the effective chords from the registry into the editors.
    prefInput->updateFromRegistry();

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
    configuration().setRealignThreshold(prefRealignment->getRealignThreshold());
    configuration().setRealignIterations(prefRealignment->getRealignIterations());
    configuration().setRealignMaxShift(prefRealignment->getRealignMaxShift());
    configuration().setRealignMode(prefRealignment->getRealignMode());
    configuration().setReorderMethod(prefClusterOrder->getReorderMethod());
    configuration().setWaveformSortMean(prefClusterOrder->getWaveformSortMean());
    configuration().setMergeRecommendMax(prefMerging->getMergeRecommendMax());
    configuration().setMergeRecommendErrorFloor(prefMerging->getMergeRecommendErrorFloor());
    configuration().setMergeRecommendQualityFloor(prefMerging->getMergeRecommendQualityFloor());
    configuration().setShowMergeRecommendPanel(prefMerging->getShowMergeRecommendPanel());
    configuration().setDriftSliderMaxClusters(prefMatrices->getDriftSliderMaxClusters());
    configuration().setCurationLogging(prefSession->getCurationLogging());
    configuration().setReorderDisplayOnly(prefClusterOrder->getReorderDisplayOnly());
    configuration().setRealignVerbose(prefRealignment->getRealignVerbose());
    configuration().setReextractSpikesOnSave(prefSession->getReextractSpikesOnSave());
    configuration().setSyncFeatureSpaceOnSwitch(prefRealignment->getSyncFeatureSpaceOnSwitch());
    configuration().setRealignFromAnySpace(prefRealignment->getRealignFromAnySpace());
    configuration().setAutoRealignAfterMerge(prefRealignment->getAutoRealignAfterMerge());
    configuration().setAutoRenumberAfterMerge(prefClusterOrder->getAutoRenumberAfterMerge());
    configuration().setAutoUpdateMatricesAfterMerge(prefMatrices->getAutoUpdateMatricesAfterMerge());
    configuration().setErrorMatrixIncremental(prefMatrices->getErrorMatrixIncremental());
    configuration().setErrorMatrixLowPrecision(prefMatrices->getErrorMatrixLowPrecision());
    configuration().setDipSplitMinSize(prefSplitting->getDipSplitMinSize());
    configuration().setDipSplitBloatFactor(prefSplitting->getDipSplitBloatFactor());
    configuration().setDipSplitValleyThresh(prefSplitting->getDipSplitValleyThresh());
    configuration().setKnnK(prefSplitting->getKnnK());
    configuration().setKnnThreshold(prefSplitting->getKnnThreshold());
    configuration().setKnnMinNew(prefSplitting->getKnnMinNew());
    configuration().setKnnMinRef(prefSplitting->getKnnMinRef());

    // Display
    configuration().setBackgroundColor(prefDisplay->getBackgroundColor());
    configuration().setMarkerSize(prefDisplay->getMarkerSize());
    configuration().setSelectionLineWidth(prefDisplay->getSelectionLineWidth());
    configuration().setAutoscaleMarginPercent(prefDisplay->getAutoscaleMarginPercent());
    configuration().setUseWhiteColorDuringPrinting(prefDisplay->useWhiteColorDuringPrinting());
    configuration().setAutoShowMatricesOnOpen(prefMatrices->getAutoShowMatricesOnOpen());
    configuration().setAutoShowTemplateLibraryOnOpen(prefDisplay->getAutoShowTemplateLibraryOnOpen());
    configuration().setTemplatesMode(prefDisplay->getTemplatesMode());
    configuration().setTemplateThresholdMin(prefMatrices->getTemplateThresholdMin());
    configuration().setTemplateThresholdMax(prefMatrices->getTemplateThresholdMax());
    configuration().setTemplateXcorrMetric(prefMatrices->getTemplateXcorrMetric());

    // Auto-Merge (patch 0068)
    configuration().setAutoMergeAlgorithm(prefMerging->getAlgorithm());
    configuration().setAutoMergeMedianK(prefMerging->getMedianK());
    configuration().setAutoMergeScoreThreshold(prefMerging->getScoreThreshold());
    configuration().setAutoMergeUseErrorMatrix(prefMerging->getUseErrorMatrix());
    configuration().setAutoMergeErrorProbThreshold(prefMerging->getErrorProbThreshold());
    configuration().setAutoMergeMaxShift(prefMerging->getMaxShift());
    configuration().setAutoMergeTaperSamples(prefMerging->getTaperSamples());
    configuration().setAutoMergeMinClusterSize(prefMerging->getMinClusterSize());
    configuration().setAutoMergeScope(prefMerging->getScope());
    configuration().setAutoMergePreviewBeforeApply(prefMerging->getPreviewBeforeApply());

    // Cluster + Waveform views
    configuration().setTimeInterval(prefclusterView->getTimeInterval());
    configuration().setTsneSpikeCap(prefclusterView->getTsneSpikeCap());
    configuration().setTsnePerplexityStep(prefclusterView->getTsnePerplexityStep());
    configuration().setTsneStartPerplexity(prefclusterView->getTsneStartPerplexity());
    configuration().setTsneIterations(prefclusterView->getTsneIterations());
    configuration().setTsneTheta(prefclusterView->getTsneTheta());
    configuration().setTsneMaxDimensions(prefclusterView->getTsneMaxDimensions());
    configuration().setTsneLearningRate(prefclusterView->getTsneLearningRate());
    configuration().setTsneExaggeration(prefclusterView->getTsneExaggeration());
    configuration().setTsneExaggerationIterations(prefclusterView->getTsneExaggerationIterations());
    configuration().setTsneSubsampleOverCap(prefclusterView->getTsneSubsampleOverCap());
    configuration().setTsneRandomSeed(prefclusterView->getTsneRandomSeed());
    configuration().setProjectionScopeMode(prefclusterView->getProjectionScopeMode());
    configuration().setProjectionScopeMinutes(prefclusterView->getProjectionScopeMinutes());
    configuration().setProjectionOutOfScopeHidden(prefclusterView->getProjectionOutOfScopeHidden());
    configuration().setShowEapCollisions(prefclusterView->getShowEapCollisions());
    configuration().setShowEapClassMembers(prefclusterView->getShowEapClassMembers());
    configuration().setDensitySigmaPx(prefclusterView->getDensitySigmaPx());
    configuration().setGain(prefWaveformView->getGain());
    configuration().setNbChannels(prefWaveformView->getNbChannels());
    configuration().setChannelPositions(prefWaveformView->getChannelPositions());

    // Appearance: persist the suite-wide theme and apply it immediately.
    {
        const neurosuite::Theme t =
            static_cast<neurosuite::Theme>(themeCombo->currentData().toInt());
        neurosuite::saveThemePreference(t);
        neurosuite::applyTheme(t);
    }

    // Input bindings: editors -> registry overrides + the persisted Configuration map.
    // KlustersApp::applyPreferences() then pushes the QAction shortcuts.
    prefInput->commitToRegistry();

    // Radial menu: store the edited layout; the shipped one is stored as "" so a
    // later change to the default reaches users who never edited theirs.
    {
        const RadialLayout l = prefRadialMenu->layout();
        configuration().setRadialMenuLayout(l == radiallayout::defaults() ? QString()
                                                                          : radiallayout::toJson(l));
    }

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

    prefRealignment->setRealignThreshold(configuration().getRealignThresholdDefault());
    prefRealignment->setRealignIterations(configuration().getRealignIterationsDefault());
    prefRealignment->setRealignMaxShift(configuration().getRealignMaxShiftDefault());
    prefRealignment->setRealignMode(configuration().getRealignModeDefault());
    prefClusterOrder->setReorderMethod(configuration().getReorderMethodDefault());
    prefClusterOrder->setWaveformSortMean(configuration().getWaveformSortMeanDefault());
    prefMerging->setMergeRecommendMax(configuration().getMergeRecommendMaxDefault());
    prefMerging->setMergeRecommendErrorFloor(configuration().getMergeRecommendErrorFloorDefault());
    prefMerging->setMergeRecommendQualityFloor(configuration().getMergeRecommendQualityFloorDefault());
    prefMerging->setShowMergeRecommendPanel(configuration().getShowMergeRecommendPanelDefault());
    prefMatrices->setDriftSliderMaxClusters(configuration().getDriftSliderMaxClustersDefault());
    prefSession->setCurationLogging(configuration().getCurationLoggingDefault());
    prefClusterOrder->setReorderDisplayOnly(configuration().getReorderDisplayOnlyDefault());
    prefRealignment->setRealignVerbose(configuration().getRealignVerboseDefault());
    prefSession->setReextractSpikesOnSave(configuration().getReextractSpikesOnSaveDefault());
    prefRealignment->setSyncFeatureSpaceOnSwitch(configuration().getSyncFeatureSpaceOnSwitchDefault());
    prefRealignment->setRealignFromAnySpace(configuration().getRealignFromAnySpaceDefault());
    prefRealignment->setAutoRealignAfterMerge(configuration().getAutoRealignAfterMergeDefault());
    prefClusterOrder->setAutoRenumberAfterMerge(configuration().getAutoRenumberAfterMergeDefault());
    prefMatrices->setAutoUpdateMatricesAfterMerge(configuration().getAutoUpdateMatricesAfterMergeDefault());
    prefMatrices->setErrorMatrixIncremental(configuration().getErrorMatrixIncrementalDefault());
    prefMatrices->setErrorMatrixLowPrecision(configuration().getErrorMatrixLowPrecisionDefault());
    prefSplitting->setDipSplitMinSize(configuration().getDipSplitMinSizeDefault());
    prefSplitting->setDipSplitBloatFactor(configuration().getDipSplitBloatFactorDefault());
    prefSplitting->setDipSplitValleyThresh(configuration().getDipSplitValleyThreshDefault());
    prefSplitting->setKnnK(configuration().getKnnKDefault());
    prefSplitting->setKnnThreshold(configuration().getKnnThresholdDefault());
    prefSplitting->setKnnMinNew(configuration().getKnnMinNewDefault());
    prefSplitting->setKnnMinRef(configuration().getKnnMinRefDefault());

    prefDisplay->setBackgroundColor(configuration().getBackgroundColorDefault());
    prefDisplay->setMarkerSize(configuration().getMarkerSizeDefault());
    prefDisplay->setSelectionLineWidth(configuration().getSelectionLineWidthDefault());
    prefDisplay->setAutoscaleMarginPercent(configuration().getAutoscaleMarginPercentDefault());
    prefDisplay->setUseWhiteColorDuringPrinting(configuration().getUseWhiteColorDuringPrintingDefault());
    prefMatrices->setAutoShowMatricesOnOpen(configuration().getAutoShowMatricesOnOpenDefault());
    prefDisplay->setAutoShowTemplateLibraryOnOpen(configuration().getAutoShowTemplateLibraryOnOpenDefault());
    prefDisplay->setTemplatesMode(configuration().getTemplatesModeDefault());
    prefMatrices->setTemplateThresholdMin(configuration().getTemplateThresholdMinDefault());
    prefMatrices->setTemplateThresholdMax(configuration().getTemplateThresholdMaxDefault());
    prefMatrices->setTemplateXcorrMetric(configuration().getTemplateXcorrMetricDefault());

    // Auto-Merge (patch 0068) — defaults match KKE flag defaults.
    prefMerging->setAlgorithm(configuration().getAutoMergeAlgorithmDefault());
    prefMerging->setMedianK(configuration().getAutoMergeMedianKDefault());
    prefMerging->setScoreThreshold(configuration().getAutoMergeScoreThresholdDefault());
    prefMerging->setUseErrorMatrix(configuration().getAutoMergeUseErrorMatrixDefault());
    prefMerging->setErrorProbThreshold(configuration().getAutoMergeErrorProbThresholdDefault());
    prefMerging->setMaxShift(configuration().getAutoMergeMaxShiftDefault());
    prefMerging->setTaperSamples(configuration().getAutoMergeTaperSamplesDefault());
    prefMerging->setMinClusterSize(configuration().getAutoMergeMinClusterSizeDefault());
    prefMerging->setScope(configuration().getAutoMergeScopeDefault());
    prefMerging->setPreviewBeforeApply(configuration().getAutoMergePreviewBeforeApplyDefault());

    prefclusterView->setTimeInterval(configuration().getTimeIntervalDefault());
    prefclusterView->setTsneSpikeCap(configuration().getTsneSpikeCapDefault());
    prefclusterView->setTsnePerplexityStep(configuration().getTsnePerplexityStepDefault());
    prefclusterView->setTsneStartPerplexity(configuration().getTsneStartPerplexityDefault());
    prefclusterView->setTsneIterations(configuration().getTsneIterationsDefault());
    prefclusterView->setTsneTheta(configuration().getTsneThetaDefault());
    prefclusterView->setTsneMaxDimensions(configuration().getTsneMaxDimensionsDefault());
    prefclusterView->setTsneLearningRate(configuration().getTsneLearningRateDefault());
    prefclusterView->setTsneExaggeration(configuration().getTsneExaggerationDefault());
    prefclusterView->setTsneExaggerationIterations(configuration().getTsneExaggerationIterationsDefault());
    prefclusterView->setTsneSubsampleOverCap(configuration().getTsneSubsampleOverCapDefault());
    prefclusterView->setTsneRandomSeed(configuration().getTsneRandomSeedDefault());
    prefclusterView->setProjectionScopeMode(configuration().getProjectionScopeModeDefault());
    prefclusterView->setProjectionScopeMinutes(configuration().getProjectionScopeMinutesDefault());
    prefclusterView->setProjectionOutOfScopeHidden(configuration().getProjectionOutOfScopeHiddenDefault());
    prefclusterView->setShowEapCollisions(configuration().getShowEapCollisionsDefault());
    prefclusterView->setShowEapClassMembers(configuration().getShowEapClassMembersDefault());
    prefclusterView->setDensitySigmaPx(configuration().getDensitySigmaPxDefault());
    prefWaveformView->setGain(configuration().getGainDefault());
    prefWaveformView->resetChannelList(configuration().getNbChannels());

    // Input bindings: every editor back to its command's shipped default.
    prefInput->restoreDefaults();

    // Radial menu: the shipped wheel.
    prefRadialMenu->restoreDefaults();

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
