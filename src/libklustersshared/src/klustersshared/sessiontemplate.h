/***************************************************************************
 * sessiontemplate.h  -  a session's configuration as a reusable template
 *
 * A session template is an ordinary session parameter file (it opens, and
 * File > Use Template takes it, like any other) holding only the sections
 * that describe how a kind of recording is set up -- groups, plugins and
 * their parameters, display settings, probes -- plus a `template:` block
 * saying what it is:
 *
 *     template:
 *       name: CA1 4-shank silicon probe
 *       description: Buzsaki64, 20 kHz, KK on stderiv
 *       createdFrom: rat01-20260901.yaml
 *       created: 2026-10-16
 *       sections: [acquisitionSystem, anatomicalDescription, ...]
 *
 * ndManager's File > Export Session as Template writes one from the open
 * session (the user picks the sections); File > Apply Template to Session
 * replaces chosen sections of the open session with the template's.
 *
 * A *section* is a top-level key, except that `probes` carries
 * `probeLibraryPath` with it.  `parameters` (the file header) and
 * `template` are never sections.  Every other key the file holds is a
 * section too, so a block this code does not know about can be exported
 * and applied as well.
 *
 * Qt-free (yaml-cpp and the standard library), so it is tested directly.
 ***************************************************************************/

#ifndef KLUSTERSSHARED_SESSIONTEMPLATE_H
#define KLUSTERSSHARED_SESSIONTEMPLATE_H

#include "libklustersshared_export.h"

#include <yaml-cpp/yaml.h>

#include <string>
#include <vector>

namespace sessiontemplate {

/** What the code knows about a section. */
struct SectionInfo {
    std::string key;            ///< its top-level key
    std::string label;          ///< for the dialogs ("Spike groups")
    std::string hint;           ///< one line on what it holds
    bool        exportDefault;  ///< checked in Export as Template
    bool        applyDefault;   ///< checked in Apply Template
};

/** The known sections in display order.  Session-specific ones (general
 *  information, source recording, units) are not exported by default; the
 *  recording's own setup (acquisition, files, video, probes) is exported
 *  but not applied by default, so applying a template to a session keeps
 *  what was recorded and takes how it is processed. */
KLUSTERSSHARED_EXPORT const std::vector<SectionInfo>& knownSections();

/** The info for @p key: the known one, or for an unknown block a label from
 *  the key, exported by default and not applied by default. */
KLUSTERSSHARED_EXPORT SectionInfo describe(const std::string& key);

/** The top-level keys a section brings along ("probes" -> probes and
 *  probeLibraryPath). */
KLUSTERSSHARED_EXPORT std::vector<std::string> keysOf(const std::string& section);

/** The sections @p doc holds: known ones in knownSections() order, then the
 *  others in file order.  Empty when @p doc is not a mapping. */
KLUSTERSSHARED_EXPORT std::vector<std::string> sectionsOf(const YAML::Node& doc);

/** The `template:` block. */
struct Meta {
    std::string name, description, createdFrom, created;
    std::vector<std::string> sections;
};

/** Reads @p doc's `template:` block (all empty when it has none). */
KLUSTERSSHARED_EXPORT Meta metaOf(const YAML::Node& doc);

/** True when @p doc has a `template:` block. */
KLUSTERSSHARED_EXPORT bool isTemplate(const YAML::Node& doc);

/** A template from @p session: its `parameters` header, the keys of
 *  @p sections (in @p session's order), and a `template:` block from @p meta
 *  whose `sections` lists the ones actually present.  @p session is not
 *  changed (the result holds copies). */
KLUSTERSSHARED_EXPORT YAML::Node makeTemplate(const YAML::Node& session,
                                              const std::vector<std::string>& sections,
                                              const Meta& meta);

/** @p session with the keys of each of @p sections replaced by
 *  @p templ's (copies).  A key the template lacks is removed from the
 *  session only when the template has the section at all (probes without
 *  a probeLibraryPath clears the session's path); a section the template
 *  does not hold is left alone and reported in @p skipped.  A key the
 *  session lacks is added at the end.  The `template:` block is never
 *  copied.  @p session is not changed. */
KLUSTERSSHARED_EXPORT YAML::Node apply(const YAML::Node& session, const YAML::Node& templ,
                                       const std::vector<std::string>& sections,
                                       std::vector<std::string>* skipped = nullptr);

/** acquisitionSystem.nChannels of @p doc, or -1 when absent or not a number.
 *  Groups made for another channel count do not fit a session. */
KLUSTERSSHARED_EXPORT int channelCount(const YAML::Node& doc);

} // namespace sessiontemplate

#endif // KLUSTERSSHARED_SESSIONTEMPLATE_H
