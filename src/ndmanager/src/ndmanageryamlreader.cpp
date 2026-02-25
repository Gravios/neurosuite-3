/***************************************************************************
 * ndmanageryamlreader.cpp
 *
 * Thin delegator — only the two methods that need adapter logic remain here.
 ***************************************************************************/

#include "ndmanageryamlreader.h"

void NdManagerYamlReader::getAcquisitionSystemInfo(QMap<QString,double>& info) const
{
    info[QStringLiteral("nBits")]        = m_reader.getResolution();
    info[QStringLiteral("nChannels")]    = m_reader.getNbChannels();
    info[QStringLiteral("samplingRate")] = m_reader.getSamplingRate();
    info[QStringLiteral("voltageRange")] = m_reader.getVoltageRange();
    info[QStringLiteral("amplification")]= m_reader.getAmplification();
    info[QStringLiteral("offset")]       = m_reader.getOffset();
}

// getChannelColors: maps ChannelColorEntry (libklustersshared) → ndmanager's
// local ChannelColors typedef.  Since ChannelColors IS ChannelColorEntry,
// the list is directly assigned.
void NdManagerYamlReader::getChannelColors(QList<ChannelColors>& list) const
{
    m_reader.getChannelColors(list);
}

void NdManagerYamlReader::getProgramInformation(ProgramInformation& pi) const
{
    const QString target = pi.getProgramName();
    QList<ProgramInformation> all;
    m_reader.getProgramsInformation(all);
    for (const auto& entry : all) {
        if (entry.getProgramName() == target) {
            pi = entry;
            return;
        }
    }
}
