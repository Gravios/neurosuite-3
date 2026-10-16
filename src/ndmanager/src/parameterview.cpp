/***************************************************************************
 *   Copyright (C) 2004 by Lynn Hazan                                      *
 *   lynn.hazan@myrealbox.com                                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

// include files for QT
#include <QPushButton>

#include <QLineEdit>

#include <QLabel>

#include <QCheckBox>

#include <QPixmap>

#include <QObject>

#include <QStringList>

#include <QTextEdit>
#include <QDebug>
#include <QStackedWidget>

#include <QTextStream>
#include <QList>
#include <QFrame>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QSplitter>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMainWindow>
#include <QRegularExpression>
#include <QStatusBar>
#include <QProcess>
#include <QTemporaryFile>
#include <QDir>
#include <QSet>
#include <QSignalBlocker>

//include files for the application
#include "parameterview.h"
#include "ndmanagerdoc.h"
#include "ndmanager.h"
#include "tags.h"
#include "descriptionyamlreader.h"
#include "ndmanagerutils.h"

#include "parametertree.h"
#include "plugincategory.h"

using namespace ndmanager;

const QString ParameterView::DEFAULT_COLOR = "#0080ff";

ParameterView::ParameterView(ndManager*,ndManagerDoc& doc,QWidget* parent, const char*, bool expertMode)
    : QWidget(parent)
    ,doc(doc),counter(0),programsModified(false),programId(0),expertMode(expertMode){

    QHBoxLayout *hbox = new QHBoxLayout;
    setLayout(hbox);

    QSplitter *splitter = new QSplitter;
    splitter->setChildrenCollapsible(false);
    hbox->addWidget(splitter);
    mParameterTree = new ParameterTree;
    mParameterTree->setHeaderLabels(QStringList()<<QString());
    splitter->addWidget(mParameterTree);

    mStackWidget = new QStackedWidget;
    splitter->addWidget(mStackWidget);
    connect(mParameterTree,&ParameterTree::showWidgetPage,mStackWidget,&QStackedWidget::setCurrentWidget);

    setWindowTitle(tr("Parameter View"));

    //adding page "General information"
    generalInfo = new GeneralInfoPage;
    mStackWidget->addWidget(generalInfo);
    mParameterTree->addPage(":/icons/folder-blue",tr("General information"),generalInfo);

    //adding page "Acquisition System"
    acquisitionSystem = new AcquisitionSystemPage;
    mStackWidget->addWidget(acquisitionSystem);
    mParameterTree->addPage(":/icons/acquisition",tr("Acquisition System"),acquisitionSystem);


    //adding page "Video"
    video = new VideoPage;
    mStackWidget->addWidget(video);
    mParameterTree->addPage(":/icons/video",tr("Video"),video);

    //adding page "Local Field Potentials "
    lfp = new LfpPage;
    mStackWidget->addWidget(lfp);
    mParameterTree->addPage(":/icons/lfp",tr("Local Field Potentials"),lfp);


    //adding page "Files"
    //This page is added only if the expert mode is set. The page is always created to keep track of the file information
    if(expertMode){
        files = new FilesPage;
        mStackWidget->addWidget(files);
        mParameterTree->addPage(":/icons/files",tr("Files"),files);
    } else {
        files = new FilesPage();
    }

    // adding page "Probes"
    // Always created; shown only in expert mode.  The probe list is part of
    // the session YAML and must be preserved on save even in non-expert mode.
    // Probes comes first so that importing a probe immediately populates the
    // Anatomical Groups and Spike Groups pages below it.
    probe = new ProbePage;
    if(expertMode){
        mStackWidget->addWidget(probe);
        mParameterTree->addPage(":/icons/probe", tr("Probes"), probe);
    }

    //adding page "Anatomical Groups"
    //This page is added only if the expert mode is set. The page is always created to keep track of the file information
    if(expertMode){
        anatomy = new AnatomyPage;
        mStackWidget->addWidget(anatomy);
        mParameterTree->addPage(":/icons/anatomy",tr("Anatomical Groups"),anatomy);
    }
    else{
        anatomy = new AnatomyPage();
    }

    //adding page "Spike Groups"
    //This page is added only if the expert mode is set. The page is always created to keep track of the file information
    if(expertMode){
        spike = new SpikePage;
        mStackWidget->addWidget(spike);
        mParameterTree->addPage(":/icons/spikes", tr("Spike Groups"), spike);
    } else {
        spike = new SpikePage();
    }

    //adding page "Unit List"
    unitList = new UnitListPage;
    mStackWidget->addWidget(unitList);
    mParameterTree->addPage(":/icons/units", tr("Units"), unitList);

    //adding page "Neuroscope"

    QTabWidget* tabWidget = new QTabWidget;
    //adding "Miscellaneous" tab
    miscellaneous = new MiscellaneousPage();
    tabWidget->addTab(miscellaneous,tr("Miscellaneous"));
    //adding "Video" tab
    neuroscopeVideo = new NeuroscopeVideoPage();
    tabWidget->addTab(neuroscopeVideo,tr("Video"));
    //adding "Clusters" tab
    clusters = new ClustersPage();
    tabWidget->addTab(clusters,tr("Clusters"));

    //adding "Channel color" tab
    channelColors = new ChannelColorsPage();
    //This tab is added only if the expert mode is set. The page is always created to keep track of the file information
    if(expertMode) tabWidget->addTab(channelColors,tr("Channel Colors"));
    //adding "Channel offeset" tab
    channelDefaultOffsets = new ChannelOffsetsPage();
    //This tab is added only if the expert mode is set. The page is always created to keep track of the file information
    if(expertMode)
        tabWidget->addTab(channelDefaultOffsets,tr("Channel Offsets"));


    mStackWidget->addWidget(tabWidget);
    mParameterTree->addPage(":/icons/neuroscope", tr("Neuroscope"), tabWidget);


    //adding page "Programs"

    programs = new ProgramsPage(expertMode);

    mStackWidget->addWidget(programs);
    mScriptsItem = mParameterTree->addPage(":/icons/programs", tr("Plugins"), programs);

    //adding page "Pipeline Designer"
    pipelineDesigner = new PipelineDesignerPage;
    mStackWidget->addWidget(pipelineDesigner);
    mParameterTree->addPage(":/icons/programs", tr("Pipeline"), pipelineDesigner);
    connect(pipelineDesigner, &PipelineDesignerPage::applyRequested,
            this,             &ParameterView::setProgramList);
    connect(pipelineDesigner, &PipelineDesignerPage::savePipelineRequested,
            this,             &ParameterView::savePipelineDefault);
    connect(pipelineDesigner, &PipelineDesignerPage::saveAsPipelineRequested,
            this,             &ParameterView::savePipelineAs);

    //set connections
    connect(acquisitionSystem, &AcquisitionSystemPage::nbChannelsModified, this, &ParameterView::nbChannelsModified);
    connect(programs, &ProgramsPage::addNewProgram, this, &ParameterView::addNewProgram);
    connect(programs, &ProgramsPage::programToLoad, this, &ParameterView::loadProgram);
    connect(programs, &ProgramsPage::discoverPlugins, this, &ParameterView::discoverPlugins);
    connect(spike, &SpikePage::nbGroupsModified, this, &ParameterView::nbSpikeGroupsModified);
    connect(files, &FilesPage::fileModification, this, &ParameterView::fileModification);




    connect(this, &ParameterView::resetModificationStatus, generalInfo, &GeneralInfoPage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, acquisitionSystem, &AcquisitionSystemPage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, video, &VideoPage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, lfp, &LfpPage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, miscellaneous, &MiscellaneousPage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, neuroscopeVideo, &NeuroscopeVideoPage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, clusters, &ClustersPage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, unitList, &UnitListPage::resetModificationStatus);
    // These pages exist (hidden) in non-expert mode too and still count in
    // isModified(), so a save must reset them in every mode.
    connect(this, &ParameterView::resetModificationStatus, files, &FilesPage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, anatomy, &AnatomyPage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, spike, &SpikePage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, channelColors, &ChannelColorsPage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, channelDefaultOffsets, &ChannelOffsetsPage::resetModificationStatus);
    connect(this, &ParameterView::resetModificationStatus, probe, &ProbePage::resetModificationStatus);

    if(expertMode){
        connect(probe, &ProbePage::probeLayoutImported,
                this,  &ParameterView::applyProbeLayout);
    }
}

ParameterView::~ParameterView()
{
    if (!expertMode) {
       delete files;
       delete anatomy;
       delete spike;
       delete channelColors;
       delete channelDefaultOffsets;
       delete probe;   // not in mStackWidget, so not owned by it
    }
}

void ParameterView::addNewProgram(){
    counter++;
    programId++;
    QString programName = QString::fromLatin1("New Plugin-%1").arg(programId);
    ProgramPage* program = addProgram(programName);
    program->initialisationOver();
    emit scriptListHasBeenModified(QStringList()<<programDict.keys());
}

ProgramPage* ParameterView::addProgram(const QString& programName){
    ProgramPage* program = addProgram(programName,true);
    return program;
}

ProgramPage* ParameterView::addProgram(const QString& programName,bool show,
                                       const QString& category){
    QWidget *w = new QWidget(this);
    ProgramPage* program = new ProgramPage(expertMode,w,programName);
    program->setCategory(category);
    if(!doc.url().isEmpty())
        program->setSessionDir(QFileInfo(doc.url()).absolutePath());
    mStackWidget->addWidget(program);

    QTreeWidgetItem * item = mParameterTree->addSubPage(mScriptsItem,programName, program);

    ProgramPageId pageId;
    pageId.item = item;
    pageId.page = program;

    programDict.insert(programName,pageId);
    programOrder.removeAll(programName);
    programOrder.append(programName);
    placeProgramItems();

    //set the parameterPage program name
    ParameterPage* parameterPage = program->getParameterPage();
    parameterPage->setProgramName(programName);

    //set connections
    connect(program, &ProgramPage::programNameChanged, this, &ParameterView::changeProgramName);
    connect(program, &ProgramPage::programToRemove, this, &ParameterView::removeProgram);
    connect(program, &ProgramPage::scriptHidden, this, &ParameterView::scriptHidden);
    // A value edited on the plugin's page reaches the pipeline graph's nodes
    // of that plugin (the name is read at the time: the program may be renamed).
    connect(parameterPage, &ParameterPage::parameterValueChanged, this,
            [this, parameterPage](const QString& name, const QString& value) {
        if (pipelineDesigner)
            pipelineDesigner->setProgramParameter(parameterPage->getProgramName(), name, value);
    });

    //Show the new page
    if(show)
        mStackWidget->setCurrentWidget(program);

    return program;
}

QString ParameterView::programCategory(const QString& programName) const
{
    const ProgramPage* page = programDict.value(programName).page;
    const NdmScriptDef* def = ndmScriptDef(programName);
    return plugincategory::resolve(page ? page->getCategory() : QString(),
                                   def ? def->category : QString());
}

void ParameterView::placeProgramItems()
{
    // Moving items would otherwise change the current item on the way and
    // flip the page stack back and forth; the current item is put back below.
    const QSignalBlocker blocker(mParameterTree);
    QTreeWidgetItem* current = mParameterTree->currentItem();

    QStringList present;
    for (const QString& name : programOrder) present << programCategory(name);
    const QStringList cats = plugincategory::order(ndmCategoryOrder(), present);

    // Remember which branches the user collapsed: a moved item loses it.
    QSet<QString> collapsed;
    for (auto it = mCategoryItems.cbegin(); it != mCategoryItems.cend(); ++it)
        if (!it.value()->isExpanded()) collapsed.insert(it.key());

    // Detach the plugins, then drop the branches no plugin needs any more.
    for (const QString& name : programOrder)
        if (QTreeWidgetItem* item = programDict.value(name).item)
            if (QTreeWidgetItem* parent = item->parent())
                parent->removeChild(item);
    for (auto it = mCategoryItems.begin(); it != mCategoryItems.end(); ) {
        if (cats.contains(it.key())) { ++it; continue; }
        if (current == it.value()) current = mScriptsItem;
        delete it.value();
        it = mCategoryItems.erase(it);
    }

    // The branches in category order, each with its plugins in run order.
    for (int i = 0; i < cats.size(); ++i) {
        const QString& cat = cats.at(i);
        QTreeWidgetItem* branch = mCategoryItems.value(cat);
        if (!branch) {
            // A branch shows the Plugins overview page when clicked.
            branch = mParameterTree->addSubPage(mScriptsItem, ndmCategoryLabel(cat), programs);
            mCategoryItems.insert(cat, branch);
        }
        if (mScriptsItem->indexOfChild(branch) != i) {
            mScriptsItem->removeChild(branch);
            mScriptsItem->insertChild(i, branch);
        }
    }
    for (const QString& name : programOrder)
        if (QTreeWidgetItem* item = programDict.value(name).item)
            mCategoryItems.value(programCategory(name))->addChild(item);
    for (auto it = mCategoryItems.cbegin(); it != mCategoryItems.cend(); ++it)
        it.value()->setExpanded(!collapsed.contains(it.key()));

    if (current) mParameterTree->setCurrentItem(current);
}

void ParameterView::changeProgramName(ProgramPage* programPage, const QString& newName, const QString &message, const QString &title){

    const QString oldName = programPage->objectName();
    if(newName == oldName)
        return;

    if(programDict.contains(newName)){
        //set back the old name
        ParameterPage* parameterPage = programPage->getParameterPage();
        parameterPage->setProgramName(oldName);
        //this method will be call anyway but by calling it now a first time, we ensure that the text display in the script tab in consistent with the program (the old program which is kept)
        programPage->nameChanged(oldName);

        const QString currentMessage =  tr("There is already a plugin with the name %1.").arg(newName);
        QMessageBox::critical (this, tr("plugin name conflict"),currentMessage);

        return;
    }
    ProgramPageId id = programDict[oldName];
    id.item->setText(0,newName);
    id.page->setObjectName(newName);

    programDict.remove(oldName);
    programDict.insert(newName,id);
    // Rename in place: the program keeps its position in the list.
    const int orderIdx = programOrder.indexOf(oldName);
    if (orderIdx >= 0) programOrder[orderIdx] = newName;
    else               programOrder.append(newName);
    placeProgramItems();   // a built-in name may belong to another branch


    mStackWidget->setCurrentWidget(id.page);
    //If the message if not empty show a message box with it
    if(!message.isEmpty())
        QMessageBox::critical (this,title,message );

    emit scriptListHasBeenModified(programOrder);
}


void ParameterView::removeProgram(ProgramPage* programPage){
    programsModified = true;

    const QString name = programPage->objectName();
    ProgramPageId id = programDict[name];
    mStackWidget->removeWidget(id.page);
    delete id.page;

    delete id.item;
    programDict.remove(name);
    programOrder.removeAll(name);
    placeProgramItems();   // drops a branch left empty

    /*if(name.contains("New Program-") || name.contains("Untitled-"))*/ counter--;
    emit scriptListHasBeenModified(programOrder);
    //mParameterTree->setCurrentItem(mScriptsItem);
}

void ParameterView::initialize(QMap<int, QList<int> >& anatomicalGroups,QMap<QString, QMap<int,QString> >& attributes,
                               QMap<int, QList<int> >& spikeGroups,QMap<int, QMap<QString,QString> >& spikeGroupsInformation,QMap<int, QStringList >& units,
                               GeneralInformation& generalInformation,QMap<QString,double>& acquisitionSystemInfo,QMap<QString,double>& videoInformation,
                               QList<FileInformation>& fileList,QList<ChannelColors>& channelColors,QMap<int,int>& channelDefaultOffsets,
                               NeuroscopeVideoInfo& neuroscopeVideoInfo,QList<ProgramInformation>& programList,
                               double lfpRate,float screenGain,int nbSamples,int peakSampleIndex,const QString& traceBackgroundImage){

    //Initialize the general page
    generalInfo->setDate(generalInformation.getDate());
    generalInfo->setDescription(generalInformation.getDescription());
    generalInfo->setExperimenters(generalInformation.getExperimenters());
    generalInfo->setNotes(generalInformation.getNotes());
    generalInfo->initialisationOver();

    //Initialize the acquisition page
    acquisitionSystem->setAmplification(static_cast<int>(acquisitionSystemInfo[AMPLIFICATION]));
    acquisitionSystem->setOffset(static_cast<int>(acquisitionSystemInfo[OFFSET]));
    acquisitionSystem->setResolution(static_cast<int>(acquisitionSystemInfo[BITS]));
    acquisitionSystem->setSamplingRate(acquisitionSystemInfo[SAMPLING_RATE]);
    acquisitionSystem->setVoltageRange(static_cast<int>(acquisitionSystemInfo[VOLTAGE_RANGE]));
    acquisitionSystem->setNbChannels(static_cast<int>(acquisitionSystemInfo[NB_CHANNELS]));
    acquisitionSystem->initialisationOver();

    //Initialize the video page
    if(videoInformation[SAMPLING_RATE] != 0)
        video->setSamplingRate(videoInformation[SAMPLING_RATE]);
    if(videoInformation[WIDTH] != 0)
        video->setWidth(static_cast<int>(videoInformation[WIDTH]));
    if(videoInformation[HEIGHT] != 0)
        video->setHeight(static_cast<int>(videoInformation[HEIGHT]));
    video->initialisationOver();

    //Initialize the lfp page
    lfp->setSamplingRate(lfpRate);
    lfp->initialisationOver();

    //Initialize the files page
    QList<FileInformation>::iterator fileIterator;
    for(fileIterator = fileList.begin(); fileIterator != fileList.end(); ++fileIterator){
        FileInformation fileInformation = static_cast<FileInformation>(*fileIterator);
        QString extension = fileInformation.getExtension();
        FilePage* filePage = files->addFile(extension);
        filePage->setExtension(extension);
        filePage->setSamplingRate(fileInformation.getSamplingRate());
        filePage->setChannelMapping(fileInformation.getChannelMapping());
        filePage->initialisationOver();
    }


    //Initialize the anatomical groups page
    nbChannels = static_cast<int>(acquisitionSystemInfo[NB_CHANNELS]);
    probe->setNbChannels(nbChannels);
    anatomy->setNbChannels(static_cast<int>(acquisitionSystemInfo[NB_CHANNELS]));
    anatomy->setGroups(anatomicalGroups);
    anatomy->setAttributes(attributes);
    anatomy->setModified(false);

    //Initialize the spike groups page
    spike->setNbChannels(nbChannels);
    spike->setGroups(spikeGroups,spikeGroupsInformation);
    spike->setModified(false);

    //Initialize the unit list page
    unitList->setNbUnits(units.count());
    unitList->setUnits(units);

    //Initialize the NeuroScope miscellaneous page
    miscellaneous->setScreenGain(screenGain);
    miscellaneous->setTraceBackgroundImage(traceBackgroundImage);
    miscellaneous->initialisationOver();

    //Initialize the video NeuroScope page
    neuroscopeVideo->setBackgroundImage(neuroscopeVideoInfo.getBackgroundImage());
    neuroscopeVideo->setFlip(neuroscopeVideoInfo.getFlip());
    neuroscopeVideo->setRotation(neuroscopeVideoInfo.getRotation());
    neuroscopeVideo->setPositionsBackground(neuroscopeVideoInfo.getTrajectory());
    neuroscopeVideo->initialisationOver();

    //Initialize the spike NeuroScope page
    clusters->setNbSamples(nbSamples);
    clusters->setPeakIndex(peakSampleIndex);
    clusters->initialisationOver();

    //Initialize the channel color page
    this->channelColors->setNbChannels(static_cast<int>(acquisitionSystemInfo[NB_CHANNELS]));
    this->channelColors->setColors(channelColors);
    this->channelColors->setModified(false);

    //Initialize the channel offset page
    this->channelDefaultOffsets->setNbChannels(static_cast<int>(acquisitionSystemInfo[NB_CHANNELS]));
    this->channelDefaultOffsets->setOffsets(channelDefaultOffsets);
    this->channelDefaultOffsets->setModified(false);


    //Initialize the programs page
    QList<ProgramInformation>::iterator programIterator;
    for(programIterator = programList.begin(); programIterator != programList.end(); ++programIterator){
        ProgramInformation programInformation = static_cast<ProgramInformation>(*programIterator);
        ProgramPage* programPage = addProgram(programInformation.getProgramName(),false,
                                              programInformation.getCategory());
        //set the parameters
        ParameterPage* parameterPage = programPage->getParameterPage();
        QString name = programInformation.getProgramName();
        parameterPage->setProgramName(name);
        //set the help
        programPage->setHelp(programInformation.getHelp());
        QMap<int, QStringList > info = programInformation.getParameterInformation();
        parameterPage->setParameterInformation(info);
        if(expertMode){
            //set the script if any: the file corresponding to the program name
            const QString path = NdManagerUtils::findExecutable(name);
            if(!path.isNull()){
                const QString message = programPage->loadScript(path);
                if(!message.isEmpty())
                    QMessageBox::critical (this,tr("IO Error!"),message);
            }
            programPage->initialisationOver();
        }
    }

    // Seed the Pipeline Designer from the same programs list
    pipelineDesigner->setPrograms(programList);

    // Then opportunistically load the default pipeline file alongside the
    // session, if it exists.  This overrides whatever setPrograms put in
    // place — the file is the authoritative source of pipeline state when
    // present, and setPrograms (without a file) just gives us the
    // empty-graph + root baseline.  Silent on miss; existence-only check
    // because every session starts without a pipeline file the first time
    // it's opened.
    {
        const QString base = sessionBasePath();
        if (!base.isEmpty()) {
            const QString defaultPath = base + QLatin1String(".ndm.default.pipeline");
            if (QFileInfo::exists(defaultPath)) {
                // No modal at open time — it would interrupt the document-load
                // flow — but never silent either: the Pipeline tab's notice
                // bar says what happened, and qWarning logs it.
                QString warning;
                const QString fileName = QFileInfo(defaultPath).fileName();
                if (!pipelineDesigner->loadPipelineFile(defaultPath, &warning)) {
                    qWarning("Pipeline auto-load failed: %s", qUtf8Printable(warning));
                    pipelineDesigner->showNotice(
                        tr("Could not load %1:\n%2\nThe graph shown is NOT that pipeline; "
                           "Save Pipeline will ask before overwriting the file.")
                            .arg(fileName, warning),
                        /*isError=*/true);
                } else if (!warning.isEmpty()) {
                    // Non-fatal repairs (unknown plugin types dropped, ...).
                    qWarning("Pipeline auto-load warning: %s", qUtf8Printable(warning));
                    pipelineDesigner->showNotice(
                        tr("Loaded %1 with changes:\n%2").arg(fileName, warning));
                }
            }
        }
    }
}

void ParameterView::discoverPlugins() {
    // ndm plugin protocol v1: an executable named `ndm_<name>` on $PATH is a plugin iff it answers the
    // handshake `--ndm-version` with a line "ndm-plugin-protocol <N>".  We gate on that first (so we do
    // not run `--ndm-describe` on arbitrary ndm_* binaries), skip any plugin declaring a protocol newer
    // than this build understands, then hand `--ndm-describe`'s YAML to the same DescriptionYamlReader /
    // loadProgram path used for installed descriptions, adding any not already loaded.
    const int kNdmProtocol = 1;          // highest ndm-plugin protocol this build understands

    auto runPluginCmd = [](const QString &exePath, const QString &arg, QByteArray *out) -> bool {
        QProcess proc;
        proc.start(exePath, QStringList() << arg);
        if (!proc.waitForFinished(5000) || proc.exitStatus() != QProcess::NormalExit
                || proc.exitCode() != 0)
            return false;
        if (out)
            *out = proc.readAllStandardOutput();
        return true;
    };

    // Search $NDM_PLUGIN_PATH (explicit plugin dirs, listSeparator-separated) first so a plugin there
    // shadows a same-named one on $PATH, then $PATH itself.
    QStringList dirs;
    const QString pluginPath = QString::fromLocal8Bit(qgetenv("NDM_PLUGIN_PATH"));
    if (!pluginPath.isEmpty())
        dirs += pluginPath.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    dirs += QString::fromLocal8Bit(qgetenv("PATH")).split(QDir::listSeparator(), Qt::SkipEmptyParts);
    QSet<QString> tried;
    int added = 0;
    int skipped = 0;
    for (const QString &dirPath : dirs) {
        const QFileInfoList entries = QDir(dirPath).entryInfoList(
            QStringList() << QStringLiteral("ndm_*"), QDir::Files | QDir::Executable);
        for (const QFileInfo &fi : entries) {
            const QString exe = fi.fileName();
            if (tried.contains(exe))
                continue;
            tried.insert(exe);

            // Handshake: parse "ndm-plugin-protocol <N>" from --ndm-version's first line.
            QByteArray vout;
            if (!runPluginCmd(fi.absoluteFilePath(), QStringLiteral("--ndm-version"), &vout))
                continue;                                          // not a plugin
            const QString vline =
                QString::fromUtf8(vout).section(QLatin1Char('\n'), 0, 0).trimmed();
            const QString tag = QStringLiteral("ndm-plugin-protocol");
            if (!vline.startsWith(tag))
                continue;                                          // not a plugin
            bool ok = false;
            const int proto = vline.mid(tag.size()).trimmed().toInt(&ok);
            if (!ok)
                continue;
            if (proto > kNdmProtocol) {
                ++skipped;                                         // newer than we understand
                qWarning().noquote() << QStringLiteral(
                    "ndm discovery: %1 declares ndm-plugin-protocol %2 (this build supports %3) -- skipped")
                    .arg(exe).arg(proto).arg(kNdmProtocol);
                continue;
            }

            QByteArray out;
            if (!runPluginCmd(fi.absoluteFilePath(), QStringLiteral("--ndm-describe"), &out)
                    || out.trimmed().isEmpty())
                continue;

            QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/ndmdesc_XXXXXX.yaml"));
            if (!tmp.open())
                continue;
            tmp.write(out);
            tmp.flush();

            // Peek the program name so already-loaded plugins are skipped without a reload prompt.
            DescriptionYamlReader reader;
            if (!reader.parseFile(tmp.fileName()))
                continue;
            ProgramInformation info;
            reader.getProgramInformation(info);
            const QString name = info.getProgramName();
            if (name.isEmpty() || programDict.contains(name))
                continue;

            loadProgram(tmp.fileName());
            ++added;
        }
    }
    if (QMainWindow *mw = qobject_cast<QMainWindow *>(window()))
        if (mw->statusBar()) {
            QString msg = tr("Discovered %1 ndm_* plugin(s) on $PATH").arg(added);
            if (skipped > 0)
                msg += tr("; skipped %1 with a newer protocol").arg(skipped);
            mw->statusBar()->showMessage(msg, 4000);
        }
}

void ParameterView::loadProgram(const QString &programUrl) {
    //QString programDecriptionName = QFileInfo(programUrl).fileName();
    counter++;
    programId++;

    // Get the information concering the program from the file.
    DescriptionYamlReader reader;
    reader.parseFile(programUrl);
    ProgramInformation programInformation;
    reader.getProgramInformation(programInformation);

    int reloadIndex = -1;   // place of a plugin being reloaded
    QString name = programInformation.getProgramName();
    //If the description file was incorrect, no name was supplied
    if(name.isEmpty())
        name = QString::fromLatin1("Untitled-%1").arg(programId);

    if(programDict.contains(name)){
        const QString message =  tr("The selected plugin %1 is already loaded. Do you want to reload it?").arg(name);
        const int answer = QMessageBox::question(this,tr("plugin already loaded"),message, QMessageBox::Yes|QMessageBox::No );
        if(answer == QMessageBox::No){
            counter--;
            return;
        } else {
            reloadIndex = programOrder.indexOf(name);
            ProgramPage* programPage = programDict[name].page;
            removeProgram(programPage);
        }
    }

    programsModified = true;

    ProgramPage* program = addProgram(name, true, programInformation.getCategory());
    // A reloaded plugin keeps its place in the program list (and so in the
    // session file's programs: order, which ndm_start runs in), instead of
    // moving to the end.
    if (reloadIndex >= 0 && reloadIndex < programOrder.size() - 1) {
        programOrder.removeAll(name);
        programOrder.insert(reloadIndex, name);
        placeProgramItems();
        emit scriptListHasBeenModified(programOrder);
    }

    //Set the parameters
    ParameterPage* parameterPage = program->getParameterPage();
    parameterPage->setProgramName(name);
    QMap<int, QStringList > info = programInformation.getParameterInformation();
    parameterPage->setParameterInformation(info);

    //set the help
    program->setHelp(programInformation.getHelp());
    if(expertMode){
        //set the script if any: the file corresponding to the program name
        const QString path = NdManagerUtils::findExecutable(name);
        if(!path.isNull()){
            const QString message = program->loadScript(path);
            if(!message.isEmpty())
                QMessageBox::critical (this,tr("IO Error!"),message);
        }
        program->initialisationOver();
    }
    emit scriptListHasBeenModified(QStringList()<<programDict.keys());
}

void ParameterView::nbChannelsModified(int nbChannels){
    //All the parameters which use the number of channels are reset.
    //Exception: when the count was unset (0, e.g. a session made from a
    //template that leaves nChannels empty) there are no channel assignments
    //to invalidate, so the groups, mappings and probes are kept and only the
    //per-channel tables (attributes, colors, offsets) are sized.
    const bool countWasUnset = (this->nbChannels == 0);
    this->nbChannels = nbChannels;

    probe->setNbChannels(nbChannels);  // always, so importProbeYaml has it
    spike->setNbChannels(nbChannels);
    if(!countWasUnset){
        // ── Probe page: clear all probe entries ──────────────────────────
        // A channel-count change invalidates all probe-to-channel assignments.
        if(expertMode){
            probe->setProbes(QList<ProbeEntry>());
            probe->setModified(false);
        }

        //the files page
        //remove any channel mapping
        QMap<int, QList<int> > mapping;
        QList<FilePage*> fileList;
        files->getFilePages(fileList);
        for(int i= 0 ; i < fileList.count();++i) {
            fileList.at(i)->setChannelMapping(mapping);
        }

        //the anatomical groups page: one group containing all channels 0..(N-1)
        QMap<int, QList<int> > anatomicalGroups;
        QList<int> anatomicalGroupOne;
        for(int i = 0; i<nbChannels;++i) anatomicalGroupOne.append(i);
        anatomicalGroups.insert(1,anatomicalGroupOne);
        anatomy->setGroups(anatomicalGroups);

        //the spike groups page
        //all the channels are put in the undefined group, therefore there is not group in the map (the trash and undefined groups are not shown)
        QMap<int, QList<int> > spikeGroups;
        //no other information is provided
        QMap<int, QMap<QString,QString> > spikeGroupsInformation;
        spike->setGroups(spikeGroups,spikeGroupsInformation);
    }

    //the anatomical attributes: one row per channel
    anatomy->setNbChannels(nbChannels);
    //For the moment the attribute names and values are hard coded (there is only the skip attribut with a default at 0)
    QMap<QString, QMap<int,QString> > attributes;
    QMap<int,QString> skip;
    for(int i = 0; i<nbChannels;++i)
        skip.insert(i,"0");
    attributes.insert(SKIP,skip);
    anatomy->setAttributes(attributes);

    //the channel color page
    this->channelColors->setNbChannels(nbChannels);
    //all the channels have the same default color
    QList<ChannelColors> channelColorsList;
    for(int i = 0; i<nbChannels;++i){
        ChannelColors channelColors;
        channelColors.setId(i);
        channelColors.setColor(DEFAULT_COLOR);
        channelColors.setGroupColor(DEFAULT_COLOR);
        channelColors.setSpikeGroupColor(DEFAULT_COLOR);
        channelColorsList.append(channelColors);
    }
    this->channelColors->setColors(channelColorsList);

    //the channel offset page: one row per channel, all at the default offset 0
    this->channelDefaultOffsets->setNbChannels(nbChannels);
}

QStringList ParameterView::modifiedScripts() const{
    QStringList programModified;

    QMapIterator<QString, ProgramPageId> i(programDict);
    while (i.hasNext())  {
        i.next();
        ProgramPage* program = i.value().page;
        if(program->isScriptModified()) {
            programModified.append(i.key());
        }
    }
    return programModified;
}

QStringList ParameterView::modifiedProgramDescription() const{
    QStringList programModified;

    QMapIterator<QString, ProgramPageId> i(programDict);
    while (i.hasNext())  {
        i.next();
        ProgramPage* program = i.value().page;
        if(program->isDescriptionModifiedAndNotSaved()) {
            programModified.append(i.key());
        }
    }
    return programModified;
}


bool ParameterView::isModified(){
    return isSessionModified() || isPipelineModified();
}

bool ParameterView::isSessionModified(){
    bool parameterModified = false;
    bool descriptionModified = false;

    QMapIterator<QString, ProgramPageId> i(programDict);
    while (i.hasNext())  {
        i.next();
        ProgramPage* program = i.value().page;
        parameterModified = program->areParametersModified();
        descriptionModified = program->isDescriptionModified();
        if(parameterModified || descriptionModified)
            break;
    }

    const bool p = (programsModified ||
                    (counter != 0)||
                    generalInfo->isModified() ||
                    acquisitionSystem->isModified() ||
                    video->isModified() ||
                    lfp->isModified()  ||
                    anatomy->isModified() ||
                    spike->isModified() ||
                    probe->isModified() ||
                    unitList->isModified() ||
                    miscellaneous->isModified() ||
                    neuroscopeVideo->isModified() ||
                    clusters->isModified() ||
                    this->channelColors->isModified() ||
                    this->channelDefaultOffsets->isModified() ||
                    files->isModified() ||
                    parameterModified ||
                    descriptionModified);

    return p;
}

void ParameterView::getInformation(QMap<int, QList<int> >& anatomicalGroups,QMap<QString, QMap<int,QString> >& attributes,
                                   QMap<int, QList<int> >& spikeGroups,QMap<int, QMap<QString,QString> >& spikeGroupsInformation,QMap<int, QStringList >& units,
                                   GeneralInformation& generalInformation,QMap<QString,double>& acquisitionSystemInfo,QMap<QString,double>& videoInformation,
                                   QList<FileInformation>& files,QList<ChannelColors>& channelColors,QMap<int,int>& channelDefaultOffsets,
                                   NeuroscopeVideoInfo& neuroscopeVideoInfo,QList<ProgramInformation>& programs,
                                   double& lfpRate,float& screenGain,int& nbSamples,int& peakSampleIndex,QString& traceBackgroundImage){

    //First check if the number of channels has changed before returning the information.
    acquisitionSystem->checkNbChannels();

    //Gather the information from the different pages
    anatomy->getGroups(anatomicalGroups);
    anatomy->getAttributes(attributes);

    spike->getGroups(spikeGroups);
    spike->getGroupInformation(spikeGroupsInformation);

    unitList->getUnits(units);

    generalInformation.setDate(generalInfo->getDate());
    generalInformation.setDescription(generalInfo->getDescription());
    generalInformation.setExperimenters(generalInfo->getExperimenters());
    generalInformation.setNotes(generalInfo->getNotes());

    acquisitionSystemInfo.insert(AMPLIFICATION,static_cast<float>(acquisitionSystem->getAmplification()));
    acquisitionSystemInfo.insert(OFFSET,static_cast<float>(acquisitionSystem->getOffset()));
    acquisitionSystemInfo.insert(BITS,static_cast<float>(acquisitionSystem->getResolution()));
    acquisitionSystemInfo.insert(SAMPLING_RATE,acquisitionSystem->getSamplingRate());
    acquisitionSystemInfo.insert(VOLTAGE_RANGE,static_cast<float>(acquisitionSystem->getVoltageRange()));
    acquisitionSystemInfo.insert(NB_CHANNELS,static_cast<float>(acquisitionSystem->getNbChannels()));

    //If the width is 0 (<=> no video info has been provided), do not store the video information
    if(video->getWidth() != 0){
        videoInformation.insert(SAMPLING_RATE,video->getSamplingRate());
        videoInformation.insert(WIDTH,video->getWidth());
        videoInformation.insert(HEIGHT,video->getHeight());
    }

    lfpRate = lfp->getSamplingRate();
    screenGain = miscellaneous->getScreenGain();
    traceBackgroundImage = miscellaneous->getTraceBackgroundImage();
    nbSamples = clusters->getNbSamples();
    peakSampleIndex = clusters->getPeakIndex();
    neuroscopeVideoInfo.setBackgroundImage(neuroscopeVideo->getBackgroundImage());
    neuroscopeVideoInfo.setFlip(neuroscopeVideo->getFlip());
    neuroscopeVideoInfo.setRotation(neuroscopeVideo->getRotation());
    neuroscopeVideoInfo.setTrajectory(neuroscopeVideo->getPositionsBackground());

    this->channelColors->getColors(channelColors);
    this->channelDefaultOffsets->getOffsets(channelDefaultOffsets);

    QList<FilePage*> fileList;
    this->files->getFilePages(fileList);
    FilePage* filePage;
    for(int i = 0; i <fileList.count(); ++i) {
        filePage = fileList.at(i);
        FileInformation fileInformation;
        fileInformation.setSamplingRate(filePage->getSamplingRate());
        fileInformation.setExtension(filePage->getExtension());
        QMap<int, QList<int> > mapping = filePage->getChannelMapping();
        fileInformation.setChannelMapping(mapping);
        files.append(fileInformation);
    }

    // Write programs in list order, not programDict's alphabetical order:
    // ndm_start must stay programs[0] and the rest keep their run order.
    for (const QString& name : programOrder) {
        if (!programDict.contains(name)) continue;
        ProgramPage* program = programDict.value(name).page;
        ProgramInformation programInformation;
        programInformation.setProgramName(name);
        programInformation.setHelp(program->getHelp());
        programInformation.setCategory(program->getCategory());
        ParameterPage* parameterPage = program->getParameterPage();
        QMap<int, QStringList > parameterInformation = parameterPage->getParameterInformation();
        programInformation.setParameterInformation(parameterInformation);
        programs.append(programInformation);
    }
}

void ParameterView::hasBeenSave(){
    programsModified = false;

    emit resetModificationStatus();

    //This object has a track of all the programPage
    QMapIterator<QString, ProgramPageId> i(programDict);
    while (i.hasNext())  {
        i.next();
        ProgramPage* program = i.value().page;
        program->resetModificationStatus();
    }
    counter = 0; /// added by MZ
}

bool ParameterView::saveScript(const QString& programName){
    ProgramPage* program = programDict[programName].page;
    return program->saveProgramScript();
}

void ParameterView::saveProgramDescription(const QString& programName){
    ProgramPage* program = programDict[programName].page;
    program->saveProgramParameters();
}


void ParameterView::scriptHidden(){
    emit partHidden();
}

QStringList ParameterView::getFileScriptNames()const
{
    return programOrder;
}

void ParameterView::setCurrentPage(int index)
{
    mStackWidget->setCurrentIndex(index);
}

int ParameterView::currentPage() const
{
    return mStackWidget->currentIndex();
}

// ---------------------------------------------------------------------------
// Probe data — called from ndmanagerdoc after createParameterView/initialize
// ---------------------------------------------------------------------------

void ParameterView::setProbeData(const QList<ProbeEntry>& probes,
                                 const QString& libraryPath)
{
    // Session-local probe files sit next to the document; the lambda
    // follows the document through Save As.
    probe->setSessionFileProvider([this] { return doc.url(); });
    probe->setProbes(probes);
    if (!libraryPath.isEmpty())
        probe->setLibraryPath(libraryPath);
    probe->setModified(false);
}

void ParameterView::getProbeData(QList<ProbeEntry>& probes,
                                 QString& libraryPath) const
{
    probe->getProbes(probes);
    libraryPath = probe->getLibraryPath();
}

// ---------------------------------------------------------------------------
// applyProbeLayout
// ---------------------------------------------------------------------------

void ParameterView::applyProbeLayout(QList<ProbeEntry>     /*probes*/,
                                     QMap<int,QList<int>>  newAnatomy,
                                     QMap<int,QList<int>>  newSpike,
                                     int                   /*firstNewGroupId*/)
{
    if (!expertMode) return;  // anatomy/spike pages only exist in expert mode

    // The probe tab computed one group per shank plus a leftover anatomy
    // group; that becomes the complete description.  A group whose channel
    // set the pages already hold keeps what the user set on it -- its
    // channel order, its channels' skip flags, its spike parameters
    // (nSamples, peakSampleIndex, nFeatures, sdiffPairs) -- so adding a
    // probe, or a geometry edit that changes one shank, does not reset
    // every other group.  probeId / shankIndex are written per group on
    // save (writeProbesSection), from the probe tab's layout.
    QMap<int,QList<int>> oldAnatomy, oldSpike;
    QMap<QString,QMap<int,QString>> oldAttributes;
    QMap<int,QMap<QString,QString>> oldSpikeInfo;
    anatomy->getGroups(oldAnatomy);
    anatomy->getAttributes(oldAttributes);
    spike->getGroups(oldSpike);
    spike->getGroupInformation(oldSpikeInfo);

    auto sameSetIn = [](const QMap<int,QList<int>>& old, const QList<int>& chs) {
        const QSet<int> s(chs.cbegin(), chs.cend());
        for (auto it = old.cbegin(); it != old.cend(); ++it)
            if (QSet<int>(it.value().cbegin(), it.value().cend()) == s)
                return it.key();
        return -1;
    };

    QSet<int> keptChannels;          // channels of anatomy groups left as they were
    for (auto it = newAnatomy.begin(); it != newAnatomy.end(); ++it) {
        const int k = sameSetIn(oldAnatomy, it.value());
        if (k < 0) continue;
        it.value() = oldAnatomy.value(k);
        for (int ch : it.value()) keptChannels.insert(ch);
    }
    anatomy->setNbChannels(nbChannels);
    anatomy->setGroups(newAnatomy);

    // Skip flags (per channel): kept for the channels of unchanged groups,
    // 0 elsewhere.  The attribute keys are the page's own column names.
    QStringList attributeKeys = oldAttributes.keys();
    if (attributeKeys.isEmpty()) attributeKeys << QStringLiteral("Skip");
    QMap<QString, QMap<int,QString>> attributes;
    for (const QString& key : attributeKeys) {
        const QMap<int,QString> old = oldAttributes.value(key);
        QMap<int,QString> values;
        for (int i = 0; i < nbChannels; ++i)
            values.insert(i, keptChannels.contains(i) && old.contains(i)
                                 ? old.value(i) : QStringLiteral("0"));
        attributes.insert(key, values);
    }
    anatomy->setAttributes(attributes);
    anatomy->setModified(true);

    // Spike groups: an unchanged group keeps its row; a new one gets the
    // defaults.  Every group gets nSamples / peakSampleIndex / nFeatures
    // (cells left null in cols 1-3 crash getGroupInformation on save).
    QMap<int, QMap<QString,QString>> spikeInfo;
    for (auto it = newSpike.begin(); it != newSpike.end(); ++it) {
        QMap<QString,QString> info;
        const int k = sameSetIn(oldSpike, it.value());
        if (k >= 0) {
            it.value() = oldSpike.value(k);
            info = oldSpikeInfo.value(k);
        }
        // Use the tag constants, not hand-typed strings: SpikePage::setGroups
        // matches on these exact keys, and "nbSamples"/"nbFeatures" silently
        // matched nothing, leaving those cells blank to be saved as 0.
        if (info.value(NB_SAMPLES).isEmpty())        info[NB_SAMPLES]        = QStringLiteral("52");
        if (info.value(PEAK_SAMPLE_INDEX).isEmpty()) info[PEAK_SAMPLE_INDEX] = QStringLiteral("26");
        if (info.value(NB_FEATURES).isEmpty())       info[NB_FEATURES]       = QStringLiteral("3");
        spikeInfo[it.key()] = info;
    }
    spike->setGroups(newSpike, spikeInfo);
    spike->setModified(true);
}

// ---------------------------------------------------------------------------
// ParameterView::setProgramList
// Called by PipelineDesignerPage::applyRequested.  Orders the Plugins tab as
// the graph runs, without discarding what the Plugins tab already holds —
// see the header for the rules.
// ---------------------------------------------------------------------------
void ParameterView::setProgramList(const QList<ProgramInformation>& newPrograms,
                                   const PipelineParamEdits& edited)
{
    bool changed = false;

    // ── 1. Graph programs, in run order, one entry per plugin type ─────────
    QStringList graphOrder;
    for (const ProgramInformation& prog : newPrograms) {
        const QString name = prog.getProgramName();
        if (name.isEmpty() || graphOrder.contains(name)) continue;
        graphOrder.append(name);
        if (programDict.contains(name)) {
            // Keep the page (tuned values, help, script); write only what
            // the user changed in the inspector.
            ParameterPage* pp = programDict.value(name).page->getParameterPage();
            for (const auto& kv : edited.value(name)) {
                if (pp->setParameterValue(kv.first, kv.second, /*createIfMissing=*/true))
                    changed = true;
            }
        } else {
            ProgramPage* page = addProgram(name, /*show=*/false, prog.getCategory());
            ParameterPage* pp = page->getParameterPage();
            pp->setProgramName(name);
            page->setHelp(prog.getHelp());
            pp->setParameterInformation(prog.getParameterInformation());
            page->initialisationOver();
            changed = true;
        }
    }

    // ── 2. Graph programs first, then everything else in its old order ─────
    QStringList newOrder = graphOrder;
    for (const QString& name : programOrder)
        if (!newOrder.contains(name)) newOrder.append(name);
    if (newOrder != programOrder) {
        programOrder = newOrder;
        changed = true;
    }

    // ── 3. Show the Plugins tree in that order ──────────────────────────────
    placeProgramItems();
    mParameterTree->expandItem(mScriptsItem);
    if (changed) programsModified = true;
    emit scriptListHasBeenModified(programOrder);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline-file save / load wiring
// ─────────────────────────────────────────────────────────────────────────────
//
// Pipelines live in `<session>.ndm.<name>.pipeline` files alongside the
// session YAML.  The `.ndm.` infix namespaces our pipeline files so they
// don't collide with `.pipeline` files that might be generated by other
// tools in the same session directory (e.g. Klusters, snakemake-style
// workflow runners, etc.).
//
// The default pipeline (auto-loaded on document open) is named "default":
//
//     /data/session_001/session_001.ndm.default.pipeline
//     /data/session_001/session_001.ndm.best.pipeline
//     /data/session_001/session_001.ndm.experimental.pipeline
//
// ParameterView is the natural home for these slots because it holds both
// the document reference (for the URL) and the pipelineDesigner page
// (for the graph data).

namespace {

// Filename infix that namespaces ndmanager-produced pipeline files.
constexpr const char* kPipelineInfix = ".ndm.";
constexpr const char* kPipelineSuffix = ".pipeline";
constexpr const char* kDefaultPipelineName = "default";

// Sanitise a user-typed pipeline name to the bare form we embed in the
// filename.  Drops everything except [A-Za-z0-9_-]; collapses whitespace
// to underscore; lowercases.  Returns empty string on failure (caller
// should re-prompt).
QString sanitisePipelineName(const QString& raw)
{
    QString s = raw.trimmed().toLower();
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("_"));
    s.remove(QRegularExpression(QStringLiteral("[^a-z0-9_\\-]")));
    return s;
}

}  // namespace

bool ParameterView::isPipelineModified() const
{
    return pipelineDesigner && pipelineDesigner->isModified();
}

QString ParameterView::sessionBasePath() const
{
    const QString url = doc.url();
    if (url.isEmpty()) return QString();
    QFileInfo fi(url);
    // Strip the parameter-file extension (.yaml, .yml, .xml).  Anything
    // else is left intact — e.g. a user-named "session.archived.yaml"
    // becomes "session.archived" which is a fine pipeline-file basename.
    QString stem = fi.completeBaseName();
    const QString suffix = fi.suffix().toLower();
    if (suffix == QLatin1String("yaml") ||
        suffix == QLatin1String("yml") ||
        suffix == QLatin1String("xml")) {
        // completeBaseName already strips the final extension, so stem is
        // already correct.
    } else {
        stem = fi.fileName();  // no extension → use the whole filename
    }
    return fi.absolutePath() + QLatin1Char('/') + stem;
}

bool ParameterView::checkSessionSavedForPipeline(const QString& title)
{
    // A New / Untitled document's url is a placeholder in the working
    // directory, and a Use-Template document's url is the template's: the
    // pipeline file would land as ./Untitled.ndm.default.pipeline or beside
    // the template, where neither the session nor ndm_start finds it.
    // Only a document backed by its own session file has a real base path.
    if (doc.isSaved() && !sessionBasePath().isEmpty()) return true;
    QMessageBox::warning(this, title,
        tr("Save the session file first.\n\nThe pipeline file is written beside "
           "the session file (<session>.ndm.<name>.pipeline), and this session "
           "has not been saved to a file of its own yet."));
    return false;
}

bool ParameterView::savePipelineDefault()
{
    if (!pipelineDesigner) return false;
    if (!checkSessionSavedForPipeline(tr("Save Pipeline"))) return false;
    const QString base = sessionBasePath();
    const QString path = base + kPipelineInfix
                         + QLatin1String(kDefaultPipelineName) + kPipelineSuffix;
    // If this file failed to load, the graph on screen is not what it holds
    // (typically the bare root): don't overwrite it unasked.
    if (!pipelineDesigner->failedLoadPath().isEmpty()
        && QFileInfo(path).absoluteFilePath() == pipelineDesigner->failedLoadPath()) {
        if (QMessageBox::question(this, tr("Save Pipeline"),
                tr("'%1' could not be loaded, so the graph shown is not the pipeline "
                   "it holds.  Overwrite it with the current graph?")
                  .arg(QFileInfo(path).fileName()),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
            return false;
        }
    }
    QString error;
    if (!pipelineDesigner->savePipelineFile(path, &error)) {
        QMessageBox::critical(this, tr("Save Pipeline"),
            tr("Failed to save pipeline:\n%1").arg(error));
        return false;
    }
    // Lightweight success feedback — a full message box would be too noisy
    // for a frequently-used save action.  Use the window title bar via the
    // top-level window's status bar if accessible; otherwise stay silent
    // (the file is saved either way).
    if (QWidget* tlw = window()) {
        if (auto* mw = qobject_cast<QMainWindow*>(tlw)) {
            if (QStatusBar* sb = mw->statusBar()) {
                sb->showMessage(tr("Pipeline saved: %1").arg(QFileInfo(path).fileName()),
                                3000);
            }
        }
    }
    return true;
}

void ParameterView::savePipelineAs()
{
    if (!pipelineDesigner) return;
    if (!checkSessionSavedForPipeline(tr("Save Pipeline As"))) return;
    const QString base = sessionBasePath();

    bool ok = false;
    const QString rawName = QInputDialog::getText(this,
        tr("Save Pipeline As"),
        tr("Pipeline name (e.g. \"best\", \"experimental\"):\n"
           "Will be saved as <session>.ndm.<name>.pipeline"),
        QLineEdit::Normal,
        QString(),
        &ok);
    if (!ok) return;
    const QString name = sanitisePipelineName(rawName);
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("Save Pipeline As"),
            tr("Pipeline name must contain at least one alphanumeric character "
               "(letters, digits, underscore, or hyphen)."));
        return;
    }
    if (name == QLatin1String(kDefaultPipelineName)) {
        // Allowed but warn — usually Save As means "make a non-default copy".
        if (QMessageBox::question(this, tr("Save Pipeline As"),
                tr("Saving as 'default' will overwrite the auto-loaded pipeline. "
                   "Continue?"),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    const QString path = base + kPipelineInfix + name + kPipelineSuffix;
    if (QFileInfo::exists(path)) {
        if (QMessageBox::question(this, tr("Save Pipeline As"),
                tr("'%1' already exists.  Overwrite?")
                  .arg(QFileInfo(path).fileName()),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    QString error;
    // Only the default pipeline is what the close prompt protects (and what
    // ndm_start runs without NDM_PIPELINE): a copy under another name leaves
    // the graph's edits unsaved as far as the default file is concerned.
    const bool isDefault = (name == QLatin1String(kDefaultPipelineName));
    if (!pipelineDesigner->savePipelineFile(path, &error, isDefault)) {
        QMessageBox::critical(this, tr("Save Pipeline As"),
            tr("Failed to save pipeline:\n%1").arg(error));
        return;
    }
    QMessageBox::information(this, tr("Save Pipeline As"),
        tr("Pipeline saved to:\n%1").arg(path));
}

void ParameterView::loadPipelineDialog()
{
    if (!pipelineDesigner) return;
    const QString base = sessionBasePath();
    QString startDir;
    if (!base.isEmpty()) startDir = QFileInfo(base).absolutePath();

    const QString path = QFileDialog::getOpenFileName(this,
        tr("Load Pipeline"),
        startDir,
        tr("Pipeline files (*.pipeline);;All files (*)"));
    if (path.isEmpty()) return;

    QString warning;
    if (!pipelineDesigner->loadPipelineFile(path, &warning)) {
        QMessageBox::critical(this, tr("Load Pipeline"),
            tr("Failed to load pipeline:\n%1").arg(warning));
        return;
    }
    // Replace any notice about the previous graph (e.g. a failed auto-load).
    pipelineDesigner->showNotice(warning.isEmpty() ? QString()
        : tr("Loaded %1 with changes:\n%2").arg(QFileInfo(path).fileName(), warning));
    if (!warning.isEmpty()) {
        // Non-fatal: e.g. unknown plugin types were dropped.  Inform the user.
        QMessageBox::warning(this, tr("Load Pipeline"), warning);
    }
}

void ParameterView::recordUnits(const QSet<int>& groups, const QList<QStringList>& rows)
{
    QMap<int, QStringList> current;
    unitList->getUnits(current);
    QMap<int, QStringList> merged;
    int key = 1;
    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        const QStringList& row = it.value();
        bool ok = false;
        const int group = row.isEmpty() ? 0 : row.at(0).toInt(&ok);
        if (ok && groups.contains(group)) continue;          // replaced below
        merged.insert(key++, row);
    }
    for (const QStringList& row : rows) merged.insert(key++, row);
    unitList->setNbUnits(merged.count());
    unitList->setUnits(merged);
    unitList->propertyModified();                             // a change to the session
}
