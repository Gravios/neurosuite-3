/***************************************************************************
 * descriptionyamlreader.cpp
 ***************************************************************************/
#include "descriptionyamlreader.h"

#include <yaml-cpp/yaml.h>

#include <QMap>
#include <QStringList>

template<typename T>
static T safeGet(const YAML::Node& n, const char* key, T def = T{})
{
    try { if (n[key]) return n[key].as<T>(); } catch (...) {}
    return def;
}

bool DescriptionYamlReader::parseFile(const QString& path)
{
    parsed = false;
    info   = ProgramInformation();

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.toStdString());
    } catch (...) {
        return false;
    }

    YAML::Node prog = root["program"];
    if (!prog) return false;

    info.setProgramName(
        QString::fromStdString(safeGet<std::string>(prog,"name","")));
    info.setHelp(
        QString::fromStdString(safeGet<std::string>(prog,"help","")));

    YAML::Node params = prog["parameters"];
    QMap<int,QStringList> paramMap;
    if (params && params.IsSequence()) {
        int idx = 0;
        for (const auto& p : params) {
            QStringList row;
            row << QString::fromStdString(safeGet<std::string>(p,"name",""));
            row << QString::fromStdString(safeGet<std::string>(p,"value",""));
            row << QString::fromStdString(safeGet<std::string>(p,"status",""));
            if (p["type"])    row << QString::fromStdString(p["type"].as<std::string>());
            if (p["default"]) row << QString::fromStdString(p["default"].as<std::string>());
            if (p["hidden"])  row << QString::fromStdString(p["hidden"].as<std::string>());
            paramMap.insert(idx++, row);
        }
    }
    info.setParameterInformation(paramMap);
    parsed = true;
    return true;
}

void DescriptionYamlReader::getProgramInformation(ProgramInformation& info) const
{
    info = this->info;
}
