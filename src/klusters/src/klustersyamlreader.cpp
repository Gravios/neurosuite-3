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
    // ParameterYamlReader::getUnits() returns a flat map; we need to filter
    // by group and populate ClusterUserInformation objects.
    // Re-parse the units list directly via getProgramParameter() is not
    // suitable here — instead expose through the shared reader:
    QMap<int,QStringList> unitsMap;
    // We need a mutable copy to call getUnits; cast away const via a local:
    const_cast<ParameterYamlReader&>(m_reader).getUnits(unitsMap);

    // unitsMap key = cluster id, value = [group, cluster, structure, type,
    //                                      isolationDistance, quality, notes]
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
