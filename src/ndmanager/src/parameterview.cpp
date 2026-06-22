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

//include files for the application
#include "parameterview.h"
#include "ndmanagerdoc.h"
#include "ndmanager.h"
#include "tags.h"
#include "descriptionyamlreader.h"
#include "ndmanagerutils.h"

#include "parametertree.h"

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


    if(expertMode){
        connect(this, &ParameterView::resetModificationStatus, files, &FilesPage::resetModificationStatus);
        connect(this, &ParameterView::resetModificationStatus, anatomy, &AnatomyPage::resetModificationStatus);
        connect(this, &ParameterView::resetModificationStatus, spike, &SpikePage::resetModificationStatus);
        connect(this, &ParameterView::resetModificationStatus, channelDefaultOffsets, &ChannelOffsetsPage::resetModificationStatus);
        connect(this, &ParameterView::resetModificationStatus, probe, &ProbePage::resetModificationStatus);
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

ProgramPage* ParameterView::addProgram(const QString& programName,bool show){
    //adding page "Video"
    QWidget *w = new QWidget(this);
    ProgramPage* program = new ProgramPage(expertMode,w,programName);
    mStackWidget->addWidget(program);

    QTreeWidgetItem * item = mParameterTree->addSubPage(mScriptsItem,programName, program);

    ProgramPageId pageId;
    pageId.item = item;
    pageId.page = program;

    programDict.insert(programName,pageId);

    //set the parameterPage program name
    ParameterPage* parameterPage = program->getParameterPage();
    parameterPage->setProgramName(programName);

    //set connections
    connect(program, &ProgramPage::programNameChanged, this, &ParameterView::changeProgramName);
    connect(program, &ProgramPage::programToRemove, this, &ParameterView::removeProgram);
    connect(program, &ProgramPage::scriptHidden, this, &ParameterView::scriptHidden);

    //Show the new page
    if(show)
        mStackWidget->setCurrentWidget(program);

    return program;
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


    mStackWidget->setCurrentWidget(id.page);
    //If the message if not empty show a message box with it
    if(!message.isEmpty())
        QMessageBox::critical (this,title,message );

    emit scriptListHasBeenModified(QStringList()<<programDict.keys());
}


void ParameterView::removeProgram(ProgramPage* programPage){
    programsModified = true;

    const QString name = programPage->objectName();
    ProgramPageId id = programDict[name];
    mStackWidget->removeWidget(id.page);
    delete id.page;

    delete id.item;
    //mParameterTree->(id.item, 0);
    programDict.remove(name);

    /*if(name.contains("New Program-") || name.contains("Untitled-"))*/ counter--;
    emit scriptListHasBeenModified(QStringList()<<programDict.keys());
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
        ProgramPage* programPage = addProgram(programInformation.getProgramName(),false);
        //set the parameters
        ParameterPage* parameterPage = programPage->getParameterPage();
        QString name = programInformation.getProgramName();
        parameterPage->setProgramName(name);
        //set the help
        programPage->setHelp(programInformation.getHelp());
        QMap<int, QStringList > info = programInformation.getParameterInformation();
        parameterPage->setParameterInformation(info);
        if(expertMode){
            //set the script if any
            QTextEdit* scriptView = programPage->getScriptView();
            //find the file corresponding to the program name
            QString path = NdManagerUtils::findExecutable(name);
            if(!path.isNull()){
                QFile file(path);
                if(!file.open(QIODevice::ReadOnly)){
                    const QString message = tr("The file %1 is not readable.").arg(name);
                    QMessageBox::critical (this,tr("IO Error!"),message);
                } else {
                    QTextStream stream(&file);
                    QString firstLine = stream.readLine();
                    const int i = firstLine.indexOf(QRegularExpression("^#!"));

                    if(i != -1) {
                        scriptView->setText(stream.readAll());
                        file.close();
                    } else {
                        const QString message = tr("The file %1  does not appear to be a plugin file (a script file should begin with #!).").arg(name);
                        QMessageBox::critical (this,tr("IO Error!"),message);
                        scriptView->clear();
                    }
                }
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
                QString warning;
                if (pipelineDesigner->loadPipelineFile(defaultPath, &warning)
                    && !warning.isEmpty()) {
                    // Non-fatal warnings (e.g. unknown plugin types dropped)
                    // are printed to qDebug rather than a modal at open time —
                    // we don't want to interrupt the document-load flow.
                    qWarning("Pipeline auto-load warning: %s",
                             qUtf8Printable(warning));
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
            ProgramPage* programPage = programDict[name].page;
            removeProgram(programPage);
        }
    }

    programsModified = true;

    ProgramPage* program = addProgram(name);

    //Set the parameters
    ParameterPage* parameterPage = program->getParameterPage();
    parameterPage->setProgramName(name);
    QMap<int, QStringList > info = programInformation.getParameterInformation();
    parameterPage->setParameterInformation(info);

    //set the help
    program->setHelp(programInformation.getHelp());
    if(expertMode){
        //set the script if any
        QTextEdit* scriptView = program->getScriptView();
        //find the file corresponding to the program name
        const QString path = NdManagerUtils::findExecutable(name);
        if(!path.isNull()){
            QFile file(path);
            if(!file.open(QIODevice::ReadOnly)){
                const QString message = tr("The file %1 is not readable.").arg(name);
                QMessageBox::critical (this,message, tr("IO Error!"));
            }
            else{
                QTextStream stream(&file);
                QString firstLine = stream.readLine();
                const int i = firstLine.indexOf(QRegularExpression("^#!"));

                if(i != -1){
                    scriptView->setText(stream.readAll());
                    file.close();
                } else {
                    const QString message =  tr("The file %1  does not appear to be a plugin file (a script file should begin with #!).").arg(name);
                    QMessageBox::critical (this,tr("IO Error!"),message);
                    scriptView->clear();
                }
            }
        }
        program->initialisationOver();
    }
    emit scriptListHasBeenModified(QStringList()<<programDict.keys());
}

void ParameterView::nbChannelsModified(int nbChannels){
    //All the parameters which use the number of channels are reset
    this->nbChannels = nbChannels;

    // ── Probe page: clear all probe entries and update channel count ─────
    // A channel-count change invalidates all probe-to-channel assignments.
    probe->setNbChannels(nbChannels);  // always, so importProbeYaml has it
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
    anatomy->setNbChannels(nbChannels);
    QMap<int, QList<int> > anatomicalGroups;
    QList<int> anatomicalGroupOne;
    for(int i = 0; i<nbChannels;++i) anatomicalGroupOne.append(i);
    anatomicalGroups.insert(1,anatomicalGroupOne);
    anatomy->setGroups(anatomicalGroups);
    //For the moment the attribute names and values are hard coded (there is only the skip attribut with a default at 0)
    QMap<QString, QMap<int,QString> > attributes;
    QMap<int,QString> skip;
    for(int i = 0; i<nbChannels;++i)
        skip.insert(i,"0");
    attributes.insert("Skip",skip);
    anatomy->setAttributes(attributes);

    //the spike groups page
    //all the channels are put in the undefined group, therefore there is not group in the map (the trash and undefined groups are not shown)
    QMap<int, QList<int> > spikeGroups;
    //no other information is provided
    QMap<int, QMap<QString,QString> > spikeGroupsInformation;
    spike->setGroups(spikeGroups,spikeGroupsInformation);

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

    QMapIterator<QString, ProgramPageId> i(programDict);
    while (i.hasNext())  {
        i.next();
        const QString name = i.key();
        ProgramPage* program = i.value().page;
        ProgramInformation programInformation;
        programInformation.setProgramName(name);
        programInformation.setHelp(program->getHelp());
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
    return QStringList()<<programDict.keys();
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

    // Replace anatomy groups entirely with what the probe defines.
    // The caller (ProbePage::browseProbeFile) has already computed:
    //   - one group per shank, containing probe channels
    //   - an optional final group for any leftover channels
    // We set this as the complete anatomical description.
    anatomy->setNbChannels(nbChannels);
    anatomy->setGroups(newAnatomy);

    // Rebuild skip attributes for all channels in the new groups
    QMap<QString, QMap<int,QString>> attributes;
    QMap<int,QString> skip;
    for (int i = 0; i < nbChannels; ++i)
        skip.insert(i, QStringLiteral("0"));
    attributes.insert(QStringLiteral("Skip"), skip);
    anatomy->setAttributes(attributes);
    anatomy->setModified(true);

    // Replace spike groups with defaults for nSamples, peakSampleIndex, nFeatures.
    // Pre-populate spikeInfo so setGroups fills all four columns — cells that
    // remain null in cols 1-3 crash getGroupInformation on save.
    QMap<int, QMap<QString,QString>> spikeInfo;
    for (int gid : newSpike.keys()) {
        QMap<QString,QString> info;
        info[QStringLiteral("nbSamples")]       = QStringLiteral("52");
        info[QStringLiteral("peakSampleIndex")] = QStringLiteral("26");
        info[QStringLiteral("nbFeatures")]      = QStringLiteral("3");
        spikeInfo[gid] = info;
    }
    spike->setGroups(newSpike, spikeInfo);
    spike->setModified(true);
}

// ---------------------------------------------------------------------------
// ParameterView::setProgramList
// Called by PipelineDesignerPage::applyRequested to push the graph back
// into the Plugins tree.  Replaces all existing programs in order.
// ---------------------------------------------------------------------------
void ParameterView::setProgramList(const QList<ProgramInformation>& newPrograms)
{
    // ── 1. Remove all existing programs ────────────────────────────────────
    const QStringList existing = programDict.keys();
    for (const QString& name : existing) {
        ProgramPageId pid = programDict[name];
        // Remove tree item (deleting a QTreeWidgetItem removes it from its parent)
        delete pid.item;
        mStackWidget->removeWidget(pid.page);
        pid.page->deleteLater();
    }
    programDict.clear();

    // ── 2. Add each program from the pipeline designer in order ────────────
    for (const ProgramInformation& prog : newPrograms) {
        ProgramPage* page = addProgram(prog.getProgramName(), /*show=*/false);
        ParameterPage* pp = page->getParameterPage();
        pp->setProgramName(prog.getProgramName());
        page->setHelp(prog.getHelp());
        pp->setParameterInformation(prog.getParameterInformation());
        page->initialisationOver();
    }

    mParameterTree->expandItem(mScriptsItem);
    programsModified = true;
    emit scriptListHasBeenModified(programDict.keys());
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

void ParameterView::savePipelineDefault()
{
    if (!pipelineDesigner) return;
    const QString base = sessionBasePath();
    if (base.isEmpty()) {
        QMessageBox::warning(this, tr("Save Pipeline"),
            tr("Cannot save pipeline: the session has no associated file path. "
               "Save the session document first."));
        return;
    }
    const QString path = base + kPipelineInfix
                         + QLatin1String(kDefaultPipelineName) + kPipelineSuffix;
    QString error;
    if (!pipelineDesigner->savePipelineFile(path, &error)) {
        QMessageBox::critical(this, tr("Save Pipeline"),
            tr("Failed to save pipeline:\n%1").arg(error));
        return;
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
}

void ParameterView::savePipelineAs()
{
    if (!pipelineDesigner) return;
    const QString base = sessionBasePath();
    if (base.isEmpty()) {
        QMessageBox::warning(this, tr("Save Pipeline As"),
            tr("Cannot save pipeline: the session has no associated file path. "
               "Save the session document first."));
        return;
    }

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
    if (!pipelineDesigner->savePipelineFile(path, &error)) {
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
    if (!warning.isEmpty()) {
        // Non-fatal: e.g. unknown plugin types were dropped.  Inform the user.
        QMessageBox::warning(this, tr("Load Pipeline"), warning);
    }
}
