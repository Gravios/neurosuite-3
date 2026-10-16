/***************************************************************************
 * sessiontemplate.cpp  -  see sessiontemplate.h
 ***************************************************************************/

#include "sessiontemplate.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace sessiontemplate {

namespace {

const char* const kTemplateKey = "template";
const char* const kHeaderKey   = "parameters";

/** The section a top-level key belongs to ("probeLibraryPath" -> "probes"). */
std::string sectionOfKey(const std::string& key)
{
    if (key == "probeLibraryPath") return "probes";
    return key;
}

bool contains(const std::vector<std::string>& v, const std::string& x)
{
    return std::find(v.begin(), v.end(), x) != v.end();
}

std::string scalar(const YAML::Node& n)
{
    return n && n.IsScalar() ? n.Scalar() : std::string();
}

} // namespace

const std::vector<SectionInfo>& knownSections()
{
    static const std::vector<SectionInfo> s = {
        {"generalInfo",           "General information",  "date, experimenters, description, notes",          false, false},
        {"sourceRecording",       "Source recording",     "header metadata of the recording it came from",    false, false},
        {"acquisitionSystem",     "Acquisition system",   "channels, sampling rate, resolution, gain",        true,  false},
        {"fieldPotentials",       "Field potentials",     "LFP sampling rate",                                true,  true},
        {"files",                 "Extra files",          "derived files: extension, sampling rate, mapping", true,  false},
        {"video",                 "Video",                "frame size and tracking rate",                     true,  false},
        {"probes",                "Probes",               "probes, their .probe files and the library path",  true,  false},
        {"anatomicalDescription", "Anatomical groups",    "channel groups and per-channel attributes",        true,  true},
        {"spikeDetection",        "Spike groups",         "spike groups, waveform window, features",          true,  true},
        {"units",                 "Units",                "the curation record",                              false, false},
        {"neuroscope",            "NeuroScope display",   "colours, offsets, screen gain, spike view",        true,  true},
        {"programs",              "Plugins",              "the plugins and their parameters",                 true,  true},
    };
    return s;
}

SectionInfo describe(const std::string& key)
{
    for (const SectionInfo& s : knownSections())
        if (s.key == key) return s;
    // An unknown block: named after its key ("myLab" -> "MyLab"), taken
    // along by an export (it is part of the setup as far as anyone knows)
    // but not applied unless asked for.
    std::string label = key;
    if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    return {key, label, std::string(), true, false};
}

std::vector<std::string> keysOf(const std::string& section)
{
    if (section == "probes") return {"probes", "probeLibraryPath"};
    return {section};
}

std::vector<std::string> sectionsOf(const YAML::Node& doc)
{
    std::vector<std::string> present;
    if (!doc || !doc.IsMap()) return present;
    for (const auto& kv : doc) {
        const std::string key = scalar(kv.first);
        if (key.empty() || key == kTemplateKey || key == kHeaderKey) continue;
        const std::string sec = sectionOfKey(key);
        if (!contains(present, sec)) present.push_back(sec);
    }
    std::vector<std::string> out;
    for (const SectionInfo& s : knownSections())
        if (contains(present, s.key)) out.push_back(s.key);
    for (const std::string& sec : present)
        if (!contains(out, sec)) out.push_back(sec);
    return out;
}

Meta metaOf(const YAML::Node& doc)
{
    Meta m;
    if (!doc || !doc.IsMap()) return m;
    const YAML::Node t = doc[kTemplateKey];
    if (!t || !t.IsMap()) return m;
    m.name        = scalar(t["name"]);
    m.description = scalar(t["description"]);
    m.createdFrom = scalar(t["createdFrom"]);
    m.created     = scalar(t["created"]);
    const YAML::Node secs = t["sections"];
    if (secs && secs.IsSequence())
        for (const auto& s : secs)
            if (!scalar(s).empty()) m.sections.push_back(scalar(s));
    return m;
}

bool isTemplate(const YAML::Node& doc)
{
    return doc && doc.IsMap() && doc[kTemplateKey] && doc[kTemplateKey].IsMap();
}

YAML::Node makeTemplate(const YAML::Node& session, const std::vector<std::string>& sections,
                        const Meta& meta)
{
    YAML::Node out(YAML::NodeType::Map);
    if (!session || !session.IsMap()) return out;

    std::vector<std::string> kept;
    for (const auto& kv : session) {
        const std::string sec = sectionOfKey(scalar(kv.first));
        if (contains(sections, sec) && !contains(kept, sec)) kept.push_back(sec);
    }

    YAML::Node t(YAML::NodeType::Map);
    t["name"] = meta.name;
    if (!meta.description.empty()) t["description"] = meta.description;
    if (!meta.createdFrom.empty()) t["createdFrom"] = meta.createdFrom;
    if (!meta.created.empty())     t["created"]     = meta.created;
    YAML::Node list(YAML::NodeType::Sequence);
    for (const std::string& sec : sectionsOf(session))    // display order
        if (contains(kept, sec)) list.push_back(sec);
    t["sections"] = list;
    out[kTemplateKey] = t;

    if (session[kHeaderKey]) out[kHeaderKey] = YAML::Clone(session[kHeaderKey]);
    for (const auto& kv : session) {
        const std::string key = scalar(kv.first);
        if (key == kTemplateKey || key == kHeaderKey) continue;
        if (contains(kept, sectionOfKey(key))) out[key] = YAML::Clone(kv.second);
    }
    return out;
}

YAML::Node apply(const YAML::Node& session, const YAML::Node& templ,
                 const std::vector<std::string>& sections, std::vector<std::string>* skipped)
{
    YAML::Node out = session && session.IsMap() ? YAML::Clone(session)
                                                : YAML::Node(YAML::NodeType::Map);
    const std::vector<std::string> available = sectionsOf(templ);
    for (const std::string& sec : sections) {
        if (sec == kTemplateKey || sec == kHeaderKey) continue;
        if (!contains(available, sec)) {
            if (skipped) skipped->push_back(sec);
            continue;
        }
        for (const std::string& key : keysOf(sec)) {
            if (templ[key]) out[key] = YAML::Clone(templ[key]);
            else            out.remove(key);
        }
    }
    return out;
}

int channelCount(const YAML::Node& doc)
{
    if (!doc || !doc.IsMap()) return -1;
    const YAML::Node a = doc["acquisitionSystem"];
    if (!a || !a.IsMap()) return -1;
    const std::string s = scalar(a["nChannels"]);
    if (s.empty()) return -1;
    char* end = nullptr;
    const long n = std::strtol(s.c_str(), &end, 10);
    return (end && *end == '\0' && n >= 0) ? static_cast<int>(n) : -1;
}

} // namespace sessiontemplate
