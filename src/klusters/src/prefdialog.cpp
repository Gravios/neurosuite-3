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
#include "prefplugins.h"
#include "pluginregistry.h"
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
#include <pagekeynavigator.h>
#include "prefbinder.h"
#include <QScrollArea>

#include <klustersshared/theme.h>

PrefDialog::PrefDialog(QWidget *parent, int nbChannels, const PluginRegistry* plugins)
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
        // Every page scrolls.  A page laid out for more height than the screen gives
        // the dialog is otherwise squeezed: its groups collapse onto one another and
        // the lowest ones end up unreadable or out of sight (the Cluster view page
        // asks for over 900 px).  With room to spare the area shows no scroll bar.
        QScrollArea* area = new QScrollArea(this);
        area->setFrameShape(QFrame::NoFrame);
        area->setWidgetResizable(true);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // A resizable area shrinks its widget to the minimum size hint, and these
        // pages' group boxes report a minimum they cannot be read at: hold the page
        // at the height it was laid out for, and let the area scroll below that.
        page->setMinimumHeight(page->sizeHint().height());
        area->setWidget(page);
        area->viewport()->setAutoFillBackground(false);     // the dialog's own background, not the base colour
        page->setAutoFillBackground(false);
        QPageWidgetItem* it = new QPageWidgetItem(area, name);
        it->setHeader(header);
        it->setIcon(QIcon(icon));
        addSubPage(parentItem, it);
        itemOfPage.insert(page, it);
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
    subPage(curation, prefReclustering, tr("Reclustering"), tr("Reclustering: the engine and its configuration"),
            ":/icons/reclustering");
    if (plugins) prefReclustering->setPlugins(plugins->plugins());
    connect(prefReclustering, &PrefReclustering::changed, this, &PrefDialog::enableApply);

    // ── Plugins: generated from the installed descriptors ──────────
    if (plugins) {
        const QList<KlustersPlugin>& list = plugins->plugins();
        QPageWidgetItem* pluginsGroup = new QPageWidgetItem(makePluginsOverviewPage(list, this, plugins->broken()), tr("Plugins"));
        pluginsGroup->setHeader(tr("Plugins"));
        pluginsGroup->setIcon(QIcon(":/shared-icons/folder-open"));
        addPage(pluginsGroup);
        for (const KlustersPlugin& p : list) {
            PrefPluginPage* page = new PrefPluginPage(p, this);
            const QString header = p.kind.isEmpty() ? p.name : tr("%1  [%2]").arg(p.name, p.kind);
            QPageWidgetItem* it = new QPageWidgetItem(page, p.name);
            it->setHeader(header);
            it->setIcon(QIcon(":/icons/reclustering"));
            addSubPage(pluginsGroup, it);
            pluginPageItems.insert(p.name, it);
            connect(page, &PrefPluginPage::changed, this, &PrefDialog::enableApply);
            prefPlugins.append(page);
        }
    }
    // Reclustering's "Parameters…" jumps to the engine's plugin page.
    connect(prefReclustering, &PrefReclustering::openPluginPage, this, [this](const QString& name) {
        if (QPageWidgetItem* it = pluginPageItems.value(name)) setCurrentPage(it);
    });

    // ── Wire enableApply on every interactive widget ───────────────
    // Session
    connect(prefSession->crashRecoveryCheckBox,  &QAbstractButton::clicked, this, &PrefDialog::enableApply);
    connect(prefSession->crashRecoveryComboBox,  &QComboBox::activated,     this, &PrefDialog::enableApply);

    // Reclustering
    connect(prefReclustering->reclusteringArgsLineEdit,        &QLineEdit::textChanged, this, &PrefDialog::enableApply);

    // Refinement (realign + dipsplit + knn)
    connect(prefRealignment->realignModeOffRadio,        &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRealignment->supersampleModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &PrefDialog::enableApply);
    connect(prefRealignment->realignModePcaRadio,        &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRealignment->realignModeRmsRadio,        &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefRealignment->realignModeCentroidRadio,   &QAbstractButton::toggled,     this, &PrefDialog::enableApply);
    connect(prefSplitting, &PrefSplitting::coreCutChanged, this, &PrefDialog::enableApply);

    // Display
    connect(prefDisplay->backgroundColorButton,        &QColorButton::colorChanged,    this, &PrefDialog::enableApply);
    connect(prefDisplay->useWhiteColorPrinting,        &QAbstractButton::clicked,      this, &PrefDialog::enableApply);
    connect(prefDisplay->templatesModeCheckBox,        &QAbstractButton::clicked,      this, &PrefDialog::enableApply);

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
    connect(prefclusterView, &PrefClusterView::hoverSpikeChanged, this, &PrefDialog::enableApply);
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
    connect(prefclusterView->correctCollisionRowsCheckBox, &QCheckBox::toggled, this, &PrefDialog::enableApply);
    connect(prefclusterView->showCollisionsCheckBox,     &QCheckBox::toggled, this, &PrefDialog::enableApply);
    connect(prefclusterView->showClassMembersCheckBox,   &QCheckBox::toggled, this, &PrefDialog::enableApply);
    connect(prefclusterView->densitySigmaSpinBox, &QDoubleSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefWaveformView->gainSpinBox,     &QSpinBox::valueChanged, this, &PrefDialog::enableApply);
    connect(prefWaveformView, &PrefWaveformView::positionsChanged,      this, &PrefDialog::enableApply);
    connect(prefWaveformView, &PrefWaveformView::waveformQualityChanged, this, &PrefDialog::enableApply);

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

    // Every setting whose row of the preference table names a control of these pages
    // (configuration_prefs.cpp): loaded, applied and reset by the binder, not by hand.
    prefBinder = new PrefBinder(this, this);
    connect(prefBinder, &PrefBinder::changed, this, &PrefDialog::enableApply);

    // The dialog from the keyboard: Enter into a page, Tab round its settings, Esc back.
    new PageKeyNavigator(this);
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
    prefBinder->load(configuration());

    // Radial menu: the stored layout (or the shipped one).
    prefRadialMenu->setLayout(radiallayout::fromSettings(configuration().getRadialMenuLayout()));

    // Session
    prefSession->setCrashRecovery(configuration().isCrashRecovery());
    prefSession->setCrashRecoveryIndex(configuration().crashRecoveryIntervalIndex());

    // Reclustering
    prefReclustering->setReclusterEngine(configuration().getReclusterEngine());
    prefReclustering->setFiberKitReclusterCommand(configuration().getFiberKitReclusterCommand());
    prefReclustering->setReclusteringArguments(configuration().getReclusteringArguments());
    prefReclustering->setReclusterChunking(configuration().getReclusterChunking());
    prefReclustering->setReclusterChunkMinutes(configuration().getReclusterChunkMinutes());

    // Refinement
    prefRealignment->setRealignMode(configuration().getRealignMode());
    prefRealignment->setSupersampleMode(configuration().getSupersampleMode());
    prefSplitting->setCoreCut(configuration().getCoreCut());

    // Display
    prefDisplay->setBackgroundColor(configuration().getBackgroundColor());
    prefDisplay->setUseWhiteColorDuringPrinting(configuration().getUseWhiteColorDuringPrinting());
    prefDisplay->setTemplatesMode(configuration().getTemplatesMode());

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
    prefclusterView->setHoverSpike(configuration().getHoverSpike());
    prefclusterView->setTsneRandomSeed(configuration().getTsneRandomSeed());
    prefclusterView->setProjectionScopeMode(configuration().getProjectionScopeMode());
    prefclusterView->setProjectionScopeMinutes(configuration().getProjectionScopeMinutes());
    prefclusterView->setProjectionOutOfScopeHidden(configuration().getProjectionOutOfScopeHidden());
    prefclusterView->setCorrectCollisionRows(configuration().getCorrectCollisionRows());
    prefclusterView->setShowEapCollisions(configuration().getShowEapCollisions());
    prefclusterView->setShowEapClassMembers(configuration().getShowEapClassMembers());
    prefclusterView->setDensitySigmaPx(configuration().getDensitySigmaPx());
    prefWaveformView->setGain(configuration().getGain());
    prefWaveformView->setWaveformQuality(configuration().getWaveformQuality());

    // Appearance: reflect the current suite-wide theme preference.
    {
        const int idx = themeCombo->findData(
            static_cast<int>(neurosuite::loadThemePreference()));
        themeCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }

    // Input bindings: load the effective chords from the registry into the editors.
    prefInput->updateFromRegistry();
    for (PrefPluginPage* page : prefPlugins) page->updateFromSettings();
    prefReclustering->setReclusterEngine(configuration().getReclusterEngine());   // re-reads the plugins' stored defaults for its summary

    enableButtonApply(false);
    applyEnable = false;
}

void PrefDialog::updateConfiguration()
{
    prefBinder->apply(configuration());

    // Session
    configuration().setCrashRecovery(prefSession->isCrashRecovery());
    configuration().setCrashRecoveryIndex(prefSession->crashRecoveryIntervalIndex());

    // Reclustering
    configuration().setReclusterEngine(prefReclustering->getReclusterEngine());
    configuration().setFiberKitReclusterCommand(prefReclustering->getFiberKitReclusterCommand());
    configuration().setReclusteringArguments(prefReclustering->getReclusteringArguments());
    configuration().setReclusterChunking(prefReclustering->getReclusterChunking());
    configuration().setReclusterChunkMinutes(prefReclustering->getReclusterChunkMinutes());

    // Refinement
    configuration().setRealignMode(prefRealignment->getRealignMode());
    configuration().setSupersampleMode(prefRealignment->getSupersampleMode());
    configuration().setCoreCut(prefSplitting->getCoreCut());

    // Display
    configuration().setBackgroundColor(prefDisplay->getBackgroundColor());
    configuration().setUseWhiteColorDuringPrinting(prefDisplay->useWhiteColorDuringPrinting());
    configuration().setTemplatesMode(prefDisplay->getTemplatesMode());

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
    configuration().setHoverSpike(prefclusterView->getHoverSpike());
    configuration().setTsneRandomSeed(prefclusterView->getTsneRandomSeed());
    configuration().setProjectionScopeMode(prefclusterView->getProjectionScopeMode());
    configuration().setProjectionScopeMinutes(prefclusterView->getProjectionScopeMinutes());
    configuration().setProjectionOutOfScopeHidden(prefclusterView->getProjectionOutOfScopeHidden());
    configuration().setCorrectCollisionRows(prefclusterView->getCorrectCollisionRows());
    configuration().setShowEapCollisions(prefclusterView->getShowEapCollisions());
    configuration().setShowEapClassMembers(prefclusterView->getShowEapClassMembers());
    configuration().setDensitySigmaPx(prefclusterView->getDensitySigmaPx());
    configuration().setGain(prefWaveformView->getGain());
    configuration().setWaveformQuality(prefWaveformView->getWaveformQuality());
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
    for (PrefPluginPage* page : prefPlugins) page->commitToSettings();

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
    restoreDefaults();
}

void PrefDialog::restoreDefaults()
{
    prefBinder->restoreDefaults();

    prefSession->setCrashRecovery(configuration().isCrashRecoveryDefault());
    prefSession->setCrashRecoveryIndex(configuration().crashRecoveryIntervalIndexDefault());

    prefReclustering->setReclusterEngine(QStringLiteral("klustakwik"));
    prefReclustering->setFiberKitReclusterCommand(configuration().getFiberKitReclusterCommandDefault());
    prefReclustering->setReclusteringArguments(configuration().getReclusteringArgumentsDefault());
    prefReclustering->setReclusterChunking(configuration().getReclusterChunkingDefault());
    prefReclustering->setReclusterChunkMinutes(configuration().getReclusterChunkMinutesDefault());

    prefRealignment->setRealignMode(configuration().getRealignModeDefault());
    prefRealignment->setSupersampleMode(configuration().getSupersampleModeDefault());
    prefSplitting->setCoreCut(configuration().getCoreCutDefault());

    prefDisplay->setBackgroundColor(configuration().getBackgroundColorDefault());
    prefDisplay->setUseWhiteColorDuringPrinting(configuration().getUseWhiteColorDuringPrintingDefault());
    prefDisplay->setTemplatesMode(configuration().getTemplatesModeDefault());

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
    prefclusterView->setHoverSpike(configuration().getHoverSpikeDefault());
    prefclusterView->setTsneRandomSeed(configuration().getTsneRandomSeedDefault());
    prefclusterView->setProjectionScopeMode(configuration().getProjectionScopeModeDefault());
    prefclusterView->setProjectionScopeMinutes(configuration().getProjectionScopeMinutesDefault());
    prefclusterView->setProjectionOutOfScopeHidden(configuration().getProjectionOutOfScopeHiddenDefault());
    prefclusterView->setCorrectCollisionRows(configuration().getCorrectCollisionRowsDefault());
    prefclusterView->setShowEapCollisions(configuration().getShowEapCollisionsDefault());
    prefclusterView->setShowEapClassMembers(configuration().getShowEapClassMembersDefault());
    prefclusterView->setDensitySigmaPx(configuration().getDensitySigmaPxDefault());
    prefWaveformView->setGain(configuration().getGainDefault());
    prefWaveformView->setWaveformQuality(configuration().getWaveformQualityDefault());
    prefWaveformView->resetChannelList(configuration().getNbChannels());

    // Input bindings: every editor back to its command's shipped default.
    prefInput->restoreDefaults();
    for (PrefPluginPage* page : prefPlugins) page->restoreDescriptorDefaults();

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

QStringList PrefDialog::unboundPreferenceRows() const { return prefBinder->unboundRows(); }

void PrefDialog::enableApply()
{
    enableButtonApply(true);
    applyEnable = true;
}

void PrefDialog::syncAutoNFeatures(int n)
{
    prefReclustering->autoSelectNFeaturesSpinBox->setValue(n);
}

void PrefDialog::resetChannelList(int nbChannels)
{
    prefWaveformView->resetChannelList(nbChannels);
}

void PrefDialog::enableChannelSettings(bool state)
{
    prefWaveformView->enableChannelSettings(state);
}

bool PrefDialog::showPage(const QWidget* page)
{
    QPageWidgetItem* item = itemOfPage.value(page, nullptr);
    if (!item) return false;
    setCurrentPage(item);
    return true;
}
