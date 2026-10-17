// contract_test.cpp — contract.yaml (repository root) states what the code does.
//
//   core_test_contract <path/to/contract.yaml>
//
// Checks that contract, plugin_protocol, session_schema and every formats entry
// equal version_contract.hpp; that each writer writes the version the contract
// names and each reader rejects the version after the one it claims; and the
// --ndm-version parser and mismatch report ndmanager uses.  The vectors block
// is test_shared_contract.py's.  contract.yaml is read line by line (it is kept
// flat for that), so this needs no YAML library.
#include <neurosuite/core/version_contract.hpp>
#include <neurosuite/core/neurofileio.h>
#include <neurosuite/core/pca_projection.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <string>

namespace ct = neurosuite::contract;

static int fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::cout << "FAIL: " << what << "\n"; ++fails; }
}

struct Contract {
    std::map<std::string, std::string> top;            // key -> scalar
    std::map<std::string, std::pair<int, int>> formats; // name -> {read, write}
};

static Contract readContract(const std::string& path) {
    Contract c;
    std::ifstream in(path);
    const std::regex topRe(R"(^([A-Za-z_]+):\s*(.*?)\s*$)");
    const std::regex fmtRe(R"(^\s+([A-Za-z0-9_]+):\s*\{\s*read:\s*(\d+)\s*,\s*write:\s*(\d+)\s*\}\s*$)");
    std::string line, section;
    std::smatch m;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (std::regex_match(line, m, topRe)) {
            section = m[1];
            if (!m[2].str().empty()) c.top[m[1]] = m[2];
            continue;
        }
        if (section == "formats" && std::regex_match(line, m, fmtRe))
            c.formats[m[1]] = {std::stoi(m[2]), std::stoi(m[3])};
    }
    return c;
}

static std::string unquote(std::string s) {
    if (s.size() >= 2 && (s.front() == '\'' || s.front() == '"') && s.back() == s.front())
        s = s.substr(1, s.size() - 2);
    return s;
}

static std::string firstLine(const std::string& path) {
    std::ifstream in(path);
    std::string l;
    std::getline(in, l);
    return l;
}

static void writeText(const std::string& path, const std::string& text) {
    std::ofstream(path) << text;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: core_test_contract contract.yaml\n"; return 2; }
    const Contract c = readContract(argv[1]);

    // ── contract.yaml == version_contract.hpp ──────────────────────────────
    check(c.top.count("contract") && c.top.at("contract") == std::to_string(ct::kContractVersion),
          "contract == kContractVersion (" + std::to_string(ct::kContractVersion) + ")");
    check(c.top.count("plugin_protocol")
              && c.top.at("plugin_protocol") == std::to_string(ct::kNdmPluginProtocol),
          "plugin_protocol == kNdmPluginProtocol");
    check(c.top.count("session_schema")
              && unquote(c.top.at("session_schema")) == ct::kSessionSchema,
          std::string("session_schema == kSessionSchema (") + ct::kSessionSchema + ")");
    const auto code = ct::formats();
    check(c.formats.size() == code.size(), "formats lists exactly the formats the code has");
    for (const auto& f : code) {
        auto it = c.formats.find(f.name);
        check(it != c.formats.end(), "formats has " + f.name);
        if (it == c.formats.end()) continue;
        check(it->second.first == f.read && it->second.second == f.write,
              f.name + ": contract {read " + std::to_string(it->second.first) + ", write "
                  + std::to_string(it->second.second) + "}, code {read " + std::to_string(f.read)
                  + ", write " + std::to_string(f.write) + "}");
    }

    // ── the writers write, and the readers stop at, those versions ─────────
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("ns3_contract_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    auto P = [&](const char* n) { return (dir / n).string(); };

    {   // .wti: v2 once a row has a parent
        neurofileio::WtiIndex idx;
        idx.nSamples = 4; idx.nChannels = 1;
        neurofileio::WtiRow r0; r0.row = 0; r0.link = "drift";
        neurofileio::WtiRow r1; r1.row = 1; r1.link = "drift"; r1.parent = 0;
        idx.rows = {r0, r1};
        check(neurofileio::writeWti(P("a.wti"), idx), "writeWti");
        check(firstLine(P("a.wti")) == "wti " + std::to_string(neurofileio::kWtiVersion),
              "writeWti writes wti " + std::to_string(neurofileio::kWtiVersion));
        check(neurofileio::readWti(P("a.wti")).ok, "readWti reads what writeWti wrote");
        writeText(P("b.wti"), "wti " + std::to_string(neurofileio::kWtiVersion + 1) + "\n");
        check(!neurofileio::readWti(P("b.wti")).ok, "readWti rejects a newer version");
    }
    {   // .wtl
        neurofileio::WtlForest f;
        check(neurofileio::writeWtl(P("a.wtl"), f), "writeWtl");
        check(firstLine(P("a.wtl")) == "wtl " + std::to_string(neurofileio::kWtlVersion),
              "writeWtl writes wtl " + std::to_string(neurofileio::kWtlVersion));
        check(neurofileio::readWtl(P("a.wtl")).ok, "readWtl reads what writeWtl wrote");
        writeText(P("b.wtl"), "wtl " + std::to_string(neurofileio::kWtlVersion + 1) + "\nnNodes 0\n");
        check(!neurofileio::readWtl(P("b.wtl")).ok, "readWtl rejects a newer version");
    }
    {   // .tcl
        check(neurofileio::writeTcl(P("a.tcl"), neurofileio::initTcl(2)), "writeTcl");
        check(firstLine(P("a.tcl")) == "tcl " + std::to_string(neurofileio::kTclVersion),
              "writeTcl writes tcl " + std::to_string(neurofileio::kTclVersion));
        check(neurofileio::readTcl(P("a.tcl")).ok, "readTcl reads what writeTcl wrote");
        writeText(P("b.tcl"), "tcl " + std::to_string(neurofileio::kTclVersion + 1) + "\nnClasses 0\n");
        check(!neurofileio::readTcl(P("b.tcl")).ok, "readTcl rejects a newer version");
    }
    {   // .eap: the version is the magic's 4th byte
        std::vector<int8_t> cells(4, neurofileio::EAP_ABSENT);
        check(neurofileio::writeEap(P("a.eap"), 2, 2, 0, 0, cells), "writeEap");
        std::ifstream in(P("a.eap"), std::ios::binary);
        char magic[4] = {0, 0, 0, 0};
        in.read(magic, 4);
        check(static_cast<unsigned char>(magic[3]) == neurofileio::kEapVersion,
              "writeEap writes version byte " + std::to_string(neurofileio::kEapVersion));
        in.close();
        check(neurofileio::readEap(P("a.eap")).ok, "readEap reads what writeEap wrote");
        std::fstream io(P("a.eap"), std::ios::binary | std::ios::in | std::ios::out);
        io.seekp(3);
        io.put(static_cast<char>(neurofileio::kEapVersion + 1));
        io.close();
        check(!neurofileio::readEap(P("a.eap")).ok, "readEap rejects a newer version");
    }
    {   // .pca (PCAE): the header's second int32
        neurosuite::core::PcaBasis b;
        b.nCh = 1; b.data2use = 2; b.nComp = 1;
        b.means = {{0.0, 0.0}};
        b.evec = {{1.0, 0.0}};
        check(neurosuite::core::writePca(P("a.pca"), b), "writePca");
        std::ifstream in(P("a.pca"), std::ios::binary);
        int32_t hdr[2] = {0, 0};
        in.read(reinterpret_cast<char*>(hdr), 8);
        check(hdr[1] == neurosuite::core::kPcaeVersion,
              "writePca writes version " + std::to_string(neurosuite::core::kPcaeVersion));
        in.close();
        neurosuite::core::PcaBasis back;
        check(neurosuite::core::loadPca(P("a.pca"), back), "loadPca reads what writePca wrote");
        std::fstream io(P("a.pca"), std::ios::binary | std::ios::in | std::ios::out);
        const int32_t newer = neurosuite::core::kPcaeVersion + 1;
        io.seekp(4);
        io.write(reinterpret_cast<const char*>(&newer), 4);
        io.close();
        check(!neurosuite::core::loadPca(P("a.pca"), back), "loadPca rejects a newer version");
    }
    fs::remove_all(dir);

    // ── --ndm-version parsing and the mismatch report ──────────────────────
    {
        const auto v = ct::parseNdmVersion(
            "ndm-plugin-protocol 1\r\nfiber-kit 0.44.0\nformat wtl 2 1\nformat pca 2 2\n"
            "format bad x 1\nformat tcl 1 1 extra\nformat eap 0 1\n");
        check(v.ok && v.protocol == 1, "parseNdmVersion: protocol 1");
        check(v.formats.size() == 2 && v.formats[0].name == "wtl" && v.formats[0].read == 2
                  && v.formats[0].write == 1 && v.formats[1].name == "pca",
              "parseNdmVersion: two well-formed format lines, malformed ones skipped");
        check(!ct::parseNdmVersion("fiber-kit 0.44.0\nndm-plugin-protocol 1\n").ok,
              "parseNdmVersion: the protocol line must come first");
        check(!ct::parseNdmVersion("").ok, "parseNdmVersion: empty output is not a plugin");

        const std::vector<ct::FormatVersion> host = {{"wtl", 2, 2}, {"pca", 2, 2}};
        check(ct::formatMismatches(host, v.formats).empty(),
              "a plugin that reads wtl 2 and writes wtl 1 matches a host at wtl 2");
        const auto old = ct::formatMismatches(host, {{"wtl", 1, 1}, {"pca", 3, 3}, {"xyz", 9, 9}});
        check(old.size() == 2, "a plugin behind on wtl and ahead on pca gives two messages");
        check(old.size() == 2 && old[0] == "wtl: the plugin reads up to v1, this build writes v2"
                  && old[1] == "pca: the plugin writes v3, this build reads up to v2",
              "mismatch messages name the format and both versions");
    }

    if (fails) { std::cout << fails << " check(s) failed\n"; return 1; }
    std::cout << "contract: all checks passed\n";
    return 0;
}
