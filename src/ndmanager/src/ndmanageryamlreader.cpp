/***************************************************************************
 * ndmanageryamlreader.cpp
 *
 * Thin delegator — only the two methods that need adapter logic remain here.
 ***************************************************************************/

#include "ndmanageryamlreader.h"

void NdManagerYamlReader::getAcquisitionSystemInfo(QMap<QString,double>& info) const
{
    info[QStringLiteral("nBits")]        = reader.getResolution();
    info[QStringLiteral("nChannels")]    = reader.getNbChannels();
    info[QStringLiteral("samplingRate")] = reader.getSamplingRate();
    info[QStringLiteral("voltageRange")] = reader.getVoltageRange();
    info[QStringLiteral("amplification")]= reader.getAmplification();
    info[QStringLiteral("offset")]       = reader.getOffset();
}

// getChannelColors: maps ChannelColorEntry (libklustersshared) → ndmanager's
// local ChannelColors typedef.  Since ChannelColors IS ChannelColorEntry,
// the list is directly assigned.
void NdManagerYamlReader::getChannelColors(QList<ChannelColors>& list) const
{
    reader.getChannelColors(list);
}

void NdManagerYamlReader::getProgramInformation(ProgramInformation& pi) const
{
    const QString target = pi.getProgramName();
    QList<ProgramInformation> all;
    reader.getProgramsInformation(all);
    for (const auto& entry : all) {
        if (entry.getProgramName() == target) {
            pi = entry;
            return;
        }
    }
}
