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

#include <klustersshared/parameteryamlmodifier.h>

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
static const char* kSession = R"(parameters:
  version: "1.0"
  creator: ndmanager
acquisitionSystem:
  nBits: 16
  nChannels: 8
  samplingRate: 20000
  voltageRange: 20
  amplification: 1000
  offset: 0
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

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
