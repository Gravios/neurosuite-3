/***************************************************************************
 * neuroscopeyamlreader.cpp
 ***************************************************************************/

#include "neuroscopeyamlreader.h"

bool NeuroscopeYamlReader::parseFile(const QString& path, fileType type)
{
    m_type = type;
    return m_reader.parseFile(path);
}

void NeuroscopeYamlReader::closeFile()
{
    m_reader.closeFile();
}

QList<ChannelDescription> NeuroscopeYamlReader::getChannelDescription() const
{
    QList<ChannelDescription> result;
    QList<QMap<QString,QString>> colors;
    QMap<int,int> offsets;
    const_cast<ParameterYamlReader&>(m_reader).getChannelDisplayInfo(colors, offsets);

    for (const auto& cc : colors) {
        ChannelDescription cd;
        cd.setId(cc.value(QStringLiteral("channel")).toInt());
        cd.setColor(cc.value(QStringLiteral("color")));
        cd.setGroupColor(cc.value(QStringLiteral("anatomyColor")));
        cd.setSpikeGroupColor(cc.value(QStringLiteral("spikeColor")));
        result.append(cd);
    }
    return result;
}

void NeuroscopeYamlReader::getChannelDefaultOffset(QMap<int,int>& channelDefaultOffsets)
{
    QList<QMap<QString,QString>> colors;
    m_reader.getChannelDisplayInfo(colors, channelDefaultOffsets);
}
