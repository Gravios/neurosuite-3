/***************************************************************************
 *  yamlmodifier_test.cpp  -  regression test for ParameterYamlModifier.
 *
 *  NeuroScope rewrites the session's parameter YAML on every close, through
 *  ParameterYamlModifier.  Each setter it calls must update only what NeuroScope
 *  models and carry everything else in the file across untouched -- keys other
 *  tools write (sdiffPairs, probeId, channelMapping, ...) included.  This test
 *  drives the same setters NeuroScope's saveSession() does over a file holding
 *  such keys and checks they survive.
 *
 *  Self-contained, assert-style (own main). Built only with NS_BUILD_TESTS.
 ***************************************************************************/

#include <klustersshared/channelcolors.h>
#include <klustersshared/parameteryamlmodifier.h>
#include <klustersshared/parameteryamlreader.h>

#include <QDir>
#include <QTemporaryDir>

#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <fstream>
#include <string>

static int fails = 0;
static void chk(bool cond, const char* msg)
{
    std::printf("  %s  %s\n", cond ? "PASS" : "FAIL", msg);
    if (!cond) ++fails;
}

static std::string str(const YAML::Node& n)
{
    if (!n || !n.IsScalar()) return {};
    return n.as<std::string>();
}

// A session written by ndmanager / the stderiv pipeline: group 1 carries a custom
// difference pattern, group 2 a probe/shank, and both an unknown future key.
static const char* kSession = R"(# hand-written note
parameters:
  version: "1.0"
  creator: ndmanager
acquisitionSystem:
  nBits: 16
  nChannels: 8
  samplingRate: 20000
  voltageRange: 20
  amplification: 192.5
  offset: 0
neuroscope:
  channels:
    colors:
      - {channel: 0, color: "#ff0000", anatomyColor: "#ff0000", spikeColor: "#ff0000", label: ref}
      - {channel: 3, color: "#00ff00", anatomyColor: "#00ff00", spikeColor: "#00ff00"}
    offsets:
      - {channel: 0, defaultOffset: 10}
      - {channel: 3, defaultOffset: 50}
files:
  - samplingRate: 20000
    extension: fil
    channelMapping:
      - original: 0
        mapped: [0, 1]
  - samplingRate: 1250
    extension: lfp
  - note: hand-written entry the readers skip
anatomicalDescription:
  channelGroups:
    - label: shank A
      channels:
        - {id: 0, skip: 0, depth: 100}
        - {id: 1, skip: 1}
        - {id: 2, skip: 0}
        - {id: 3, skip: 0}
    - channels:
        - {id: 4, skip: 0}
        - {id: 5, skip: 0}
        - {id: 6, skip: 0}
        - {id: 7, skip: 0}
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3]
      nSamples: 40
      peakSampleIndex: 20
      nFeatures: 4
      sdiffPairs: "0-1,1-2,2-3,3-0"
      futureKey: kept
    - channels: [4, 5, 6, 7]
      nSamples: 32
      peakSampleIndex: 16
      nFeatures: 3
      probeId: 2
      shankIndex: 5
)";

static YAML::Node roundTrip(const QString& path,
                            QMap<int,QList<int>> spikeGroups,
                            int nbSamples = 54, int peak = 26)
{
    ParameterYamlModifier mod;
    if (!mod.parseFile(path)) { chk(false, "parseFile"); return {}; }
    mod.setSpikeDetectionInformation(nbSamples, peak, spikeGroups);
    if (!mod.writeToFile(path)) { chk(false, "writeToFile"); return {}; }
    return YAML::LoadFile(path.toStdString());
}

static void writeSession(const QString& path)
{
    std::ofstream(path.toStdString()) << kSession;
}

int main()
{
    QTemporaryDir dir;
    if (!dir.isValid()) { std::printf("FAIL  no temp dir\n"); return 1; }
    const QString path = dir.filePath(QStringLiteral("s.yaml"));

    std::printf("spike groups unchanged (a plain NeuroScope open + close):\n");
    writeSession(path);
    {
        QMap<int,QList<int>> g;
        g[-1] = {};
        g[1] = {0, 1, 2, 3};
        g[2] = {4, 5, 6, 7};
        const YAML::Node r = roundTrip(path, g);
        const YAML::Node grps = r["spikeDetection"]["channelGroups"];
        chk(grps.IsSequence() && grps.size() == 2, "two groups written");
        chk(str(grps[0]["sdiffPairs"]) == "0-1,1-2,2-3,3-0", "group 1 sdiffPairs kept");
        chk(str(grps[0]["futureKey"]) == "kept", "group 1 unknown key kept");
        chk(str(grps[0]["nSamples"]) == "40", "group 1 nSamples not replaced by session value");
        chk(str(grps[0]["nFeatures"]) == "4", "group 1 nFeatures kept");
        chk(str(grps[1]["probeId"]) == "2", "group 2 probeId kept");
        chk(str(grps[1]["shankIndex"]) == "5", "group 2 shankIndex kept");
        chk(str(grps[1]["peakSampleIndex"]) == "16", "group 2 peakSampleIndex kept");
        chk(!grps[1]["sdiffPairs"], "no sdiffPairs invented on group 2");

        // A second close must be a fixed point.
        const YAML::Node r2 = roundTrip(path, g);
        chk(str(r2["spikeDetection"]["channelGroups"][0]["sdiffPairs"]) == "0-1,1-2,2-3,3-0",
            "sdiffPairs survives a second save");
    }

    std::printf("groups renumbered (group 1 deleted in NeuroScope):\n");
    writeSession(path);
    {
        QMap<int,QList<int>> g;
        g[1] = {4, 5, 6, 7};                 // old group 2 is now group 1
        const YAML::Node grps = roundTrip(path, g)["spikeDetection"]["channelGroups"];
        chk(grps.size() == 1, "one group written");
        chk(str(grps[0]["probeId"]) == "2", "keys follow the channels, not the position");
        chk(!grps[0]["sdiffPairs"], "old group 1's sdiffPairs not grafted onto it");
    }

    std::printf("channel added to a group:\n");
    writeSession(path);
    {
        QMap<int,QList<int>> g;
        g[1] = {0, 1, 2, 3};
        g[2] = {4, 5, 6};                    // 7 moved out
        g[3] = {7};                          // new group
        const YAML::Node grps = roundTrip(path, g)["spikeDetection"]["channelGroups"];
        chk(grps.size() == 3, "three groups written");
        chk(grps[1]["channels"].size() == 3, "group 2 channels updated");
        chk(str(grps[1]["probeId"]) == "2", "edited group keeps its keys by position");
        chk(str(grps[2]["nSamples"]) == "54" && str(grps[2]["nFeatures"]) == "3",
            "new group gets the session geometry");
    }

    std::printf("anatomical groups, channel 7 moved to the display trash:\n");
    writeSession(path);
    {
        QMap<int,QList<int>> g;
        g[0] = {7};                          // NeuroScope's display trash
        g[1] = {0, 1, 2, 3};
        g[2] = {4, 5, 6};
        QMap<int,bool> skip;
        skip[1] = true;
        skip[2] = true;                      // newly skipped
        ParameterYamlModifier mod;
        chk(mod.parseFile(path), "parseFile");
        mod.setAnatomicalDescription(g, skip);
        chk(mod.writeToFile(path), "writeToFile");
        const YAML::Node grps = YAML::LoadFile(path.toStdString())
                                    ["anatomicalDescription"]["channelGroups"];
        chk(grps.size() == 2, "trash not written as a group");
        chk(grps[0]["channels"].size() == 4, "group 1 is still the first group");
        chk(str(grps[0]["label"]) == "shank A", "group key kept");
        chk(str(grps[0]["channels"][0]["depth"]) == "100", "channel key kept");
        chk(str(grps[0]["channels"][2]["skip"]) == "1", "skip flag updated");
        chk(grps[1]["channels"].size() == 3, "group 2 channels updated");

        ParameterYamlReader rd;
        chk(rd.parseFile(path), "reader parses");
        QMap<int,int> chGrp; QMap<int,QList<int>> grpCh; QMap<int,bool> sk;
        rd.getAnatomicalDescription(8, chGrp, grpCh, sk);
        chk(grpCh.value(0) == QList<int>{7} && chGrp.value(7, -1) == 0,
            "reader rebuilds the trash from the unlisted channel");
        chk(grpCh.value(1).size() == 4 && grpCh.value(2).size() == 3,
            "groups 1 and 2 read back in place");
    }

    std::printf("files: rate changed, extension added:\n");
    writeSession(path);
    {
        QMap<QString,double> rates;
        rates[QStringLiteral("fil")] = 20000;
        rates[QStringLiteral("lfp")] = 1000;  // changed
        rates[QStringLiteral("whl")] = 39.06; // new
        ParameterYamlModifier mod;
        chk(mod.parseFile(path), "parseFile");
        mod.setSampleRateByExtension(rates);
        chk(mod.writeToFile(path), "writeToFile");
        const YAML::Node f = YAML::LoadFile(path.toStdString())["files"];
        chk(f.size() == 4, "all entries kept, one added");
        chk(f[0]["channelMapping"] && f[0]["channelMapping"].size() == 1,
            "channelMapping kept");
        chk(str(f[1]["samplingRate"]) == "1000", "lfp rate updated");
        chk(str(f[2]["note"]) == "hand-written entry the readers skip",
            "entry the caller never saw kept");
        chk(str(f[3]["extension"]) == "whl", "new extension appended");
    }

    std::printf("channel display: caller knows channels 0-2 only:\n");
    writeSession(path);
    {
        ChannelColors colors;
        for (int ch = 0; ch < 3; ++ch) colors.append(ch, QColor(Qt::blue));
        QMap<int,int> chGroups;
        QMap<int,int> offsets;
        offsets[0] = 20;
        ParameterYamlModifier mod;
        chk(mod.parseFile(path), "parseFile");
        chk(mod.setChannelDisplayInformation(&colors, chGroups, offsets), "set");
        chk(mod.writeToFile(path), "writeToFile");
        const YAML::Node c = YAML::LoadFile(path.toStdString())["neuroscope"]["channels"];
        chk(c["colors"].size() == 8 && c["offsets"].size() == 8, "one entry per channel");
        chk(str(c["colors"][0]["color"]) == "#0000ff", "known colour updated");
        chk(str(c["colors"][0]["label"]) == "ref", "colour entry key kept");
        chk(str(c["colors"][3]["color"]) == "#00ff00", "unknown channel keeps the file colour");
        chk(str(c["colors"][5]["color"]) == "#0080ff", "absent channel gets the default");
        chk(str(c["offsets"][0]["defaultOffset"]) == "20", "known offset updated");
        chk(str(c["offsets"][3]["defaultOffset"]) == "50", "unknown channel keeps the file offset");
        chk(str(c["offsets"][5]["defaultOffset"]) == "0", "absent offset defaults to 0");
    }

    std::printf("acquisition: fractional amplification, zero offset:\n");
    writeSession(path);
    {
        ParameterYamlReader rd;
        chk(rd.parseFile(path), "reader parses");
        chk(rd.getAmplification() == 193, "fractional amplification read, not 0");
        chk(rd.hasOffset() && rd.getOffset() == 0, "offset 0 reads as present");
        rd.closeFile();

        // What NeuroScope passes back after loading those values.
        ParameterYamlModifier mod;
        chk(mod.parseFile(path), "parseFile");
        mod.setAcquisitionSystemInformation(16, 8, 20000.0, 20, 193, 0);
        chk(mod.writeToFile(path), "writeToFile");
        const YAML::Node a = YAML::LoadFile(path.toStdString())["acquisitionSystem"];
        chk(str(a["amplification"]) == "192.5", "file's amplification left as written");
        chk(str(a["samplingRate"]) == "20000", "unchanged samplingRate left as written");

        ParameterYamlModifier mod2;
        chk(mod2.parseFile(path), "parseFile");
        mod2.setAcquisitionSystemInformation(16, 8, 20000.0, 20, 400, 0);
        chk(mod2.writeToFile(path), "writeToFile");
        chk(str(YAML::LoadFile(path.toStdString())["acquisitionSystem"]["amplification"]) == "400",
            "a real change is still written");
    }

    std::printf("no-op save leaves the file untouched:\n");
    writeSession(path);
    {
        ParameterYamlModifier mod;
        chk(mod.parseFile(path), "parseFile");
        mod.setAcquisitionSystemInformation(16, 8, 20000.0, 20, 193, 0);  // same values
        mod.setLfpInformation(1250.0);                                      // new key
        chk(mod.writeToFile(path), "writeToFile");
        std::ifstream in(path.toStdString());
        std::string first;
        std::getline(in, first);
        chk(first != "# hand-written note", "a real change rewrites the file");

        writeSession(path);
        ParameterYamlModifier mod2;
        chk(mod2.parseFile(path), "parseFile");
        mod2.setAcquisitionSystemInformation(16, 8, 20000.0, 20, 193, 0);
        chk(mod2.writeToFile(path), "writeToFile");
        std::ifstream in2(path.toStdString());
        std::getline(in2, first);
        chk(first == "# hand-written note", "unchanged tree: comment and layout kept");
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
