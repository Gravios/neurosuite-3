// Session templates (sessiontemplate.h): which sections a session has, a
// template made from chosen ones, and a template applied to another session.
#include "sessiontemplate.h"
#include "yamlformat.h"

#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <string>
#include <vector>

static int fails = 0;
static void chk(bool c, const char* m)
{
    std::printf("  %s  %s\n", c ? "PASS" : "FAIL", m);
    if (!c) ++fails;
}
static std::string str(const YAML::Node& n) { return n && n.IsScalar() ? n.Scalar() : std::string(); }
using Keys = std::vector<std::string>;

static const char* kSession = R"(parameters:
  version: '1.0'
  creator: ndm_plx2dat
sourceRecording:
  header: {version: 3}
generalInfo:
  date: 2026-09-01
  experimenters: jg
acquisitionSystem:
  nChannels: 64        # 4 shanks x 16
  samplingRate: 20000
probes:
  - {probeId: 0, probeFile: neuronexus/Buzsaki64.probe}
probeLibraryPath: /lab/probes
anatomicalDescription:
  channelGroups:
    - channels: [{id: 0}, {id: 1}]
spikeDetection:
  channelGroups:
    - channels: [0, 1]
      nSamples: 40
      klustakwik: {maxClusters: 30}
units:
  - {group: 1, cluster: 2, structure: CA1}
labNotebook:
  page: 12
programs:
  - name: ndm_hipass
    parameters:
      - {name: cutoff, value: 500, status: Optional}
)";

static const char* kOther = R"(parameters:
  version: '1.0'
generalInfo:
  date: 2026-10-01
acquisitionSystem:
  nChannels: 32
  samplingRate: 30000
anatomicalDescription:
  channelGroups:
    - channels: [{id: 5}]
probes:
  - {probeId: 0, probeFile: cambridge/H2.probe}
probeLibraryPath: /other
units:
  - {group: 4, cluster: 9}
programs:
  - name: ndm_lfp
)";

int main()
{
    const YAML::Node session = YAML::Load(kSession);
    const YAML::Node other = YAML::Load(kOther);

    std::printf("sections:\n");
    const Keys secs = sessiontemplate::sectionsOf(session);
    chk(secs == (Keys{"generalInfo", "sourceRecording", "acquisitionSystem", "probes",
                      "anatomicalDescription", "spikeDetection", "units", "programs", "labNotebook"}),
        "known sections in display order, then unknown ones; probeLibraryPath folded into probes; no header");
    chk(!sessiontemplate::describe("units").exportDefault && !sessiontemplate::describe("generalInfo").exportDefault,
        "session-specific sections are not exported by default");
    chk(sessiontemplate::describe("programs").applyDefault && !sessiontemplate::describe("acquisitionSystem").applyDefault,
        "processing is applied by default, the recording's setup is not");
    const sessiontemplate::SectionInfo unknown = sessiontemplate::describe("labNotebook");
    chk(unknown.label == "LabNotebook" && unknown.exportDefault && !unknown.applyDefault, "an unknown block: exported, not applied, by default");
    chk(sessiontemplate::channelCount(session) == 64 && sessiontemplate::channelCount(YAML::Load("a: 1")) == -1, "channel count");

    std::printf("export:\n");
    sessiontemplate::Meta meta;
    meta.name = "CA1 4-shank";
    meta.description = "Buzsaki64";
    meta.createdFrom = "rat01.yaml";
    meta.created = "2026-10-16";
    Keys chosen;
    for (const std::string& s : secs)
        if (sessiontemplate::describe(s).exportDefault) chosen.push_back(s);
    chosen.push_back("notThere");
    const YAML::Node t = sessiontemplate::makeTemplate(session, chosen, meta);
    chk(sessiontemplate::isTemplate(t) && !sessiontemplate::isTemplate(session), "the result is marked as a template");
    const sessiontemplate::Meta back = sessiontemplate::metaOf(t);
    chk(back.name == "CA1 4-shank" && back.description == "Buzsaki64" && back.createdFrom == "rat01.yaml"
        && back.created == "2026-10-16", "metadata read back");
    chk(back.sections == (Keys{"acquisitionSystem", "probes", "anatomicalDescription", "spikeDetection",
                               "programs", "labNotebook"}),
        "sections lists what it holds, not what was asked for");
    chk(!t["generalInfo"] && !t["units"] && !t["sourceRecording"], "unchosen sections left out");
    chk(str(t["probeLibraryPath"]) == "/lab/probes", "probes bring the library path");
    chk(str(t["spikeDetection"]["channelGroups"][0]["klustakwik"]["maxClusters"]) == "30", "nested keys kept");
    chk(str(t["parameters"]["creator"]) == "ndm_plx2dat", "the file header is kept");
    chk(sessiontemplate::sectionsOf(t) == back.sections, "the template's sections are exactly its listed ones");
    chk(str(session["generalInfo"]["experimenters"]) == "jg" && !session["template"], "the session is untouched");

    // Written with the session's layout: its comments survive.
    const std::string text = yamlformat::emitWithLayout(t, kSession);
    chk(text.find("# 4 shanks x 16") != std::string::npos, "the session's comments survive the export");
    const YAML::Node reread = YAML::Load(text);
    chk(sessiontemplate::metaOf(reread).sections == back.sections, "the written template reads back");

    std::printf("apply:\n");
    Keys skipped;
    const YAML::Node merged = sessiontemplate::apply(other, t,
        {"anatomicalDescription", "programs", "spikeDetection", "units", "template", "parameters"}, &skipped);
    chk(str(merged["anatomicalDescription"]["channelGroups"][0]["channels"][1]["id"]) == "1",
        "a chosen section is replaced by the template's");
    chk(str(merged["programs"][0]["name"]) == "ndm_hipass" && merged["programs"].size() == 1, "programs replaced, not merged");
    chk(merged["spikeDetection"] && str(merged["spikeDetection"]["channelGroups"][0]["nSamples"]) == "40",
        "a section the session lacked is added");
    chk(skipped == (Keys{"units"}), "a section the template does not hold is reported and left alone");
    chk(str(merged["units"][0]["cluster"]) == "9", "the session's units kept");
    chk(str(merged["acquisitionSystem"]["nChannels"]) == "32" && str(merged["generalInfo"]["date"]) == "2026-10-01",
        "unchosen sections kept");
    chk(!merged["template"] && !merged["parameters"]["creator"], "neither the template block nor the header is copied");
    chk(str(other["programs"][0]["name"]) == "ndm_lfp", "the session given is untouched");

    const YAML::Node p = sessiontemplate::apply(other, YAML::Load("template: {name: x}\nprobes: []\n"), {"probes"});
    chk(p["probes"] && p["probes"].size() == 0 && !p["probeLibraryPath"],
        "probes from a template without a library path clear the session's path");
    const YAML::Node same = sessiontemplate::apply(other, t, {});
    chk(YAML::Dump(same) == YAML::Dump(other), "applying nothing changes nothing");

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
