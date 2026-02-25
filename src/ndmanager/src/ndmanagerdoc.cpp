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
// include files for Qt
#include <QMap>

#include <QList>
#include <QDebug>
#include <QMessageBox>

// application specific includes
#include <ndmanagerdoc.h>
#include "ndmanager.h"
#include "tags.h"
#include "xmlreader.h"
#include "xmlwriter.h"
#include "ndmanageryamlreader.h"
#include "ndmanageryamlwriter.h"
#include "channelcolors.h"
#include "generalinformation.h"
#include "neuroscopevideoinfo.h"
#include "fileinformation.h"
#include "programinformation.h"
#include "parameterview.h"

#include <QStandardPaths>



using namespace ndmanager;

ndManagerDoc::ndManagerDoc(QWidget* parent)
    :parent(parent)
{
}


ndManagerDoc::~ndManagerDoc(){
}

void ndManagerDoc::closeDocument(){
    docUrl.clear();
}

ndManagerDoc::OpenSaveCreateReturnMessage ndManagerDoc::openDocument(const QString& url){
    docUrl = url;

    // Dispatch to YAML or XML reader based on file extension
    const bool isYaml = url.endsWith(QLatin1String(".yaml"), Qt::CaseInsensitive)
                     || url.endsWith(QLatin1String(".yml"),  Qt::CaseInsensitive);

    if (isYaml) {
        NdManagerYamlReader reader;
        if (!reader.parseFile(url)) return PARSE_ERROR;
        return loadFromReader(reader);
    } else {
        XmlReader reader;
        if (!reader.parseFile(url)) return PARSE_ERROR;
        return loadFromReader(reader);
    }
}

template<typename Reader>
ndManagerDoc::OpenSaveCreateReturnMessage ndManagerDoc::loadFromReader(Reader& reader){
    QMap<int, QList<int> > anatomicalGroups;
    QMap<QString, QMap<int,QString> > attributes;
    QMap<int, QList<int> > spikeGroups;
    QMap<int, QMap<QString,QString> > spikeGroupsInformation;
    QMap<int, QStringList > units;
    GeneralInformation generalInformation;
    QMap<QString,double> acquisitionSystemInfo;
    QMap<QString,double> videoInformation;
    QList<FileInformation> files;
    QList<ChannelColors> channelColors;
    QMap<int,int> channelDefaultOffsets;
    NeuroscopeVideoInfo neuroscopeVideoInfo;
    QList<ProgramInformation> programs;
    double lfpRate;
    float screenGain;
    QString traceBackgroundImage;
    int nbSamples;
    int peakSampleIndex;

    reader.getGeneralInformation(generalInformation);
    //if the experimenters are not defined use the current user
    QString experimenter = generalInformation.getExperimenters().simplified();
    if(experimenter.isEmpty() || experimenter == " ")
        generalInformation.setExperimenters(getenv("USER"));

    reader.getAcquisitionSystemInfo(acquisitionSystemInfo);
    reader.getVideoInfo(videoInformation);
    lfpRate = reader.getLfpInformation();
    //Files info
    reader.getFilesInformation(files);
    //Anatomical and spike groups
    reader.getAnatomicalDescription(static_cast<int>(acquisitionSystemInfo[NB_CHANNELS]),anatomicalGroups,attributes);
    if(anatomicalGroups.contains(0)){
        spikeGroups.insert(0,anatomicalGroups[0]);
        //The trash group is not store as it is not shown (channels that are not in any group are in the trash group).
        //It will not be keep in the spike group either.
        if(anatomicalGroups.contains(0)) anatomicalGroups.remove(0);
    }
    reader.getSpikeDescription(static_cast<int>(acquisitionSystemInfo[NB_CHANNELS]),spikeGroups,spikeGroupsInformation);

    //Units information
    reader.getUnits(units);

    //NeuroScope information
    screenGain = reader.getScreenGain();
    traceBackgroundImage = reader.getTraceBackgroundImage();
    nbSamples = reader.getNbSamples();
    peakSampleIndex = reader.getPeakSampleIndex();
    reader.getChannelColors(channelColors);
    reader.getNeuroscopeVideoInfo(neuroscopeVideoInfo);

    //Build the list of channel default offsets
    reader.getChannelDefaultOffset(channelDefaultOffsets);
    //if no default offset are available in the file, set the default offset to 0
    int channelNb = static_cast<int>(acquisitionSystemInfo[NB_CHANNELS]);
    if(channelDefaultOffsets.size() == 0){
        for(int i = 0; i < channelNb; ++i) channelDefaultOffsets.insert(i,0);
    }
    //if a channel does not have a default offset, assign it the value 0
    if(channelDefaultOffsets.size() != channelNb){
        for(int i = 0; i < channelNb; ++i){
            if(!channelDefaultOffsets.contains(i)) channelDefaultOffsets.insert(i,0);
        }
    }

    //Programs
    reader.getProgramsInformation(programs);

    reader.closeFile();

    //Call the parent to create a ParameterView to display the information loaded from the file.
    static_cast<ndManager*>(parent)->createParameterView(anatomicalGroups,attributes,spikeGroups,spikeGroupsInformation,units,generalInformation,acquisitionSystemInfo,videoInformation,files,channelColors,channelDefaultOffsets,neuroscopeVideoInfo,programs,lfpRate,screenGain,nbSamples,peakSampleIndex,traceBackgroundImage);

    return OK;
}

ndManagerDoc::OpenSaveCreateReturnMessage ndManagerDoc::newDocument(){
    //If the user has no local version of the file the system default is used
#ifdef Q_OS_WIN
    // On Windows, QStandardPaths::ApplicationsLocation returns the user apps path,
    // not the system install prefix — fall back to PROGRAMFILES env var.
    QString path(qgetenv("PROGRAMFILES"));
    path += QLatin1String("/NDManager/share/applications/ndmanager/ndManagerDefault.xml");
#else
    QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, QLatin1String("ndmanager/ndManagerDefault.xml"));
#endif
    if (path.isEmpty()) {
       qDebug()<<" ndManagerDefault.xml is not found. Verify install";
       QMessageBox::critical (0, QObject::tr("Error!"),QObject::tr("The file ndManagerDefault.xml does not exist. Please verify installation"));
    }
    return openDocument(path);
    
}


ndManagerDoc::OpenSaveCreateReturnMessage ndManagerDoc::save(const QString& url){
    //first gather the information
    QMap<int, QList<int> > anatomicalGroups;
    QMap<QString, QMap<int,QString> > attributes;
    QMap<int, QList<int> > spikeGroups;
    QMap<int, QMap<QString,QString> > spikeGroupsInformation;
    QMap<int, QStringList > units;
    GeneralInformation generalInformation;
    QMap<QString,double> acquisitionSystemInfo;
    QMap<QString,double> videoInformation;
    QList<FileInformation> files;
    QList<ChannelColors> channelColors;
    QMap<int,int> channelDefaultOffsets;
    NeuroscopeVideoInfo neuroscopeVideoInfo;
    QList<ProgramInformation> programs;
    double lfpRate;
    float screenGain;
    QString traceBackgroundImage;
    int nbSamples;
    int peakSampleIndex;

    ParameterView* view = static_cast<ndManager*>(parent)->getParameterView();

    view->getInformation(anatomicalGroups,attributes,spikeGroups,spikeGroupsInformation,units,generalInformation,
                         acquisitionSystemInfo,videoInformation,files,channelColors,channelDefaultOffsets,neuroscopeVideoInfo,programs,lfpRate,screenGain,nbSamples,peakSampleIndex,traceBackgroundImage);

    // create a writer to save the information — dispatch on file extension
    const bool saveAsYaml = url.endsWith(QLatin1String(".yaml"), Qt::CaseInsensitive)
                         || url.endsWith(QLatin1String(".yml"),  Qt::CaseInsensitive);

    auto doWrite = [&](auto& writer) -> bool {
        writer.setGeneralInformation(generalInformation);
        writer.setAcquisitionSystemInformation(acquisitionSystemInfo);
        if(!videoInformation.isEmpty())
            writer.setVideoInformation(videoInformation);
        writer.setLfpInformation(lfpRate);
        if(!files.isEmpty())
            writer.setFilesInformation(files);
        writer.setAnatomicalDescription(anatomicalGroups,attributes);
        writer.setSpikeDetectionInformation(spikeGroups,spikeGroupsInformation);
        writer.setUnitsInformation(units);
        writer.setMiscellaneousInformation(screenGain,traceBackgroundImage);
        writer.setNeuroscopeVideoInformation(neuroscopeVideoInfo);
        writer.setNeuroscopeSpikeInformation(nbSamples,peakSampleIndex);
        writer.setChannelDisplayInformation(channelColors,channelDefaultOffsets);
        if(!programs.isEmpty())
            writer.setProgramsInformation(programs);
        return writer.writeTofile(url);
    };

    XmlWriter          xmlWriter;
    NdManagerYamlWriter yamlWriter;
    const bool status = saveAsYaml ? doWrite(yamlWriter) : doWrite(xmlWriter);
    if(!status)
        return CREATION_ERROR;

    //reset the state of the page as saved state
    view->hasBeenSave();

    return OK;
}

ndManagerDoc::OpenSaveCreateReturnMessage ndManagerDoc::saveDefault(){

    QString path = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    QDir dir(path);
    bool ok = dir.mkpath(path);
    path = path + QDir::separator() + QLatin1String("ndManagerDefault.xml");

    return save(path);
}

ndManagerDoc::OpenSaveCreateReturnMessage ndManagerDoc::saveScript(const QString &scriptName){
    ParameterView* view = static_cast<ndManager*>(parent)->getParameterView();
    bool status = view->saveScript(scriptName);
    if(status)
        return OK;
    else
        return SAVE_ERROR;
}
