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

namespace {

/** A top-level key line's key ("probes:" / "'probes': x" -> probes), or "". */
std::string topLevelKey(const std::string& line)
{
    if (line.empty() || line[0] == ' ' || line[0] == '\t' || line[0] == '#' || line[0] == '-')
        return std::string();
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) return std::string();
    std::string key = line.substr(0, colon);
    while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
    if (key.size() >= 2 && (key.front() == '\'' || key.front() == '"') && key.back() == key.front())
        key = key.substr(1, key.size() - 2);
    return key;
}

bool isCommentLine(const std::string& line)
{
    const std::size_t i = line.find_first_not_of(" \t");
    return i != std::string::npos && line[i] == '#';
}

struct Block { std::string key; std::string text; };

/** @p text split into top-level blocks; what precedes the first one has an
 *  empty key.  A block starts at the comment lines directly above its key. */
std::vector<Block> splitBlocks(const std::string& text)
{
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t nl = text.find('\n', at);
        const std::size_t end = nl == std::string::npos ? text.size() : nl + 1;
        lines.push_back(text.substr(at, end - at));
        at = end;
    }
    std::vector<std::size_t> starts;    // line index where each keyed block starts
    std::vector<std::string> keys;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string l = lines[i];
        if (!l.empty() && l.back() == '\n') l.pop_back();
        const std::string key = topLevelKey(l);
        if (key.empty()) continue;
        std::size_t s = i;
        const std::size_t floor = starts.empty() ? 0 : starts.back() + 1;
        while (s > floor && isCommentLine(lines[s - 1])) --s;
        starts.push_back(s);
        keys.push_back(key);
    }
    std::vector<Block> out;
    const std::size_t first = starts.empty() ? lines.size() : starts.front();
    Block head;
    for (std::size_t i = 0; i < first; ++i) head.text += lines[i];
    out.push_back(head);
    for (std::size_t b = 0; b < starts.size(); ++b) {
        Block blk;
        blk.key = keys[b];
        const std::size_t end = b + 1 < starts.size() ? starts[b + 1] : lines.size();
        for (std::size_t i = starts[b]; i < end; ++i) blk.text += lines[i];
        out.push_back(blk);
    }
    return out;
}

} // namespace

std::string layoutAfterApply(const std::string& sessionText, const std::string& templText,
                             const std::vector<std::string>& sections)
{
    std::vector<std::string> keys;
    for (const std::string& sec : sections)
        if (sec != kTemplateKey && sec != kHeaderKey)
            for (const std::string& k : keysOf(sec)) keys.push_back(k);
    const std::vector<Block> session = splitBlocks(sessionText);
    const std::vector<Block> templ = splitBlocks(templText);
    auto fromTemplate = [&templ](const std::string& key) -> const Block* {
        for (const Block& b : templ)
            if (!b.key.empty() && b.key == key) return &b;
        return nullptr;
    };
    auto ensureNewline = [](std::string& s) { if (!s.empty() && s.back() != '\n') s += '\n'; };

    std::string out;
    std::vector<std::string> placed;
    for (const Block& b : session) {
        if (b.key.empty() || !contains(keys, b.key)) { out += b.text; continue; }
        if (const Block* t = fromTemplate(b.key)) { ensureNewline(out); out += t->text; }
        placed.push_back(b.key);
    }
    for (const std::string& k : keys)
        if (!contains(placed, k))
            if (const Block* t = fromTemplate(k)) { ensureNewline(out); out += t->text; placed.push_back(k); }
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
