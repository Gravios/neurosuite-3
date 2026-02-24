/***************************************************************************
 * ndmanageryamlreader.cpp
 ***************************************************************************/

#include "ndmanageryamlreader.h"

#include <QDate>

bool NdManagerYamlReader::parseFile(const QString& path)
{
    return m_reader.parseFile(path);
}

void NdManagerYamlReader::closeFile()
{
    m_reader.closeFile();
}

void NdManagerYamlReader::getAcquisitionSystemInfo(QMap<QString,double>& info) const
{
    info[QStringLiteral("nBits")]        = m_reader.getResolution();
    info[QStringLiteral("nChannels")]    = m_reader.getNbChannels();
    info[QStringLiteral("samplingRate")] = m_reader.getSamplingRate();
    info[QStringLiteral("voltageRange")] = m_reader.getVoltageRange();
    info[QStringLiteral("amplification")]= m_reader.getAmplification();
    info[QStringLiteral("offset")]       = m_reader.getOffset();
}

void NdManagerYamlReader::getGeneralInformation(GeneralInformation& gi) const
{
    QString dateStr = m_reader.getDate();
    if (!dateStr.isEmpty()) {
        QDate d = QDate::fromString(dateStr, Qt::ISODate);
        if (d.isValid()) gi.setDate(d);
    }
    gi.setExperimenters(m_reader.getExperimenters());
    gi.setDescription(m_reader.getDescription());
    gi.setNotes(m_reader.getNotes());
}

void NdManagerYamlReader::getProgramsInformation(QList<ProgramInformation>& programs) const
{
    const auto entries = m_reader.getPrograms();
    for (const auto& entry : entries) {
        ProgramInformation pi;
        pi.setProgramName(entry.name);
        pi.setHelp(entry.help);

        QMap<int,QStringList> params;
        int idx = 0;
        for (const auto& pe : entry.parameters) {
            // XmlReader stores each parameter as [name, value, status]
            QStringList row;
            row << pe.name << pe.value << pe.status;
            params.insert(idx++, row);
        }
        pi.setParameterInformation(params);
        programs.append(pi);
    }
}

void NdManagerYamlReader::getProgramInformation(ProgramInformation& pi) const
{
    // Fills the single program whose name matches pi.getProgramName()
    const QString target = pi.getProgramName();
    const auto entries = m_reader.getPrograms();
    for (const auto& entry : entries) {
        if (entry.name != target) continue;
        pi.setHelp(entry.help);
        QMap<int,QStringList> params;
        int idx = 0;
        for (const auto& pe : entry.parameters) {
            QStringList row;
            row << pe.name << pe.value << pe.status;
            params.insert(idx++, row);
        }
        pi.setParameterInformation(params);
        return;
    }
}
