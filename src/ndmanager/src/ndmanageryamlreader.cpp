/***************************************************************************
 * ndmanageryamlreader.cpp
 ***************************************************************************/

#include "ndmanageryamlreader.h"

#include <QDate>

bool NdManagerYamlReader::parseFile(const QString& path)
{
    bool ok = m_reader.parseFile(path);
    if (ok) {
        try { m_root = YAML::LoadFile(path.toStdString()); }
        catch (...) { m_root.reset(); }
    }
    return ok;
}

void NdManagerYamlReader::closeFile()
{
    m_reader.closeFile();
    m_root.reset();
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

void NdManagerYamlReader::getFilesInformation(QList<FileInformation>& files) const
{
    files.clear();
    const YAML::Node list = m_root["files"];
    if (!list || !list.IsSequence()) return;
    for (const auto& entry : list) {
        FileInformation fi;
        fi.setSamplingRate(nodeAs<double>(entry["samplingRate"], 0.0));
        fi.setExtension(QString::fromStdString(nodeAs<std::string>(entry["extension"], {})));
        files.append(fi);
    }
}

void NdManagerYamlReader::getChannelColors(QList<ChannelColors>& list) const
{
    list.clear();
    const YAML::Node colors = m_root["neuroscope"]["channels"]["colors"];
    if (!colors || !colors.IsSequence()) return;
    for (const auto& entry : colors) {
        int ch = nodeAs<int>(entry["channel"], -1);
        if (ch < 0) continue;
        ChannelColors cc;
        cc.setId(ch);
        cc.setColor(QString::fromStdString(nodeAs<std::string>(entry["color"],        "#0080ff")));
        cc.setGroupColor(QString::fromStdString(nodeAs<std::string>(entry["anatomyColor"], "#0080ff")));
        cc.setSpikeGroupColor(QString::fromStdString(nodeAs<std::string>(entry["spikeColor"],  "#0080ff")));
        list.append(cc);
    }
}

void NdManagerYamlReader::getChannelDefaultOffset(QMap<int,int>& offsets) const
{
    offsets.clear();
    const YAML::Node list = m_root["neuroscope"]["channels"]["offsets"];
    if (!list || !list.IsSequence()) return;
    for (const auto& entry : list) {
        int ch  = nodeAs<int>(entry["channel"],       -1);
        int off = nodeAs<int>(entry["defaultOffset"],  0);
        if (ch >= 0) offsets.insert(ch, off);
    }
}

void NdManagerYamlReader::getVideoInfo(QMap<QString,double>& videoInformation) const
{
    // YAML schema does not currently store acquisitionSystem video info
    // (distinct from neuroscope video display settings).
    videoInformation.clear();
}

void NdManagerYamlReader::getNeuroscopeVideoInfo(NeuroscopeVideoInfo& videoInfo) const
{
    const YAML::Node v = m_root["neuroscope"]["video"];
    if (!v || !v.IsMap()) return;
    videoInfo.setRotation(nodeAs<int>(v["rotate"], 0));
    videoInfo.setFlip(nodeAs<int>(v["flip"], 0));
    videoInfo.setTrajectory(nodeAs<int>(v["positionsBackground"], 0));
    const std::string img = nodeAs<std::string>(v["videoImage"], {});
    if (!img.empty()) videoInfo.setBackgroundImage(QString::fromStdString(img));
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
