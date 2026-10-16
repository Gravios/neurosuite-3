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
#include <sstream>
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

    std::printf("skip flags through ndmanager's attribute map:\n");
    writeSession(path);
    {
        // ndmanager's Anatomical Groups page shows the flag in a column
        // labelled "Skip" and hands the writer the map it rebuilds from that
        // table.  The page keys the map by ndmanager::SKIP, the reader's key,
        // so a flag read from the file is the flag written back.
        const QString kSkip = QStringLiteral("skip");          // ndmanager::SKIP
        Model m = load(path);
        chk(m.attrs.contains(kSkip) && m.attrs.value(kSkip).value(7) == QStringLiteral("1"),
            "reader keys the flag \"skip\"");
        QMap<QString,QMap<int,QString>> fromPage;               // AnatomyPage::getAttributes
        for (int ch = 0; ch < 8; ++ch)
            fromPage[kSkip].insert(ch, m.attrs.value(kSkip).value(ch));
        m.attrs = fromPage;
        const YAML::Node ag = save(m, path, path)["anatomicalDescription"]["channelGroups"];
        chk(str(ag[1]["channels"][3]["skip"]) == "1", "skip: 1 stays 1 after a save");
        chk(str(ag[1]["channels"][2]["skip"]) == "0", "skip: 0 stays 0");
        Model again = load(path);
        chk(again.attrs.value(kSkip).value(7) == QStringLiteral("1"), "and reads back as 1");

        // The defect: a map keyed by the column label is not read by the
        // writer, and every flag is written 0.
        Model label = load(path);
        label.attrs.insert(QStringLiteral("Skip"), label.attrs.take(kSkip));
        const YAML::Node lg = save(label, path, other)["anatomicalDescription"]["channelGroups"];
        chk(str(lg[1]["channels"][3]["skip"]) == "0", "(a \"Skip\"-keyed map loses the flag)");
    }

    std::printf("NeuroScope spike fields left blank:\n");
    {
        std::string text = kSession;
        const std::string from = "neuroscope:\n";
        text.replace(text.find(from), from.size(),
                     "neuroscope:\n  spikes:\n    nSamples: 54\n    peakSampleIndex: 27\n");
        std::ofstream(path.toStdString()) << text;
        Model m = load(path);
        chk(m.nbSamples == 54 && m.peak == 27, "read 54/27");
        m.nbSamples = 0; m.peak = 0;                            // both fields blanked
        YAML::Node spk = save(m, path, path)["neuroscope"]["spikes"];
        chk(str(spk["nSamples"]) == "54" && str(spk["peakSampleIndex"]) == "27", "blank fields keep the file's values, not 0");
        Model e = load(path);
        e.nbSamples = 64; e.peak = 0;
        spk = save(e, path, path)["neuroscope"]["spikes"];
        chk(str(spk["nSamples"]) == "64" && str(spk["peakSampleIndex"]) == "27", "a set field is written, a blank one kept");

        writeSession(path);                                     // no neuroscope/spikes in the file
        Model f = load(path);
        const QString fresh = dir.filePath(QStringLiteral("fresh-spikes.yaml"));
        spk = save(f, QString(), fresh)["neuroscope"]["spikes"];
        chk(str(spk["nSamples"]) == "40" && str(spk["peakSampleIndex"]) == "20",
            "unset in a new file: derived from the spike groups, not 0");
    }

    std::printf("comments, inline lists and quoting survive a save:\n");
    {
        // The session as a person wrote it: a header note, an end-of-line
        // comment, a comment above a group.
        std::string text = std::string("# rat 12, day 3\n") + kSession;
        const std::string from = "  nChannels: 8\n", to = "  nChannels: 8          # two tetrodes\n";
        text.replace(text.find(from), from.size(), to);
        const std::string grp = "    - channels: [4, 5, 6, 7]\n";
        text.insert(text.find(grp), "    # second tetrode\n");
        std::ofstream(path.toStdString()) << text;

        Model m = load(path);
        save(m, path, path);
        std::ifstream in(path.toStdString());
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string out = ss.str();
        chk(out.rfind("# rat 12, day 3\n", 0) == 0, "header comment kept");
        chk(out.find("  nChannels: 8          # two tetrodes\n") != std::string::npos, "end-of-line comment kept");
        chk(out.find("    # second tetrode\n    - channels: [4, 5, 6, 7]\n") != std::string::npos,
            "group comment and inline channel list kept");
        chk(out.find("  version: \"1.0\"\n") != std::string::npos, "quoted version stays quoted");

        Model m2 = load(path);
        save(m2, path, path);
        std::ifstream in2(path.toStdString());
        std::stringstream ss2;
        ss2 << in2.rdbuf();
        chk(ss2.str() == out, "second save leaves the text unchanged");
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

    std::printf("a spike group with null channels keeps its place:\n");
    {
        std::string text = kSession;
        const std::string from = "    - channels: [0, 1, 2, 3]\n";
        text.replace(text.find(from), from.size(), "    - channels: ~\n");
        std::ofstream(path.toStdString()) << text;
        Model m = load(path);
        chk(m.spike.size() == 2 && m.spike.value(1).isEmpty(), "read as an empty group 1");
        const YAML::Node sg = save(m, path, path)["spikeDetection"]["channelGroups"];
        chk(sg.size() == 2, "both groups written");
        chk(sg[0]["channels"].IsSequence() && sg[0]["channels"].size() == 0 &&
            str(sg[0]["sdiffPairs"]) == "0-1,1-2,2-3", "group 1 written empty, keys kept");
        chk(str(sg[1]["nCells"]) == "7", "group 2 is still group 2");
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

    // Units: region, division, atlas and regionId read and written; a unit
    // without them gains no keys.
    {
        std::printf("units: region, division, atlas, regionId\n");
        const QString up = dir.filePath(QStringLiteral("units.yaml"));
        std::ofstream(up.toStdString()) <<
            "units:\n"
            "  - {group: 1, cluster: 2, structure: CA1, type: pyr, isolationDistance: 20, quality: good, notes: ~}\n"
            "  - {group: 1, cluster: 3, structure: CA3, region: hippocampal formation, division: archicortex,"
            " atlas: WHS SD rat v4, regionId: 'UBERON:0003881'}\n";
        ParameterYamlReader r;
        report(r.parseFile(up), "units file parsed");
        QMap<int,QStringList> units;
        r.getUnits(units);
        report(units.size() == 2 && units[0].size() == 11 && units[1].size() == 11, "eleven columns per unit");
        report(units[0][7].isEmpty() && units[1][7] == "hippocampal formation" && units[1][8] == "archicortex"
               && units[1][9] == "WHS SD rat v4" && units[1][10] == "UBERON:0003881", "new fields read");
        units[0][8] = QStringLiteral("cerebral cortex");                        // set one on unit 2
        units[1][9] = QString();                                               // clear one on unit 3
        ParameterYamlWriter w;
        w.setBaseDocument(up);
        w.setUnitsInformation(units);
        report(w.writeTofile(up), "units written");
        const YAML::Node d = YAML::LoadFile(up.toStdString());
        report(str(d["units"][0]["division"]) == "cerebral cortex" && !d["units"][0]["region"],
               "a set field is written, an empty one is not added");
        report(!d["units"][1]["atlas"] && str(d["units"][1]["regionId"]) == "UBERON:0003881",
               "a cleared field is removed, the others kept");
        QMap<int,QStringList> seven;                                           // a 7-column row (older callers)
        seven.insert(0, QStringList{"2", "4", "DG", "", "", "", ""});
        w.setBaseDocument(up);
        w.setUnitsInformation(seven);
        report(w.writeTofile(up) && str(YAML::LoadFile(up.toStdString())["units"][0]["structure"]) == "DG",
               "seven-column rows still accepted");
    }

    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
