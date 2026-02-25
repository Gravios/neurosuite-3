/***************************************************************************
 * klustersyamlreader.cpp
 ***************************************************************************/

#include "klustersyamlreader.h"
#include "clusteruserinformation.h"

#include <QFileInfo>
#include <QDebug>

bool KlustersYamlReader::parseFile(const QFile& file, fileType /*type*/)
{
    return m_reader.parseFile(file.fileName());
}

bool KlustersYamlReader::parseFile(const QString& path, fileType /*type*/)
{
    return m_reader.parseFile(path);
}

void KlustersYamlReader::closeFile()
{
    m_reader.closeFile();
}

void KlustersYamlReader::getClusterUserInformation(
        int pGroup,
        QMap<int,ClusterUserInformation>& clusterUserInformationMap) const
{
    // Read all units from the YAML file and filter to the requested group.
    // getUnits() keys entries by sequential document-order index so there is
    // no collision between groups that share the same cluster ids.
    QMap<int,QStringList> unitsMap;
    m_reader.getUnits(unitsMap);

    // value layout: [0]=group [1]=cluster [2]=structure [3]=type
    //               [4]=isolationDistance [5]=quality [6]=notes
    for (auto it = unitsMap.cbegin(); it != unitsMap.cend(); ++it) {
        const QStringList& info = it.value();
        if (info.size() < 7) continue;
        int group = info[0].toInt();
        if (group != pGroup) continue;

        int clusterId = info[1].toInt();
        ClusterUserInformation cui;
        cui.setGroup(group);
        cui.setCluster(clusterId);
        cui.setStructure(info[2]);
        cui.setType(info[3]);
        cui.setId(info[4]);
        cui.setQuality(info[5]);
        cui.setNotes(info[6]);
        clusterUserInformationMap.insert(clusterId, cui);
    }
}
