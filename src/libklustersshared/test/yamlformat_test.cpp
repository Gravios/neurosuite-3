// yamlformat_test.cpp — saving a parameter file keeps its layout.
//
// emitWithLayout() writes a yaml-cpp tree in block style but borrows, from the
// text it was loaded from, what the tree cannot hold: comments, inline lists,
// quoting, spacing.  Checked here:
//   - every file in the repository's templates round-trips to the same tree,
//     a second save is a fixed point, and every comment line survives;
//   - edits made through the tree keep the comments and quoting around them;
//   - collections and strings the caller creates get a sensible style;
//   - the output always loads back to the tree that was saved.
// No Qt code (the export header includes qglobal.h).  Usage: klustersshared_yamlformat_test [repo-root]
// (the root is needed only for the template round trips).

#include "yamlformat.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

static bool same(const YAML::Node& a, const YAML::Node& b)
{
    if (a.Type() != b.Type()) return false;
    if (a.IsScalar()) return a.Scalar() == b.Scalar();
    if (a.IsSequence()) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) if (!same(a[i], b[i])) return false;
        return true;
    }
    if (a.IsMap()) {
        if (a.size() != b.size()) return false;
        auto ib = b.begin();
        for (auto ia = a.begin(); ia != a.end(); ++ia, ++ib)
            if (ia->first.Scalar() != ib->first.Scalar() || !same(ia->second, ib->second)) return false;
        return true;
    }
    return true;
}

// Every comment line of @p src appears in @p out, in the same order.
static bool commentsKept(const std::string& src, const std::string& out)
{
    std::istringstream a(src), b(out);
    std::string l, m;
    while (std::getline(a, l)) {
        const size_t p = l.find_first_not_of(" \t");
        if (p == std::string::npos || l[p] != '#') continue;
        bool found = false;
        while (std::getline(b, m)) if (m == l) { found = true; break; }
        if (!found) return false;
    }
    return true;
}

static bool has(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

static const char* kSession = R"(# session notes: recorded by JG
# second header line

acquisitionSystem:
  nChannels: 4            # four channels
  samplingRate: 20000
  voltageRange: 20
generalInfo:
  date:                   # YYYY-MM-DD
  description: 'yes'
sourceRecording: {file: "rec.pl2", note: 'keep me'}
spikeDetection:
  channelGroups:
    # first shank
    - channels:   [0, 1, 2, 3]
      nSamples: 32
      sitePositions_um: [[0, 0], [0, 20]]
    # second shank
    - channels: [4, 5, 6, 7]
      nSamples: 32
programs:
  # == filters ==
  - name: ndm_hipass
    parameters:
      - name: overwrite
        value: 'false'
        status: Optional
      - name: cutoff
        value: 500.0
        status: Optional
    # Uncomment to override:
      # - name: order
      #   value: 4
  # == spikes ==
  - name: ndm_detectspikes
    parameters:
      - name: threshold
        value: '4'
    help: |-
      Detect spikes.

        indented line
      last line
# end of file
)";

int main(int argc, char** argv)
{
    // ---- an untouched document comes back as written ----
    {
        const std::string src = kSession;
        const YAML::Node doc = YAML::Load(src);
        const std::string out = yamlformat::emitWithLayout(doc, src);
        check(out == src, "untouched document is reproduced byte for byte");
        if (out != src) std::printf("---- got ----\n%s---- end ----\n", out.c_str());
    }

    // ---- edits through the tree ----
    {
        const std::string src = kSession;
        YAML::Node doc = YAML::Load(src);
        doc["acquisitionSystem"]["nChannels"] = 8;                          // value changed
        doc["acquisitionSystem"]["offset"] = 0;                              // key added
        doc["programs"][0]["parameters"][0]["value"] = std::string("true"); // quoted string replaced
        doc["programs"][1]["parameters"][0]["value"] = 5;                    // quoted number replaced by a number
        YAML::Node g(YAML::NodeType::Map);
        YAML::Node ch(YAML::NodeType::Sequence);
        for (int c = 8; c < 12; ++c) ch.push_back(c);
        g["channels"] = ch;                                                  // new group, new list
        g["note"] = std::string("no");                                       // new string that reads as a boolean
        doc["spikeDetection"]["channelGroups"].push_back(g);
        doc.remove("sourceRecording");                                       // key removed
        // programs reordered: comments follow each program by its name
        YAML::Node progs(YAML::NodeType::Sequence);
        progs.push_back(doc["programs"][1]);
        progs.push_back(doc["programs"][0]);
        doc["programs"] = progs;

        const std::string out = yamlformat::emitWithLayout(doc, src);
        const YAML::Node back = YAML::Load(out);
        check(same(back, doc), "edited document loads back to the edited tree");
        check(has(out, "# session notes: recorded by JG\n# second header line\n"), "header comment kept");
        check(has(out, "  nChannels: 8            # four channels\n"), "end-of-line comment kept on a changed value");
        check(has(out, "  date:                   # YYYY-MM-DD\n"), "empty value stays empty, comment kept");
        check(has(out, "  description: 'yes'\n"), "quoted untouched value stays quoted");
        check(has(out, "        value: 'true'\n"), "replacement of a quoted string is quoted");
        check(back["programs"][1]["parameters"][0]["value"].as<std::string>() == "true",
              "...and reads back as the string");
        check(has(out, "        value: '5'\n"), "a number replacing a quoted number string keeps the quotes");
        check(has(out, "    # first shank\n    - channels:   [0, 1, 2, 3]\n"), "item comment and inline list kept");
        check(has(out, "      sitePositions_um: [[0, 0], [0, 20]]\n"), "nested inline list kept");
        check(has(out, "    - channels: [8, 9, 10, 11]\n"), "new list of scalars written inline");
        check(has(out, "      note: 'no'\n"), "new boolean-looking string quoted");
        check(!has(out, "sourceRecording"), "removed key gone");
        check(has(out, "  offset: 0\n"), "added key present");
        const size_t det = out.find("  # == spikes ==\n  - name: ndm_detectspikes");
        const size_t hip = out.find("  # == filters ==\n  - name: ndm_hipass");
        check(det != std::string::npos && hip != std::string::npos && det < hip,
              "comments follow reordered programs by name");
        check(has(out, "    # Uncomment to override:\n      # - name: order\n      #   value: 4\n"),
              "commented-out parameters kept");
        check(has(out, "    help: |-\n      Detect spikes.\n\n        indented line\n      last line\n"),
              "literal block kept");
        check(has(out, "# end of file\n"), "trailer comment kept");
        check(yamlformat::emitWithLayout(back, out) == out, "second save is a fixed point");
    }
    {
        const std::string src = kSession;
        YAML::Node doc = YAML::Load(src);
        doc["generalInfo"]["date"] = std::string("2026-10-15");
        const std::string out = yamlformat::emitWithLayout(doc, src);
        check(has(out, "  date: 2026-10-15        # YYYY-MM-DD\n"), "filled-in empty value keeps the comment column");
    }
    // A number written where a quoted *number* string was ('4' -> 5) keeps the
    // quotes, so the parameter stays a string like the rest of its column; a
    // number replacing a quoted word drops them (the type changed on purpose).
    {
        const std::string src = "a: '4'\nb: 'abc'\n";
        YAML::Node doc = YAML::Load(src);
        doc["a"] = 5;
        doc["b"] = 7;
        const std::string out = yamlformat::emitWithLayout(doc, src);
        check(out == "a: '5'\nb: 7\n", "quoted number string stays quoted, word replaced by number does not");
    }

    // ---- no source text: a clean block document ----
    {
        YAML::Node doc(YAML::NodeType::Map);
        doc["parameters"]["version"] = std::string("1.0");
        YAML::Node ch(YAML::NodeType::Sequence);
        ch.push_back(0); ch.push_back(1);
        doc["spikeDetection"]["channelGroups"].push_back(YAML::Node(YAML::NodeType::Map));
        doc["spikeDetection"]["channelGroups"][0]["channels"] = ch;
        doc["spikeDetection"]["channelGroups"][0]["nSamples"] = 32;
        doc["files"] = YAML::Node(YAML::NodeType::Sequence);
        doc["notes"] = YAML::Node(YAML::NodeType::Null);
        doc["text"] = std::string("a: b # c");
        const std::string out = yamlformat::emitWithLayout(doc, std::string());
        check(same(YAML::Load(out), doc), "fresh document loads back");
        check(has(out, "  version: 1.0\n"), "fresh version plain (number-like text)");
        check(has(out, "    - channels: [0, 1]\n      nSamples: 32\n"), "fresh list inline, map in block");
        check(has(out, "files: []\n"), "empty list inline");
        check(has(out, "notes: ~\n"), "null written as ~");
        check(has(out, "text: 'a: b # c'\n"), "string needing quotes is quoted");
    }

    // ---- the repository's templates ----
    if (argc > 1) {
        const std::string root = argv[1];
        std::vector<std::string> files = {"/templates/template.yaml",
                                          "/src/ndmanager/src/ndManagerDefault.yaml",
                                          "/src/ndmanager/src/Template-16-tetrodes-32552Hz.yaml",
                                          "/src/ndmanager/src/Template-4-octrodes-20000Hz.yaml"};
        for (const auto& f : files) {
            const std::string src = yamlformat::readText(root + f);
            check(!src.empty(), f + ": readable");
            if (src.empty()) continue;
            const YAML::Node doc = YAML::Load(src);
            const std::string out = yamlformat::emitWithLayout(doc, src);
            check(same(YAML::Load(out), doc), f + ": same tree after a save");
            check(commentsKept(src, out), f + ": every comment line kept, in order");
            check(yamlformat::emitWithLayout(YAML::Load(out), out) == out, f + ": second save is a fixed point");
        }
    }

    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
