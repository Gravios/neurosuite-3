/***************************************************************************
 * descriptionyamlwriter.h
 *
 * Writes an ndmanager program-description to a YAML file.
 * Replaces the old DescriptionWriter (QDom-based).
 *
 * YAML format produced:
 *
 *   program:
 *     name: "process_pca"
 *     help: "Computes PCA features …"
 *     parameters:
 *       - name: "nbPCFeatures"
 *         status: "optional"
 *         type: "integer"
 *         default: "3"
 *         hidden: "0"
 ***************************************************************************/
#pragma once

#include <klustersshared/programinformation.h>
#include <QString>

class DescriptionYamlWriter
{
public:
    DescriptionYamlWriter()  = default;
    ~DescriptionYamlWriter() = default;

    /** Store the program information to be written. */
    void setProgramInformation(const ProgramInformation& info);

    /** Serialise and write to @p url. Returns true on success. */
    bool writeTofile(const QString& url);

private:
    ProgramInformation info;
};
