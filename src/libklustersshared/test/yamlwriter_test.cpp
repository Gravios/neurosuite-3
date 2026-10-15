/***************************************************************************
 *  yamlwriter_test.cpp  -  regression test for ParameterYamlWriter.
 *
 *  ndmanager saves through ParameterYamlWriter.  A save must write what
 *  ndmanager models and carry everything else the session file holds --
 *  per-group keys (probeId, shankIndex, sitePositions_um, klustakwik:, ...),
 *  acquisition keys outside ndmanager's list (bitResolution_uV, duration_s,
 *  written by ndm_plx2dat) and whole blocks it has no page for
 *  (sourceRecording).  This test loads a session the way ndmanagerdoc.cpp
 *  does, saves it back with setBaseDocument(), and checks what survives.
 *
 *  Self-contained, assert-style (own main). Built only with NS_BUILD_TESTS.
 ***************************************************************************/

#include <klustersshared/parameteryamlreader.h>
#include <klustersshared/parameteryamlreader_probes.h>
#include <klustersshared/parameteryamlwriter.h>

#include <QTemporaryDir>

#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <fstream>
#include <string>

static int fails = 0;
static void report(bool cond, const char* msg)
{
    std::printf("  %s  %s\n", cond ? "PASS" : "FAIL", msg);
    if (!cond) ++fails;
}
// A lookup through a key the document lacks throws (yaml-cpp InvalidNode);
// that is a failed check, not a crashed test.
#define chk(cond, msg) do { bool ok_ = false;                       \
        try { ok_ = static_cast<bool>(cond); } catch (...) {}         \
        report(ok_, msg); } while (0)

static std::string str(const YAML::Node& n)
{
    if (!n || !n.IsScalar()) return {};
    return n.as<std::string>();
}

// A session as ndm_plx2dat + ndm_setupgroups + the stderiv pipeline leave it.
static const char* kSession = R"(parameters:
  version: "1.0"
  creator: ndm_plx2dat
generalInfo:
  date: "2026-01-02"
  experimenters: someone
  description: ""
  notes: ""
  rig: B
acquisitionSystem:
  nBits: 16
  nChannels: 8
  samplingRate: 20000
  voltageRange: 20
  amplification: 192.5
  offset: 0
  bitResolution_uV: 0.195
  nSamples: 123456
  duration_s: 6.1728
fieldPotentials:
  lfpSamplingRate: 1250
files:
  - samplingRate: 1000
    extension: whl
    source: camera
sourceRecording:
  header:
    version: 3
  findings: [gap at 12 s]
anatomicalDescription:
  channelGroups:
    - channels:
        - {id: 0, skip: 0, depth: 100}
        - {id: 1, skip: 0}
        - {id: 2, skip: 0}
        - {id: 3, skip: 0}
      probeId: 0
      shankIndex: 0
    - channels:
        - {id: 4, skip: 0}
        - {id: 5, skip: 0}
        - {id: 6, skip: 0}
        - {id: 7, skip: 1}
      probeId: 0
      shankIndex: 1
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3]
      nSamples: 40
      peakSampleIndex: 20
      nFeatures: 4
      sdiffPairs: "0-1,1-2,2-3"
      probeId: 0
      shankIndex: 0
      sitePositions_um: [[0, 0], [0, 20], [0, 40], [0, 60]]
      klustakwik:
        maxClusters: 30
    - channels: [4, 5, 6, 7]
      nSamples: 32
      peakSampleIndex: 16
      nFeatures: 3
      probeId: 0
      shankIndex: 1
      nCells: 7
units:
  - group: 1
    cluster: 2
    structure: CA1
    type: pyr
    isolationDistance: 20
    quality: good
    notes: ~
    curator: jg
neuroscope:
  miscellaneous:
    screenGain: 0.2
    traceBackgroundImage: ~
  channels:
    colors:
      - {channel: 0, color: "#ff0000", anatomyColor: "#ff0000", spikeColor: "#ff0000", label: ref}
    offsets:
      - {channel: 0, defaultOffset: 10}
programs:
  - name: ndm_hipass
    parameters:
      - {name: cutoff, value: 300, status: dynamic, unit: Hz}
    help: high-pass
    origin: template
probes:
  - id: 0
    probeFile: neuronexus/A2x4.probe
    label: left
    channelOffset: 0
    anatomicalGroups: [1, 2]
    serial: X123
)";

// What ndmanagerdoc.cpp's loadFromReader() holds after opening a file.
struct Model {
    QMap<int,QList<int>> anat, spike;
    QMap<QString,QMap<int,QString>> attrs;
    QMap<int,QMap<QString,QString>> info;
    QMap<int,QStringList> units;
    GeneralInformation gi;
    QMap<QString,double> acq, video;
    QList<FileInformation> files;
    QList<ChannelColorEntry> colors;
    QMap<int,int> offsets;
    NeuroscopeVideoInfo nvi;
    QList<ProgramInformation> programs;
    QList<ProbeEntry> probes;
    QString libraryPath;
    double lfp = 0;
    float gain = 0;
    int nbSamples = 0, peak = 0;
    QString traceImage;
};

static Model load(const QString& path)
{
    Model m;
    ParameterYamlReader r;
    if (!r.parseFile(path)) { chk(false, "reader parses"); return m; }
    r.getGeneralInformation(m.gi);
    m.acq[QStringLiteral("nBits")]         = r.getResolution();
    m.acq[QStringLiteral("nChannels")]     = r.getNbChannels();
    m.acq[QStringLiteral("samplingRate")]  = r.getSamplingRate();
    m.acq[QStringLiteral("voltageRange")]  = r.getVoltageRange();
    m.acq[QStringLiteral("amplification")] = r.getAmplification();
    m.acq[QStringLiteral("offset")]        = r.getOffset();
    r.getTopLevelVideoInfo(m.video);
    m.lfp = r.getLfpSamplingRate();
    r.getFilesInformation(m.files);
    r.getAnatomicalDescription(8, m.anat, m.attrs);
    r.getSpikeDescription(8, m.spike, m.info);
    r.getUnits(m.units);
    m.gain = r.getScreenGain();
    m.traceImage = r.getTraceBackgroundImage();
    m.nbSamples = r.getNbSamplesSpikes();
    m.peak = r.getPeakSampleIndexSpikes();
    r.getChannelColors(m.colors);
    r.getNeuroscopeVideoInfo(m.nvi);
    r.getChannelDefaultOffset(m.offsets);
    r.getProgramsInformation(m.programs);
    readProbesSection(r.getRawRoot(), m.probes, m.libraryPath);
    return m;
}

// The same calls, in the same order, as ndmanagerdoc.cpp's save.
static YAML::Node save(Model& m, const QString& base, const QString& target,
                       bool* baseUsed = nullptr)
{
    ParameterYamlWriter w;
    const bool used = w.setBaseDocument(base);
    if (baseUsed) *baseUsed = used;
    w.setGeneralInformation(m.gi);
    w.setAcquisitionSystemInformation(m.acq);
    if (!m.video.isEmpty()) w.setVideoInformation(m.video);
    w.setLfpInformation(m.lfp);
    w.setFilesInformation(m.files);
    w.setAnatomicalDescription(m.anat, m.attrs);
    w.setSpikeDetectionInformation(m.spike, m.info);
    w.setUnitsInformation(m.units);
    w.setMiscellaneousInformation(m.gain, m.traceImage);
    w.setNeuroscopeVideoInformation(m.nvi);
    w.setNeuroscopeSpikeInformation(m.nbSamples, m.peak);
    w.setChannelDisplayInformation(m.colors, m.offsets);
    w.setProgramsInformation(m.programs);
    w.setProbesInformation(m.probes, m.libraryPath);
    if (!w.writeTofile(target)) { chk(false, "writeTofile"); return {}; }
    return YAML::LoadFile(target.toStdString());
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
    const QString other = dir.filePath(QStringLiteral("copy.yaml"));

    std::printf("open + save, nothing edited:\n");
    writeSession(path);
    {
        Model m = load(path);
        bool used = false;
        const YAML::Node d = save(m, path, path, &used);
        chk(used, "the file being saved is the base document");
        chk(str(d["sourceRecording"]["header"]["version"]) == "3", "sourceRecording kept");
        chk(str(d["parameters"]["creator"]) == "ndm_plx2dat", "parameters header kept");
        chk(str(d["generalInfo"]["rig"]) == "B", "generalInfo key kept");
        const YAML::Node a = d["acquisitionSystem"];
        chk(str(a["bitResolution_uV"]) == "0.195" && str(a["nSamples"]) == "123456" &&
            str(a["duration_s"]) == "6.1728", "acquisition keys outside the model kept");
        chk(str(a["amplification"]) == "192.5", "fractional amplification left as written");
        const YAML::Node sg = d["spikeDetection"]["channelGroups"];
        chk(sg.size() == 2, "two spike groups");
        chk(str(sg[0]["sdiffPairs"]) == "0-1,1-2,2-3", "sdiffPairs kept");
        chk(str(sg[0]["klustakwik"]["maxClusters"]) == "30", "klustakwik block kept");
        chk(sg[0]["sitePositions_um"].size() == 4, "sitePositions_um kept");
        chk(str(sg[1]["probeId"]) == "0" && str(sg[1]["shankIndex"]) == "1", "probeId/shankIndex kept");
        chk(str(sg[1]["nCells"]) == "7", "unknown group key kept");
        chk(str(sg[0]["nSamples"]) == "40" && str(sg[0]["nFeatures"]) == "4", "modelled values written");
        const YAML::Node ag = d["anatomicalDescription"]["channelGroups"];
        chk(ag.size() == 2 && str(ag[1]["shankIndex"]) == "1", "anatomical group keys kept");
        chk(str(ag[0]["channels"][0]["depth"]) == "100", "anatomical channel key kept");
        chk(str(ag[1]["channels"][3]["skip"]) == "1", "skip flag written");
        chk(str(d["files"][0]["source"]) == "camera", "file entry key kept");
        chk(str(d["units"][0]["curator"]) == "jg", "unit key kept");
        chk(str(d["neuroscope"]["channels"]["colors"][0]["label"]) == "ref", "colour entry key kept");
        chk(str(d["programs"][0]["origin"]) == "template", "program key kept");
        chk(str(d["programs"][0]["parameters"][0]["unit"]) == "Hz", "program parameter key kept");
        chk(str(d["probes"][0]["serial"]) == "X123", "probe entry key kept");

        // A second save is a fixed point.
        Model m2 = load(path);
        const YAML::Node d2 = save(m2, path, path);
        chk(YAML::Dump(d2) == YAML::Dump(d), "second save writes the same document");
    }

    std::printf("Save As: base is the opened file, target is new:\n");
    writeSession(path);
    {
        Model m = load(path);
        const YAML::Node d = save(m, path, other);
        chk(str(d["sourceRecording"]["findings"][0]) == "gap at 12 s", "sourceRecording carried to the new file");
        chk(str(d["spikeDetection"]["channelGroups"][1]["nCells"]) == "7", "group keys carried");
    }

    std::printf("spike group 1 deleted in ndmanager (rows renumber):\n");
    writeSession(path);
    {
        Model m = load(path);
        QMap<int,QList<int>> spike;  QMap<int,QMap<QString,QString>> info;
        spike[1] = m.spike[2];       info[1] = m.info[2];
        m.spike = spike;             m.info = info;
        QMap<int,QList<int>> anat;   anat[1] = m.anat[2];   m.anat = anat;
        const YAML::Node d = save(m, path, path);
        const YAML::Node sg = d["spikeDetection"]["channelGroups"];
        chk(sg.size() == 1, "one spike group written");
        chk(str(sg[0]["nCells"]) == "7" && str(sg[0]["shankIndex"]) == "1",
            "remaining group keeps its own keys");
        chk(!sg[0]["sdiffPairs"] && !sg[0]["klustakwik"], "deleted group's keys not grafted on");
        const YAML::Node ag = d["anatomicalDescription"]["channelGroups"];
        chk(ag.size() == 1 && str(ag[0]["shankIndex"]) == "1", "anatomical group follows its channels");
    }

    std::printf("group added, pattern cleared, empty cells:\n");
    writeSession(path);
    {
        Model m = load(path);
        m.spike[2] = {4, 5, 6};
        m.spike[3] = {7};
        m.info[3][QStringLiteral("nSamples")] = QStringLiteral("32");
        m.info[1].remove(QStringLiteral("sdiffPairs"));          // cleared cell
        m.info[2].remove(QStringLiteral("nSamples"));            // emptied cell
        const YAML::Node sg = save(m, path, path)["spikeDetection"]["channelGroups"];
        chk(sg.size() == 3, "three spike groups");
        chk(!sg[0]["sdiffPairs"], "cleared pattern removed");
        chk(str(sg[0]["klustakwik"]["maxClusters"]) == "30", "other keys of that group kept");
        chk(str(sg[1]["nCells"]) == "7" && sg[1]["channels"].size() == 3,
            "edited group keeps its keys by position");
        chk(str(sg[1]["nSamples"]) == "32", "emptied cell keeps the file's value, not 0");
        chk(!sg[2]["nCells"] && str(sg[2]["nSamples"]) == "32" && str(sg[2]["nFeatures"]) == "0",
            "new group gets only modelled values");
    }

    std::printf("lists emptied in ndmanager:\n");
    writeSession(path);
    {
        Model m = load(path);
        m.files.clear();
        m.programs.clear();
        m.probes.clear();
        const YAML::Node d = save(m, path, path);
        chk(!d["files"], "files removed");
        chk(!d["programs"], "programs removed");
        chk(!d["probes"], "probes removed");
        chk(d["sourceRecording"].IsMap(), "unmodelled block still kept");
    }

    std::printf("per-group ints written as floats, or not numbers:\n");
    {
        std::string text = kSession;
        auto swap = [&text](const std::string& from, const std::string& to) {
            text.replace(text.find(from), from.size(), to);
        };
        swap("nSamples: 40", "nSamples: 40.0");
        swap("peakSampleIndex: 16", "peakSampleIndex: sixteen");
        std::ofstream(path.toStdString()) << text;
        Model m = load(path);
        const YAML::Node sg = save(m, path, path)["spikeDetection"]["channelGroups"];
        chk(str(sg[0]["nSamples"]) == "40", "nSamples 40.0 saved as 40, not 0");
        chk(str(sg[1]["peakSampleIndex"]) == "sixteen", "a non-number is left for the user, not zeroed");
    }

    std::printf("brand-new file (no base document):\n");
    {
        writeSession(path);
        Model m = load(path);
        const QString fresh = dir.filePath(QStringLiteral("fresh.yaml"));
        bool used = true;
        const YAML::Node d = save(m, QString(), fresh, &used);
        chk(!used, "no base document");
        chk(str(d["parameters"]["creator"]) == "ndManager-yaml", "fresh header");
        chk(!d["sourceRecording"] && !d["acquisitionSystem"]["bitResolution_uV"],
            "only modelled values written");
        chk(d["spikeDetection"]["channelGroups"].size() == 2, "groups written");
        chk(str(d["acquisitionSystem"]["amplification"]) == "193", "model value written");

        const QString notYaml = dir.filePath(QStringLiteral("x.xml"));
        std::ofstream(notYaml.toStdString()) << "<?xml version=\"1.0\"?>\n<parameters/>\n";
        ParameterYamlWriter w;
        chk(!w.setBaseDocument(notYaml), "a non-YAML-map base is refused");
        chk(!w.setBaseDocument(dir.filePath(QStringLiteral("missing.yaml"))),
            "a missing base is refused");
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
