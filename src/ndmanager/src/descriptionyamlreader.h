/***************************************************************************
 * descriptionyamlreader.h
 *
 * Reads an ndmanager program-description YAML file written by
 * DescriptionYamlWriter.  Replaces the XmlReader::getProgramInformation()
 * call in parameterview.cpp.
 ***************************************************************************/
#pragma once

#include <klustersshared/programinformation.h>
#include <QString>

class DescriptionYamlReader
{
public:
    DescriptionYamlReader()  = default;
    ~DescriptionYamlReader() = default;

    /** Parse @p path. Returns true on success. */
    bool parseFile(const QString& path);

    /** Populate @p info with the parsed data. */
    void getProgramInformation(ProgramInformation& info) const;

private:
    ProgramInformation info;
    bool               parsed = false;
};
