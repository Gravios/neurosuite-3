/***************************************************************************
 * descriptionyamlreader.cpp
 ***************************************************************************/
#include "descriptionyamlreader.h"

#include <yaml-cpp/yaml.h>

#include <QDebug>
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
            if (!p.IsMap()) {
                qWarning() << "DescriptionYamlReader:" << path
                           << ": a parameter entry is not a mapping; skipped";
                continue;
            }
            QStringList row;
            row << QString::fromStdString(safeGet<std::string>(p,"name",""));
            row << QString::fromStdString(safeGet<std::string>(p,"value",""));
            row << QString::fromStdString(safeGet<std::string>(p,"status",""));

            // The optional columns are positional ({..., type, default,
            // hidden}): a missing one before a present one is kept as "".
            // A list or map where a scalar belongs makes the entry unusable.
            static const char* const optional[] = { "type", "default", "hidden" };
            QStringList extra;
            int last = -1;
            bool bad = false;
            for (int k = 0; k < 3; ++k) {
                const YAML::Node v = p[optional[k]];
                if (!v || v.IsNull()) { extra << QString(); continue; }
                if (!v.IsScalar()) { bad = true; break; }
                extra << QString::fromStdString(v.Scalar());
                last = k;
            }
            if (bad) {
                qWarning() << "DescriptionYamlReader:" << path << ": parameter"
                           << row.at(0) << "has a list or map as type/default/hidden; skipped";
                continue;
            }
            for (int k = 0; k <= last; ++k)
                row << extra.at(k);
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
