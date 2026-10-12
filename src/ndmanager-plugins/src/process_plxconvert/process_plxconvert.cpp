/***************************************************************************
 *   Copyright (C) 2026 neurosuite-3 contributors                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

/*
 * process_plxconvert – Plexon .plx / .pl2 → neurosuite .dat + metadata YAML
 *
 * Usage:
 *   process_plxconvert [options] input.{plx,pl2} output_basename
 *
 * Options:
 *   -s, --stream PREFIX    Continuous stream to export, by channel-name prefix
 *                          (WB, SPKC, FP, AI, …).  Default: WB when it holds
 *                          data, else the fastest stream that does.
 *   -C, --channels SPEC    Subset / order within the stream: 1-based indices,
 *                          ranges and names, e.g. "1-16,18" or "WB01,WB05".
 *   -k, --keep-empty B     Keep stream channels without data as zero channels
 *                          (default 1), so .dat channel k stays Plexon channel k+1.
 *   -z, --pad-to-zero B    Start the .dat at tick 0 instead of at the first
 *                          sample (default 0).
 *   -c, --chunk N          Output samples per write chunk (default: ~4 MB of
 *                          output, i.e. 2^21 / nChannels samples — a buffer
 *                          that stays cache-friendly; much larger chunks are
 *                          measurably slower).
 *   -m, --meta FILE        Metadata output (default output_basename.plexon.yaml).
 *   -f, --force            Overwrite an existing output_basename.dat.
 *   -d, --dry-run          Print the file summary and exit.
 *   -h, --help             Display help.
 *
 * Outputs:
 *   output_basename.dat          int16 little-endian, sample-major interleaved.
 *                                Raw Plexon counts, copied verbatim.
 *   output_basename.plexon.yaml  `sourceRecording:` block — file header,
 *                                per-channel names / sources / µV per count,
 *                                .dat time origin and pauses, spike- and
 *                                event-channel summaries, OmniPlex device
 *                                settings, problems found — plus the
 *                                acquisitionSystem values for the session YAML
 *                                (merged by ndm_plx2dat / process_plxyaml.py).
 *
 * Timeline: every block lands at round((timestamp − t0) / ticksPerSample), so a
 * paused recording keeps its true time base (gaps are zero-filled and listed)
 * and the .dat stays aligned with Plexon's spike and event timestamps.
 *
 * Format sources:
 *   PLX ("Plexon 1"): the header structures of Plexon's Plexon.h as read by
 *     python-neo's PlexonRawIO (BSD-3).
 *   PL2: the clean-room notes of the OpenReadout project
 *     (docs/formats/plexon.md, MIT OR Apache-2.0), derived from hex dumps of
 *     public files.  Plexon publishes no PL2 specification and its own reader
 *     is a Windows DLL, so PL2 support is validated for internal consistency
 *     (the record walk ends on the footer, header spike counts equal the
 *     records, per-channel layouts agree); every inconsistency is reported in
 *     sourceRecording.findings rather than ignored.
 *
 * Exit status: 0 ok, 1 error, 2 usage, 3 output exists.
 */

#ifndef _LARGEFILE_SOURCE
#  define _LARGEFILE_SOURCE
#endif
#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

static_assert(std::endian::native == std::endian::little,
              "process_plxconvert assumes a little-endian host (as do the .dat files)");

namespace fs = std::filesystem;
using std::string;
using std::vector;

namespace {

constexpr int kConverterVersion = 1;

// ─────────────────────────────────────────────────────────────────────────────
// Minimal YAML value tree + emitter (strings are always double-quoted, so the
// output is valid YAML whatever the header text holds).
// ─────────────────────────────────────────────────────────────────────────────

struct Value;
using List = vector<Value>;
using Map = vector<std::pair<string, Value>>;

struct Value {
    std::variant<std::monostate, bool, int64_t, uint64_t, double, string,
                 std::shared_ptr<List>, std::shared_ptr<Map>> v;
    Value() = default;
    Value(bool b) : v(b) {}
    Value(int i) : v(static_cast<int64_t>(i)) {}
    Value(int64_t i) : v(i) {}
    Value(uint32_t i) : v(static_cast<int64_t>(i)) {}
    Value(uint64_t i) : v(i) {}
    Value(double d) : v(d) {}
    Value(const char* s) : v(string(s)) {}
    Value(string s) : v(std::move(s)) {}
    Value(List l) : v(std::make_shared<List>(std::move(l))) {}
    Value(Map m) : v(std::make_shared<Map>(std::move(m))) {}
    bool isContainer() const {
        return std::holds_alternative<std::shared_ptr<List>>(v) ||
               std::holds_alternative<std::shared_ptr<Map>>(v);
    }
    bool isEmptyContainer() const {
        if (auto l = std::get_if<std::shared_ptr<List>>(&v)) return (*l)->empty();
        if (auto m = std::get_if<std::shared_ptr<Map>>(&v)) return (*m)->empty();
        return false;
    }
};

string quote(const string& s) {
    std::ostringstream o;
    o << '"';
    for (char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"':  o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\n': o << "\\n"; break;
        case '\r': o << "\\r"; break;
        case '\t': o << "\\t"; break;
        default:
            if (c < 0x20 || c >= 0x7F)
                o << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(c) << std::dec << std::setfill(' ');
            else
                o << static_cast<char>(c);
        }
    }
    o << '"';
    return o.str();
}

string scalar(const Value& val) {
    return std::visit([](const auto& x) -> string {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate>) return "null";
        else if constexpr (std::is_same_v<T, bool>) return x ? "true" : "false";
        else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>)
            return std::to_string(x);
        else if constexpr (std::is_same_v<T, double>) {
            if (!std::isfinite(x)) return std::isnan(x) ? ".nan" : (x > 0 ? ".inf" : "-.inf");
            if (x == std::floor(x) && std::fabs(x) < 9e15)
                return std::to_string(static_cast<int64_t>(x));
            std::ostringstream o;
            o << std::setprecision(10) << x;
            return o.str();
        } else if constexpr (std::is_same_v<T, string>) return quote(x);
        else if constexpr (std::is_same_v<T, std::shared_ptr<List>>) return "[]";
        else return "{}";
    }, val.v);
}

void emit(std::ostream& o, const Value& val, int indent);

void emitMap(std::ostream& o, const Map& m, int indent, bool firstInline) {
    bool first = true;
    for (const auto& [k, x] : m) {
        if (!(first && firstInline)) o << string(static_cast<size_t>(indent), ' ');
        first = false;
        o << quote(k) << ":";
        if (x.isContainer() && !x.isEmptyContainer()) {
            o << "\n";
            emit(o, x, indent + 2);
        } else {
            o << " " << scalar(x) << "\n";
        }
    }
}

void emit(std::ostream& o, const Value& val, int indent) {
    if (auto m = std::get_if<std::shared_ptr<Map>>(&val.v)) {
        emitMap(o, **m, indent, false);
    } else if (auto l = std::get_if<std::shared_ptr<List>>(&val.v)) {
        for (const auto& x : **l) {
            o << string(static_cast<size_t>(indent), ' ') << "- ";
            if (auto mm = std::get_if<std::shared_ptr<Map>>(&x.v); mm && !(*mm)->empty()) {
                emitMap(o, **mm, indent + 2, true);
            } else if (x.isContainer() && !x.isEmptyContainer()) {
                o << "\n";
                emit(o, x, indent + 2);
            } else {
                o << scalar(x) << "\n";
            }
        }
    } else {
        o << string(static_cast<size_t>(indent), ' ') << scalar(val) << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// File access: sequential walk through a sliding buffer; random span reads.
// ─────────────────────────────────────────────────────────────────────────────

class Reader {
public:
    explicit Reader(const fs::path& p) : in(p, std::ios::binary) {
        if (!in) throw std::runtime_error("cannot open " + p.string());
        in.seekg(0, std::ios::end);
        fileSize = static_cast<uint64_t>(in.tellg());
    }
    uint64_t size() const { return fileSize; }

    /** Bytes [off, off+len) through a forward-sliding window (sequential walk). */
    const uint8_t* at(uint64_t off, uint64_t len) {
        if (off + len > fileSize) return nullptr;
        if (off < winStart || off + len > winStart + win.size()) {
            uint64_t want = std::max<uint64_t>(len, kWindow);
            want = std::min<uint64_t>(want, fileSize - off);
            win.resize(want);
            readExact(off, win.data(), want);
            winStart = off;
        }
        return win.data() + (off - winStart);
    }

    void readExact(uint64_t off, void* dst, uint64_t len) {
        in.clear();
        in.seekg(static_cast<std::streamoff>(off));
        in.read(static_cast<char*>(dst), static_cast<std::streamsize>(len));
        if (static_cast<uint64_t>(in.gcount()) != len)
            throw std::runtime_error("short read at byte " + std::to_string(off));
    }

private:
    static constexpr uint64_t kWindow = 64ull << 20;
    std::ifstream in;
    uint64_t fileSize = 0;
    vector<uint8_t> win;
    uint64_t winStart = 0;
};

template <typename T>
T rd(const uint8_t* p) {
    T x;
    std::memcpy(&x, p, sizeof(T));
    return x;
}

string cstr(const uint8_t* p, size_t n) {
    size_t k = 0;
    while (k < n && p[k] != 0) ++k;
    string s(reinterpret_cast<const char*>(p), k);  // byte text from the file header
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

double unitsToMicrovolts(const string& units) {
    string u;
    for (char c : units) u += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (u == "volts" || u == "volt" || u == "v") return 1e6;
    if (u == "millivolts" || u == "mv") return 1e3;
    if (u == "microvolts" || u == "uv") return 1.0;
    return 0.0;
}

double round9(double x) {
    if (x == 0.0 || !std::isfinite(x)) return x;
    std::ostringstream o;
    o << std::setprecision(9) << x;
    return std::stod(o.str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Model
// ─────────────────────────────────────────────────────────────────────────────

struct Block {
    uint64_t ts;
    uint64_t off;   // byte offset of the first sample
    uint32_t n;
};

struct AnalogChannel {
    string name;
    uint32_t source = 0, channel = 0;
    double rate = 0.0;
    double uvPerCount = 0.0;
    bool enabled = true;
    bool recording = true;
    Map meta;                 // container-specific header fields
    uint64_t nSamples = 0;
    uint64_t nBlocks = 0;
    bool indexed = false;     // part of the exported selection
    vector<Block> blocks;     // only when indexed
};

struct Recording {
    string container;         // "plx" | "pl2"
    double clock = 0.0;
    Map header;
    string recordedAt;        // ISO, may be empty
    string comment;
    vector<AnalogChannel> analog;
    vector<Map> spikeChannels;
    vector<Map> eventChannels;
    vector<string> findings;
    List deviceSettings;
    uint64_t dataStart = 0, dataStop = 0;
    // walk keys
    std::map<std::pair<uint32_t, uint32_t>, size_t> analogByKey;
    std::map<std::pair<uint32_t, uint32_t>, size_t> spikeByKey, eventByKey;
    vector<std::map<uint32_t, uint64_t>> unitCounts;   // per spike channel
    vector<uint64_t> eventCounts;                      // per event channel
};

string streamPrefix(const string& name) {
    string p;
    for (char c : name) {
        if (!std::isalpha(static_cast<unsigned char>(c))) break;
        p += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return p.empty() ? name : p;
}

Value mapGet(const Map& m, const string& k) {
    for (const auto& [key, x] : m)
        if (key == k) return x;
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// PLX
// ─────────────────────────────────────────────────────────────────────────────

constexpr uint64_t kPlxFileHeader = 7504, kPlxSpikeHeader = 1020,
                   kPlxEventHeader = 296, kPlxSlowHeader = 296;

void readPlxHeaders(Reader& r, Recording& rec) {
    const uint8_t* h = r.at(0, kPlxFileHeader);
    if (!h || std::memcmp(h, "PLEX", 4) != 0) throw std::runtime_error("not a PLX file");
    const int32_t version = rd<int32_t>(h + 4);
    rec.comment = cstr(h + 8, 128);
    int32_t f[14];
    std::memcpy(f, h + 136, sizeof f);
    const int32_t clock = f[0], nSpk = f[1], nEvt = f[2], nSlow = f[3];
    const double lastTs = rd<double>(h + 192);
    const uint8_t trod = h[200], dataTrod = h[201], bitsSpk = h[202], bitsSlow = h[203];
    const uint16_t spkMaxMv = rd<uint16_t>(h + 204), slowMaxMv = rd<uint16_t>(h + 206),
                   spkPreamp = rd<uint16_t>(h + 208);
    if (clock <= 0) throw std::runtime_error("implausible timestamp clock " + std::to_string(clock));
    for (int32_t k : {nSpk, nEvt, nSlow})
        if (k < 0 || k > 65536) throw std::runtime_error("implausible channel-header count");
    rec.container = "plx";
    rec.clock = clock;

    if (f[6] > 1900 && f[7] >= 1 && f[7] <= 12 && f[8] >= 1 && f[8] <= 31 &&
        f[9] >= 0 && f[9] < 24 && f[10] >= 0 && f[10] < 60 && f[11] >= 0 && f[11] < 62) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d",
                      f[6], f[7], f[8], f[9], f[10], f[11]);
        rec.recordedAt = buf;
    }

    Map& H = rec.header;
    H = {{"version", version}, {"comment", rec.comment}, {"timestampClock_hz", clock},
         {"nSpikeChannelHeaders", nSpk}, {"nEventChannelHeaders", nEvt},
         {"nContinuousChannelHeaders", nSlow}, {"waveformPoints", f[4]},
         {"preThresholdPoints", f[5]}, {"waveformRate_hz", f[13]},
         {"lastTimestamp_ticks", static_cast<int64_t>(lastTs)},
         {"recordedAt", rec.recordedAt.empty() ? Value() : Value(rec.recordedAt)},
         {"fastRead", f[12]}};
    if (version >= 103) {
        H.push_back({"electrodesPerChannel", static_cast<int>(trod)});
        H.push_back({"dataElectrodesPerChannel", static_cast<int>(dataTrod)});
        H.push_back({"spikeBits", static_cast<int>(bitsSpk)});
        H.push_back({"continuousBits", static_cast<int>(bitsSlow)});
        H.push_back({"spikeMaxMagnitude_mV", static_cast<int>(spkMaxMv)});
        H.push_back({"continuousMaxMagnitude_mV", static_cast<int>(slowMaxMv)});
    }
    if (version >= 105) H.push_back({"spikePreampGain", static_cast<int>(spkPreamp)});
    if (version >= 106) {
        H.push_back({"acquiringSoftware", cstr(h + 210, 18)});
        H.push_back({"processingSoftware", cstr(h + 228, 18)});
    }

    // Gain formulas (python-neo PlexonRawIO), converted to µV per count.
    auto spikeUv = [&](int32_t gain) -> double {
        if (gain <= 0) return 0.0;
        if (version < 103) return 3000.0 / (2048.0 * gain * 1000.0) * 1e3;
        const double bits = bitsSpk ? bitsSpk : 12;
        const double pre = (version >= 105 && spkPreamp) ? spkPreamp : 1000.0;
        return spkMaxMv / (0.5 * std::pow(2.0, bits) * gain * pre) * 1e3;
    };
    auto slowUv = [&](int32_t gain, int32_t preamp) -> double {
        if (gain <= 0) return 0.0;
        if (version <= 101) return 5000.0 / (2048.0 * gain * 1000.0) * 1e3;
        if (version == 102) return 5000.0 / (2048.0 * gain * std::max(preamp, 1)) * 1e3;
        // Neo uses the spike bit depth here; the continuous depth is the physical
        // one and the two agree on OmniPlex files.
        const double bits = bitsSlow ? bitsSlow : (bitsSpk ? bitsSpk : 12);
        return slowMaxMv / (0.5 * std::pow(2.0, bits) * gain * std::max(preamp, 1)) * 1e3;
    };

    uint64_t pos = kPlxFileHeader;
    const uint64_t headersEnd = pos + static_cast<uint64_t>(nSpk) * kPlxSpikeHeader +
                                static_cast<uint64_t>(nEvt) * kPlxEventHeader +
                                static_cast<uint64_t>(nSlow) * kPlxSlowHeader;
    if (headersEnd > r.size()) throw std::runtime_error("channel headers run past the end of the file");
    for (int32_t i = 0; i < nSpk; ++i, pos += kPlxSpikeHeader) {
        const uint8_t* b = r.at(pos, kPlxSpikeHeader);
        int32_t v[9];
        std::memcpy(v, b + 64, sizeof v);
        const double uv = spikeUv(v[4]);
        Map d{{"name", cstr(b, 32)}, {"signalName", cstr(b + 32, 32)}, {"channel", v[0]},
              {"waveformRate_hz", v[1]}, {"sig", v[2]}, {"reference", v[3]}, {"gain", v[4]},
              {"filter", v[5]}, {"threshold_counts", v[6]},
              {"threshold_uV", std::round(v[6] * uv * 1000.0) / 1000.0},
              {"sortMethod", v[7]}, {"nUnits", v[8]}, {"uvPerCount", round9(uv)}};
        if (version >= 105) d.push_back({"comment", cstr(b + 848, 128)});
        if (version >= 106) {
            d.push_back({"sourceId", static_cast<int>(b[976])});
            d.push_back({"channelId", static_cast<int>(rd<uint16_t>(b + 978))});
        }
        rec.spikeByKey[{0u, static_cast<uint32_t>(v[0])}] = rec.spikeChannels.size();
        rec.spikeChannels.push_back(std::move(d));
    }
    for (int32_t i = 0; i < nEvt; ++i, pos += kPlxEventHeader) {
        const uint8_t* b = r.at(pos, kPlxEventHeader);
        const int32_t ch = rd<int32_t>(b + 32);
        Map d{{"name", cstr(b, 32)}, {"channel", ch}, {"comment", cstr(b + 36, 128)}};
        if (version >= 106) {
            d.push_back({"sourceId", static_cast<int>(b[164])});
            d.push_back({"channelId", static_cast<int>(rd<uint16_t>(b + 166))});
        }
        rec.eventByKey[{0u, static_cast<uint32_t>(ch)}] = rec.eventChannels.size();
        rec.eventChannels.push_back(std::move(d));
    }
    for (int32_t i = 0; i < nSlow; ++i, pos += kPlxSlowHeader) {
        const uint8_t* b = r.at(pos, kPlxSlowHeader);
        int32_t v[6];
        std::memcpy(v, b + 32, sizeof v);
        AnalogChannel c;
        c.name = cstr(b, 32);
        c.channel = static_cast<uint32_t>(v[0]);
        c.rate = v[1];
        c.uvPerCount = slowUv(v[2], v[4]);
        c.enabled = v[3] != 0;
        c.meta = {{"channel", v[0]}, {"gain", v[2]}, {"preampGain", v[4]},
                  {"spikeChannel", v[5]}, {"comment", cstr(b + 56, 128)}};
        if (version >= 106) {
            c.meta.push_back({"sourceId", static_cast<int>(b[184])});
            c.meta.push_back({"channelId", static_cast<int>(rd<uint16_t>(b + 186))});
        }
        rec.analogByKey[{0u, c.channel}] = rec.analog.size();
        rec.analog.push_back(std::move(c));
    }
    rec.dataStart = pos;
    rec.dataStop = r.size();
}

void walkPlx(Reader& r, Recording& rec) {
    uint64_t pos = rec.dataStart;
    const uint64_t size = r.size();
    uint64_t unlisted = 0, nextReport = pos + (1ull << 30);
    while (pos + 16 <= size) {
        const uint8_t* b = r.at(pos, 16);
        const int16_t type = rd<int16_t>(b);
        const uint64_t ts = (static_cast<uint64_t>(rd<uint16_t>(b + 2)) << 32) | rd<uint32_t>(b + 4);
        const uint32_t ch = static_cast<uint16_t>(rd<int16_t>(b + 8));
        const uint16_t unit = rd<uint16_t>(b + 10);
        const int16_t nwf = rd<int16_t>(b + 12), nwords = rd<int16_t>(b + 14);
        const uint64_t npts = (nwf > 0 && nwords > 0) ? static_cast<uint64_t>(nwf) * static_cast<uint64_t>(nwords) : 0;
        const uint64_t end = pos + 16 + 2 * npts;
        if (type != 1 && type != 4 && type != 5) {
            rec.findings.push_back("unknown block type " + std::to_string(type) + " at byte " +
                                   std::to_string(pos) + "; walk stopped");
            break;
        }
        if (end > size) {
            rec.findings.push_back("block at byte " + std::to_string(pos) +
                                   " runs past the end of the file (truncated); dropped");
            break;
        }
        if (type == 5) {
            auto it = rec.analogByKey.find({0u, ch});
            if (it == rec.analogByKey.end()) {
                ++unlisted;
            } else if (npts) {
                AnalogChannel& c = rec.analog[it->second];
                c.nSamples += npts;
                ++c.nBlocks;
                if (c.indexed) c.blocks.push_back({ts, pos + 16, static_cast<uint32_t>(npts)});
            }
        } else if (type == 1) {
            auto it = rec.spikeByKey.find({0u, ch});
            if (it != rec.spikeByKey.end()) ++rec.unitCounts[it->second][unit];
        } else {
            auto it = rec.eventByKey.find({0u, ch});
            if (it != rec.eventByKey.end()) ++rec.eventCounts[it->second];
        }
        pos = end;
        if (pos >= nextReport) {
            std::cerr << "  indexing: " << std::fixed << std::setprecision(1)
                      << 100.0 * static_cast<double>(pos) / static_cast<double>(size) << " %\n";
            nextReport += 1ull << 30;
        }
    }
    if (unlisted)
        rec.findings.push_back(std::to_string(unlisted) +
                               " continuous blocks name a channel with no header (ignored)");
    if (pos < size && size - pos < 16)
        rec.findings.push_back(std::to_string(size - pos) + " trailing bytes after the last block");
}

// ─────────────────────────────────────────────────────────────────────────────
// PL2
// ─────────────────────────────────────────────────────────────────────────────

constexpr uint64_t kPl2FileHeader = 0x480;
constexpr uint8_t kRecSpikeHeader = 0xD5, kRecAnalogHeader = 0xD4, kRecDigitalHeader = 0xD6;
constexpr uint8_t kRecAnalog = 0x42, kRecSpikes = 0x31, kRecEvents = 0x5A, kRecEnd = 0x59;

bool looksLikePl2(const uint8_t* h) { return h[0] == 0xFE && std::memcmp(h + 10, "PLEXON", 6) == 0; }

uint64_t pl2RecordLen(uint64_t words) { return 16 + (2 * words + 15) / 16 * 16; }

/** Element-style OmniPlex <Settings> blocks (<m_xxx>value</m_xxx>), e.g. filter cut-offs. */
List scrapeSettings(const string& blob) {
    List out;
    std::set<string> seen;
    size_t p = 0;
    while (out.size() < 64) {
        const size_t a = blob.find("<Settings", p);
        if (a == string::npos) break;
        const size_t e = blob.find("</Settings>", a);
        if (e == string::npos) break;
        const string body = blob.substr(a, e - a);
        p = e + 11;
        Map d;
        string key;
        size_t q = 0;
        while ((q = body.find("<m_", q)) != string::npos) {
            const size_t ke = body.find('>', q);
            if (ke == string::npos) break;
            const string tag = body.substr(q + 1, ke - q - 1);
            const string close = "</" + tag + ">";
            const size_t ve = body.find(close, ke);
            if (tag.find_first_of(" /\"=") != string::npos || ve == string::npos) { q = ke; continue; }
            const string val = body.substr(ke + 1, ve - ke - 1);
            char* endp = nullptr;
            const double num = std::strtod(val.c_str(), &endp);
            if (!val.empty() && endp && *endp == '\0') d.push_back({tag, num});
            else d.push_back({tag, val});
            key += tag + "=" + val + ";";
            q = ve + close.size();
        }
        if (!d.empty() && seen.insert(key).second) out.push_back(std::move(d));
    }
    return out;
}

void readPl2Headers(Reader& r, Recording& rec) {
    if (r.size() < kPl2FileHeader) throw std::runtime_error("not a PL2 file");
    const uint8_t* h = r.at(0, kPl2FileHeader);
    if (!looksLikePl2(h)) throw std::runtime_error("not a PL2 file");
    uint64_t off[6];
    std::memcpy(off, h + 0x20, sizeof off);
    const uint64_t headersEnd = off[0], dataStart = off[1], footerStart = off[2],
                   indexStart = off[3], startCount = off[4], duration = off[5];
    uint32_t tm[9];
    std::memcpy(tm, h + 0x230, sizeof tm);
    const double clock = rd<double>(h + 0x258);
    const uint32_t total = rd<uint32_t>(h + 0x260), nSpk = rd<uint32_t>(h + 0x264),
                   nAna = rd<uint32_t>(h + 0x26C), nDig = rd<uint32_t>(h + 0x274);
    if (!(clock > 0)) throw std::runtime_error("implausible timestamp clock");
    for (uint32_t k : {nSpk, nAna, nDig})
        if (k > 65536) throw std::runtime_error("implausible channel-header count");
    rec.container = "pl2";
    rec.clock = clock;
    rec.comment = cstr(h + 0xE0, 256);
    if (tm[0] < 62 && tm[1] < 60 && tm[2] < 24 && tm[3] >= 1 && tm[3] <= 31 && tm[4] < 12 && tm[5] <= 300) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%04u-%02u-%02uT%02u:%02u:%02u",
                      1900 + tm[5], tm[4] + 1, tm[3], tm[2], tm[1], tm[0]);
        rec.recordedAt = buf;
    }
    rec.header = {
        {"comment", rec.comment},
        {"application", cstr(h + 0x1E0, 64)},
        {"applicationVersion", cstr(h + 0x220, 16)},
        {"recordedAt", rec.recordedAt.empty() ? Value() : Value(rec.recordedAt)},
        {"timestampClock_hz", clock},
        {"startCount_ticks", startCount},
        {"duration_ticks", duration},
        {"duration_s", round9(static_cast<double>(duration) / clock)},
        {"nChannelHeaders", total},
        {"nSpikeChannelHeaders", nSpk},
        {"nAnalogChannelHeaders", nAna},
        {"nDigitalChannelHeaders", nDig},
        {"offsets", Map{{"headersEnd", headersEnd}, {"dataStart", dataStart},
                        {"footerStart", footerStart}, {"index", indexStart}}}};

    uint64_t pos = kPl2FileHeader;
    const std::pair<uint8_t, uint32_t> kinds[3] = {
        {kRecSpikeHeader, nSpk}, {kRecAnalogHeader, nAna}, {kRecDigitalHeader, nDig}};
    for (const auto& [kind, count] : kinds) {
        const uint64_t len = kind == kRecSpikeHeader ? 2592 : kind == kRecAnalogHeader ? 512 : 368;
        for (uint32_t i = 0; i < count; ++i, pos += len) {
            const uint8_t* b = r.at(pos, len);
            if (!b) throw std::runtime_error("channel headers run past the end of the file");
            if (b[0] != kind) {
                std::ostringstream m;
                m << "channel header of type 0x" << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(b[0]) << " at byte " << std::dec << pos << ", expected 0x"
                  << std::hex << std::setw(2) << static_cast<int>(kind);
                throw std::runtime_error(m.str());
            }
            uint32_t sc[4];
            std::memcpy(sc, b + 0x50, sizeof sc);
            const string name = cstr(b + 16, 64);
            Map d{{"name", name}, {"source", sc[0]}, {"channel", sc[1]},
                  {"enabled", sc[2] != 0}, {"recording", sc[3] != 0}};
            string units;
            double rate = 0.0, upc = 0.0, uv = 0.0;
            if (kind != kRecDigitalHeader) {
                units = cstr(b + 0x60, 16);
                rate = rd<double>(b + 0x70);
                upc = rd<double>(b + 0x78);
                uv = upc * unitsToMicrovolts(units);
                d.push_back({"units", units});
                d.push_back({"rate_hz", rate});
                d.push_back({"unitsPerCount", upc});
                d.push_back({"uvPerCount", round9(uv)});
            }
            if (kind == kRecSpikeHeader) {
                const uint32_t wf = rd<uint32_t>(b + 0x80);
                const int32_t thr = rd<int32_t>(b + 0x84);
                const uint32_t pre = rd<uint32_t>(b + 0x88);
                Map hdrUnits;
                for (uint32_t u = 0; u < 256; ++u) {
                    const uint64_t n = rd<uint64_t>(b + 0xA0 + 8 * u);
                    if (n) hdrUnits.push_back({std::to_string(u), n});
                }
                d.push_back({"waveformSamples", wf});
                d.push_back({"threshold_counts", thr});
                d.push_back({"threshold_uV", std::round(thr * uv * 1000.0) / 1000.0});
                d.push_back({"preThresholdSamples", pre});
                d.push_back({"headerUnitCounts", hdrUnits});
                rec.spikeByKey[{sc[0], sc[1]}] = rec.spikeChannels.size();
                rec.spikeChannels.push_back(std::move(d));
            } else if (kind == kRecAnalogHeader) {
                AnalogChannel c;
                c.name = name;
                c.source = sc[0];
                c.channel = sc[1];
                c.enabled = sc[2] != 0;
                c.recording = sc[3] != 0;
                c.rate = rate;
                c.uvPerCount = uv;
                c.meta = {{"source", sc[0]}, {"channel", sc[1]}, {"units", units},
                          {"unitsPerCount", upc}, {"device", cstr(b + 0xD8, 64)},
                          {"electrode", cstr(b + 0x118, 64)}};
                rec.analogByKey[{c.source, c.channel}] = rec.analog.size();
                rec.analog.push_back(std::move(c));
            } else {
                rec.eventByKey[{sc[0], sc[1]}] = rec.eventChannels.size();
                rec.eventChannels.push_back(std::move(d));
            }
        }
    }
    const uint64_t headersStop = pos;
    rec.dataStart = (dataStart >= headersStop && dataStart <= r.size()) ? dataStart : headersStop;
    rec.dataStop = (footerStart > rec.dataStart && footerStart <= r.size()) ? footerStart : r.size();
    rec.header.push_back({"walkedUntil_footerStart", footerStart > rec.dataStart && footerStart <= r.size()});

    // Device settings (filter cut-offs etc.) live as XML between the channel
    // headers and the data; offline writers may leave this region empty.
    if (rec.dataStart > headersStop && rec.dataStart - headersStop <= (64ull << 20)) {
        string blob(rec.dataStart - headersStop, '\0');
        r.readExact(headersStop, blob.data(), blob.size());
        rec.deviceSettings = scrapeSettings(blob);
    }
}

void walkPl2(Reader& r, Recording& rec) {
    const uint64_t size = r.size(), stop = rec.dataStop;
    uint64_t p = rec.dataStart;
    std::map<uint8_t, uint64_t> recCounts;
    std::optional<uint64_t> endDuration;
    std::set<std::pair<uint32_t, uint32_t>> unlisted;
    while (p < stop) {
        const uint8_t* b = r.at(p, 16);
        if (!b) {
            rec.findings.push_back("record header at byte " + std::to_string(p) + " is cut off (truncated)");
            break;
        }
        const uint8_t kind = b[0];
        const uint32_t src = b[1];
        const uint64_t words = rd<uint16_t>(b + 2);
        const uint32_t ch = rd<uint16_t>(b + 4);
        uint64_t len = pl2RecordLen(words);
        if (kind == kRecAnalog) {
            const uint32_t n = rd<uint16_t>(b + 6);
            const uint64_t ts = rd<uint64_t>(b + 8);
            if (p + 16 + 2ull * n > size) {
                rec.findings.push_back("analog record at byte " + std::to_string(p) +
                                       " is cut off (truncated); dropped");
                break;
            }
            auto it = rec.analogByKey.find({src, ch});
            if (it == rec.analogByKey.end()) {
                unlisted.insert({src, ch});
            } else if (n) {
                AnalogChannel& c = rec.analog[it->second];
                c.nSamples += n;
                ++c.nBlocks;
                if (c.indexed) c.blocks.push_back({ts, p + 16, n});
            }
        } else if (kind == kRecSpikes) {
            const uint32_t wf = rd<uint16_t>(b + 6);
            const uint32_t n = rd<uint32_t>(b + 8);
            len = pl2RecordLen(static_cast<uint64_t>(n) * (5 + wf));
            auto it = rec.spikeByKey.find({src, ch});
            if (it != rec.spikeByKey.end() && n && p + len <= size) {
                vector<uint16_t> units(n);
                r.readExact(p + 16 + 8ull * n, units.data(), 2ull * n);
                for (uint16_t u : units) ++rec.unitCounts[it->second][u];
            }
        } else if (kind == kRecEvents) {
            const uint32_t n = rd<uint16_t>(b + 6);
            len = pl2RecordLen(5ull * n);
            auto it = rec.eventByKey.find({src, ch});
            if (it != rec.eventByKey.end()) rec.eventCounts[it->second] += n;
        } else if (kind == kRecEnd && words == 10 && p + 32 <= size) {
            endDuration = rd<uint64_t>(r.at(p + 24, 8));
        }
        ++recCounts[kind];
        if (p + len > size) {
            std::ostringstream m;
            m << "record at byte " << p << " (type 0x" << std::hex << static_cast<int>(kind)
              << ") is cut off (truncated)";
            rec.findings.push_back(m.str());
            break;
        }
        p += len;
    }
    const uint64_t footer = std::get<uint64_t>(mapGet(*std::get<std::shared_ptr<Map>>(
                                mapGet(rec.header, "offsets").v), "footerStart").v);
    if (stop == footer && p != footer)
        rec.findings.push_back("record walk ended at byte " + std::to_string(p) +
                               ", footer starts at " + std::to_string(footer));
    if (!recCounts.count(kRecEnd))
        rec.findings.push_back("no end-of-recording record (the recording may have been cut short)");
    const uint64_t duration = std::get<uint64_t>(mapGet(rec.header, "duration_ticks").v);
    if (endDuration && duration && *endDuration != duration)
        rec.findings.push_back("end record says " + std::to_string(*endDuration) +
                               " ticks, file header " + std::to_string(duration));
    if (!unlisted.empty())
        rec.findings.push_back("analog records for " + std::to_string(unlisted.size()) +
                               " (source, channel) pairs without a header");
    Map rc;
    for (const auto& [k, n] : recCounts) {
        char key[8];
        std::snprintf(key, sizeof key, "0x%02x", k);
        rc.push_back({key, n});
    }
    rec.header.push_back({"recordCounts", rc});
    if (endDuration) rec.header.push_back({"endRecordDuration_ticks", *endDuration});
}

// ─────────────────────────────────────────────────────────────────────────────
// Stream selection and layout
// ─────────────────────────────────────────────────────────────────────────────

std::map<string, vector<size_t>> streamsOf(const Recording& rec) {
    std::map<string, vector<size_t>> s;
    for (size_t i = 0; i < rec.analog.size(); ++i) s[streamPrefix(rec.analog[i].name)].push_back(i);
    return s;
}

/** Candidate streams in preference order: WB first, then fastest / widest. */
vector<string> streamCandidates(const Recording& rec) {
    auto s = streamsOf(rec);
    vector<string> keys;
    for (const auto& [k, v] : s) {
        bool any = false;
        for (size_t i : v) any = any || (rec.analog[i].recording && rec.analog[i].enabled);
        if (any) keys.push_back(k);
    }
    auto maxRate = [&](const string& k) {
        double m = 0;
        for (size_t i : s[k]) m = std::max(m, rec.analog[i].rate);
        return m;
    };
    std::stable_sort(keys.begin(), keys.end(), [&](const string& a, const string& b) {
        if ((a == "WB") != (b == "WB")) return a == "WB";
        if (maxRate(a) != maxRate(b)) return maxRate(a) > maxRate(b);
        return s[a].size() > s[b].size();
    });
    return keys;
}

vector<size_t> parseChannelSpec(const string& spec, const vector<size_t>& chans, const Recording& rec) {
    if (spec.empty()) return chans;
    vector<size_t> out;
    std::stringstream ss(spec);
    string tok;
    while (std::getline(ss, tok, ',')) {
        tok.erase(0, tok.find_first_not_of(" \t"));
        tok.erase(tok.find_last_not_of(" \t") + 1);
        if (tok.empty()) continue;
        string up;
        for (char c : tok) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        bool named = false;
        for (size_t i : chans) {
            string n;
            for (char c : rec.analog[i].name) n += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (n == up) { out.push_back(i); named = true; break; }
        }
        if (named) continue;
        const size_t dash = tok.find('-');
        try {
            size_t used = 0;
            const long a = std::stol(tok.substr(0, dash), &used);
            if (used != tok.substr(0, dash).size()) throw std::invalid_argument(tok);
            long b = a;
            if (dash != string::npos) {
                b = std::stol(tok.substr(dash + 1), &used);
                if (used != tok.substr(dash + 1).size()) throw std::invalid_argument(tok);
            }
            if (a < 1 || b < a || b > static_cast<long>(chans.size()))
                throw std::runtime_error("channel range '" + tok + "' outside 1.." + std::to_string(chans.size()));
            for (long k = a; k <= b; ++k) out.push_back(chans[static_cast<size_t>(k - 1)]);
        } catch (const std::logic_error&) {
            throw std::runtime_error("bad channel token '" + tok + "' (use 1-based indices or names)");
        }
    }
    return out;
}

struct Layout {
    vector<size_t> chans;     // indices into rec.analog, .dat order
    double rate = 0;
    double tps = 0;           // ticks per sample
    uint64_t t0 = 0;
    uint64_t nSamples = 0;
    vector<vector<int64_t>> start;   // per channel, per block: first .dat sample
    vector<std::pair<uint64_t, uint64_t>> segments;   // reference channel (first, n)
    vector<string> warnings;
};

Layout buildLayout(Recording& rec, vector<size_t> chans, bool keepEmpty, bool padToZero) {
    Layout L;
    if (!keepEmpty) {
        string dropped;
        vector<size_t> kept;
        for (size_t i : chans) {
            if (rec.analog[i].nBlocks) kept.push_back(i);
            else dropped += (dropped.empty() ? "" : ", ") + rec.analog[i].name;
        }
        if (!dropped.empty()) L.warnings.push_back("dropped channels without data: " + dropped);
        chans = kept;
    }
    std::set<double> rates;
    uint64_t t0 = UINT64_MAX;
    for (size_t i : chans) {
        const AnalogChannel& c = rec.analog[i];
        if (!c.nBlocks) continue;
        rates.insert(c.rate);
        for (const Block& b : c.blocks) t0 = std::min(t0, b.ts);
    }
    if (rates.empty()) throw std::runtime_error("none of the selected channels holds data");
    if (rates.size() > 1) {
        string s;
        for (double x : rates) s += (s.empty() ? "" : ", ") + scalar(Value(x)) + " Hz";
        throw std::runtime_error("selected channels mix sampling rates " + s + "; restrict with --channels");
    }
    L.chans = chans;
    L.rate = *rates.begin();
    if (!(L.rate > 0)) throw std::runtime_error("selected stream has sampling rate 0");
    L.tps = rec.clock / L.rate;
    L.t0 = padToZero ? 0 : t0;

    size_t ref = SIZE_MAX;
    for (size_t j = 0; j < chans.size(); ++j) {
        AnalogChannel& c = rec.analog[chans[j]];
        vector<int64_t> s;
        s.reserve(c.blocks.size());
        double jitter = 0;
        uint64_t overlaps = 0;
        int64_t prevEnd = INT64_MIN;
        for (const Block& b : c.blocks) {
            const double x = static_cast<double>(b.ts - L.t0) / L.tps;
            const int64_t si = std::llround(x);
            jitter = std::max(jitter, std::fabs(x - static_cast<double>(si)));
            if (si < prevEnd) ++overlaps;
            prevEnd = std::max(prevEnd, si + static_cast<int64_t>(b.n));
            L.nSamples = std::max<uint64_t>(L.nSamples, static_cast<uint64_t>(si) + b.n);
            s.push_back(si);
        }
        if (jitter > 0.01) {
            std::ostringstream m;
            m << c.name << ": block timestamps sit up to " << std::fixed << std::setprecision(3)
              << jitter << " samples off the sample grid";
            L.warnings.push_back(m.str());
        }
        if (overlaps)
            L.warnings.push_back(c.name + ": " + std::to_string(overlaps) +
                                 " blocks overlap their predecessor (later data wins)");
        if (ref == SIZE_MAX && !c.blocks.empty()) ref = j;
        L.start.push_back(std::move(s));
    }
    const AnalogChannel& R = rec.analog[chans[ref]];
    const vector<int64_t>& rs = L.start[ref];
    uint64_t a = static_cast<uint64_t>(rs[0]), e = a + R.blocks[0].n;
    for (size_t k = 1; k < rs.size(); ++k) {
        const uint64_t s = static_cast<uint64_t>(rs[k]);
        if (s > e) {
            L.segments.push_back({a, e - a});
            a = s;
        }
        e = std::max<uint64_t>(e, s + R.blocks[k].n);
    }
    L.segments.push_back({a, e - a});
    for (size_t j = 0; j < chans.size(); ++j) {
        const AnalogChannel& c = rec.analog[chans[j]];
        if (j == ref || c.blocks.empty()) continue;
        bool same = L.start[j] == rs && c.blocks.size() == R.blocks.size();
        for (size_t k = 0; same && k < c.blocks.size(); ++k) same = c.blocks[k].n == R.blocks[k].n;
        if (!same)
            L.warnings.push_back(c.name + ": block layout differs from " + R.name + " (" +
                                 std::to_string(c.nSamples) + " vs " + std::to_string(R.nSamples) +
                                 " samples)");
    }
    return L;
}

// ─────────────────────────────────────────────────────────────────────────────
// .dat writer
// ─────────────────────────────────────────────────────────────────────────────

void writeDat(Reader& r, const Recording& rec, const Layout& L, const fs::path& out, uint64_t chunk) {
    const size_t nch = L.chans.size();
    const fs::path tmp = out.string() + ".part";
    std::ofstream fo(tmp, std::ios::binary | std::ios::trunc);
    if (!fo) throw std::runtime_error("cannot create " + tmp.string());

    // Per channel: block order by start sample (built only when the file
    // holds a channel's blocks out of time order), and a monotone cursor.
    vector<vector<size_t>> order(nch);
    vector<size_t> cursor(nch, 0);
    for (size_t j = 0; j < nch; ++j) {
        if (std::is_sorted(L.start[j].begin(), L.start[j].end())) continue;
        order[j].resize(L.start[j].size());
        for (size_t k = 0; k < order[j].size(); ++k) order[j][k] = k;
        std::stable_sort(order[j].begin(), order[j].end(),
                         [&](size_t x, size_t y) { return L.start[j][x] < L.start[j][y]; });
    }
    struct Item { uint64_t src; uint64_t dst; uint64_t n; size_t j; };
    vector<int16_t> buf;
    vector<uint8_t> span;
    vector<int16_t> tmpSamples;
    constexpr uint64_t kMaxSpan = 512ull << 20;
    double nextReport = 0.0;

    for (uint64_t a = 0; a < L.nSamples; a += chunk) {
        const uint64_t b = std::min(a + chunk, L.nSamples);
        buf.assign((b - a) * nch, 0);
        vector<Item> items;
        uint64_t lo = UINT64_MAX, hi = 0;
        for (size_t j = 0; j < nch; ++j) {
            const AnalogChannel& c = rec.analog[L.chans[j]];
            const auto& st = L.start[j];
            const auto& ord = order[j];
            const size_t nb = st.size();
            auto blockAt = [&](size_t k) { return ord.empty() ? k : ord[k]; };
            while (cursor[j] < nb &&
                   static_cast<uint64_t>(st[blockAt(cursor[j])]) + c.blocks[blockAt(cursor[j])].n <= a)
                ++cursor[j];
            for (size_t k = cursor[j]; k < nb; ++k) {
                const size_t bi = blockAt(k);
                const uint64_t s = static_cast<uint64_t>(st[bi]);
                if (s >= b) break;
                const uint64_t e = s + c.blocks[bi].n;
                if (e <= a) continue;
                const uint64_t from = std::max(s, a), to = std::min(e, b);
                const uint64_t src = c.blocks[bi].off + 2 * (from - s);
                items.push_back({src, from - a, to - from, j});
                lo = std::min(lo, src);
                hi = std::max(hi, src + 2 * (to - from));
            }
        }
        if (!items.empty() && hi - lo <= kMaxSpan) {
            span.resize(hi - lo);
            r.readExact(lo, span.data(), span.size());
            for (const Item& it : items) {
                const uint8_t* p = span.data() + (it.src - lo);
                for (uint64_t k = 0; k < it.n; ++k)
                    std::memcpy(&buf[(it.dst + k) * nch + it.j], p + 2 * k, 2);
            }
        } else {
            for (const Item& it : items) {
                tmpSamples.resize(it.n);
                r.readExact(it.src, tmpSamples.data(), 2 * it.n);
                for (uint64_t k = 0; k < it.n; ++k) buf[(it.dst + k) * nch + it.j] = tmpSamples[k];
            }
        }
        fo.write(reinterpret_cast<const char*>(buf.data()),   // int16 buffer → bytes
                 static_cast<std::streamsize>(buf.size() * sizeof(int16_t)));
        if (!fo) throw std::runtime_error("write error on " + tmp.string());
        const double pct = 100.0 * static_cast<double>(b) / static_cast<double>(L.nSamples);
        if (pct >= nextReport) {
            std::cerr << "  writing: " << std::fixed << std::setprecision(1) << pct << " %\n";
            nextReport = pct + 5.0;
        }
    }
    fo.close();
    if (!fo) throw std::runtime_error("write error on " + tmp.string());
    fs::rename(tmp, out);
}

// ─────────────────────────────────────────────────────────────────────────────
// Metadata
// ─────────────────────────────────────────────────────────────────────────────

string nowIso() {
    const std::time_t t = std::time(nullptr);
    std::tm lt{};
#if defined(_WIN32)
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &lt);
    return buf;
}

Value buildMetadata(const Recording& rec, const string& stream, const Layout& L,
                    const fs::path& input, const string& datName) {
    List spk, evt, chans, streamsList, gaps;
    vector<string> countFindings;
    for (size_t i = 0; i < rec.spikeChannels.size(); ++i) {
        Map d = rec.spikeChannels[i];
        Map uc;
        uint64_t total = 0;
        for (const auto& [u, n] : rec.unitCounts[i]) {
            uc.push_back({std::to_string(u), n});
            total += n;
        }
        const Value recording = mapGet(d, "recording");
        if (const Value hu = mapGet(d, "headerUnitCounts");
            std::holds_alternative<std::shared_ptr<Map>>(hu.v)) {
            uint64_t hdr = 0;
            for (const auto& kv : *std::get<std::shared_ptr<Map>>(hu.v))
                hdr += std::get<uint64_t>(kv.second.v);
            if (hdr && hdr != total)
                countFindings.push_back(std::get<string>(mapGet(d, "name").v) + ": header counts " +
                                        std::to_string(hdr) + " spikes, records hold " +
                                        std::to_string(total));
        }
        if (!total && std::holds_alternative<bool>(recording.v) && !std::get<bool>(recording.v)) continue;
        d.push_back({"nSpikes", total});
        d.push_back({"unitCounts", uc});
        spk.push_back(std::move(d));
    }
    for (size_t i = 0; i < rec.eventChannels.size(); ++i) {
        if (!rec.eventCounts[i]) continue;
        Map d = rec.eventChannels[i];
        d.push_back({"nEvents", rec.eventCounts[i]});
        evt.push_back(std::move(d));
    }
    for (size_t j = 0; j < L.chans.size(); ++j) {
        const AnalogChannel& c = rec.analog[L.chans[j]];
        Map d{{"datChannel", static_cast<uint64_t>(j)}, {"name", c.name}, {"rate_hz", c.rate},
              {"uvPerCount", round9(c.uvPerCount)}, {"enabled", c.enabled},
              {"hasData", c.nBlocks > 0}, {"nSamples", c.nSamples}};
        for (const auto& kv : c.meta) d.push_back(kv);
        chans.push_back(std::move(d));
    }
    for (const auto& [k, idx] : streamsOf(rec)) {
        std::set<double> rates, uvs;
        uint64_t live = 0;
        for (size_t i : idx) {
            rates.insert(rec.analog[i].rate);
            uvs.insert(round9(rec.analog[i].uvPerCount));
            live += rec.analog[i].nBlocks > 0;
        }
        List rl, ul;
        for (double x : rates) rl.push_back(x);
        for (double x : uvs) ul.push_back(x);
        streamsList.push_back(Map{{"name", k}, {"nChannels", static_cast<uint64_t>(idx.size())},
                                  {"nWithData", live}, {"rate_hz", rl}, {"uvPerCount", ul},
                                  {"exported", k == stream}});
    }
    constexpr size_t kMaxGaps = 1000;
    for (size_t k = 1; k < L.segments.size() && gaps.size() < kMaxGaps; ++k) {
        const uint64_t after = L.segments[k - 1].first + L.segments[k - 1].second;
        gaps.push_back(Map{{"afterSample", after}, {"nSamples", L.segments[k].first - after},
                           {"start_s", round9(static_cast<double>(after) / L.rate)}});
    }

    // acquisitionSystem for the session YAML: µV/count = voltageRange·1e6 / (2^16 · amplification)
    std::set<double> uvs;
    for (size_t i : L.chans)
        if (rec.analog[i].nBlocks && rec.analog[i].uvPerCount > 0) uvs.insert(round9(rec.analog[i].uvPerCount));
    vector<string> findings = rec.findings;
    findings.insert(findings.end(), countFindings.begin(), countFindings.end());
    findings.insert(findings.end(), L.warnings.begin(), L.warnings.end());
    Map acq{{"nBits", 16}, {"nChannels", static_cast<uint64_t>(L.chans.size())},
            {"samplingRate", L.rate}, {"voltageRange", 20}};
    if (uvs.size() == 1) {
        const double uv = *uvs.begin();
        acq.push_back({"amplification", std::max<int64_t>(1, std::llround(20e6 / (65536.0 * uv)))});
        acq.push_back({"offset", 0});
        acq.push_back({"bitResolution_uV", uv});
    } else {
        acq.push_back({"amplification", 1000});
        acq.push_back({"offset", 0});
        if (uvs.size() > 1)
            findings.push_back("exported channels have different µV/count; acquisitionSystem gain is "
                               "not uniform — use sourceRecording.export.channels[].uvPerCount");
    }
    acq.push_back({"nSamples", L.nSamples});
    acq.push_back({"duration_s", round9(static_cast<double>(L.nSamples) / L.rate)});

    Map ex{{"datFile", datName},
           {"stream", stream},
           {"nChannels", static_cast<uint64_t>(L.chans.size())},
           {"samplingRate_hz", L.rate},
           {"ticksPerSample", L.tps},
           {"firstSampleTimestamp_ticks", L.t0},
           {"firstSample_s", round9(static_cast<double>(L.t0) / rec.clock)},
           {"nSamples", L.nSamples},
           {"duration_s", round9(static_cast<double>(L.nSamples) / L.rate)},
           {"gapFill", "zero"},
           {"nSegments", static_cast<uint64_t>(L.segments.size())},
           {"gaps", gaps}};
    if (L.segments.size() - 1 > kMaxGaps)
        ex.push_back({"gapsTruncated", static_cast<uint64_t>(L.segments.size() - 1)});
    ex.push_back({"valueNote", "raw int16 counts as stored by Plexon; uV = count x channels[].uvPerCount"});
    ex.push_back({"acquisitionSystem", acq});
    ex.push_back({"channels", chans});

    std::error_code ec;
    const auto fsize = fs::file_size(input, ec);
    Map meta{{"format", "plexon-" + rec.container},
             {"file", input.filename().string()},
             {"fileSize_bytes", static_cast<uint64_t>(ec ? 0 : fsize)},
             {"converter", "process_plxconvert v" + std::to_string(kConverterVersion)},
             {"convertedAt", nowIso()},
             {"header", rec.header},
             {"export", ex},
             {"streams", streamsList},
             {"spikeChannels", spk},
             {"eventChannels", evt}};
    if (!rec.deviceSettings.empty())
        meta.push_back({"deviceSettings", rec.deviceSettings});
    if (!findings.empty()) {
        List fl;
        for (auto& f : findings) fl.push_back(f);
        meta.push_back({"findings", fl});
    }
    return Map{{"sourceRecording", meta}};
}

void writeMetadata(const fs::path& path, const Value& doc) {
    const fs::path tmp = path.string() + ".nstmp";
    {
        std::ofstream o(tmp, std::ios::trunc);
        if (!o) throw std::runtime_error("cannot create " + tmp.string());
        o << "# Plexon source metadata — written by process_plxconvert\n";
        emit(o, doc, 0);
        if (!o) throw std::runtime_error("write error on " + tmp.string());
    }
    fs::rename(tmp, path);
}

// ─────────────────────────────────────────────────────────────────────────────
// CLI
// ─────────────────────────────────────────────────────────────────────────────

void summarize(const Recording& rec) {
    std::cout << "  Container      : " << (rec.container == "plx" ? "PLX" : "PL2");
    if (rec.container == "plx") std::cout << " v" << scalar(mapGet(rec.header, "version"));
    const Value app = mapGet(rec.header, rec.container == "plx" ? "acquiringSoftware" : "application");
    if (auto s = std::get_if<string>(&app.v); s && !s->empty()) {
        std::cout << "  (" << *s;
        const Value ver = mapGet(rec.header, "applicationVersion");
        if (auto v = std::get_if<string>(&ver.v)) std::cout << " " << *v;
        std::cout << ")";
    }
    std::cout << "\n  Recorded at    : " << (rec.recordedAt.empty() ? "unknown" : rec.recordedAt)
              << "\n  Clock          : " << scalar(Value(rec.clock)) << " Hz\n  Streams        :\n";
    for (const auto& [k, idx] : streamsOf(rec)) {
        uint64_t live = 0, ns = 0;
        std::set<double> rates, uvs;
        for (size_t i : idx) {
            live += rec.analog[i].nBlocks > 0;
            ns = std::max(ns, rec.analog[i].nSamples);
            rates.insert(rec.analog[i].rate);
            uvs.insert(round9(rec.analog[i].uvPerCount));
        }
        string rs, us;
        for (double x : rates) rs += (rs.empty() ? "" : "/") + scalar(Value(x));
        for (double x : uvs) us += (us.empty() ? "" : "/") + scalar(Value(x));
        std::cout << "    " << std::left << std::setw(6) << k << std::right << std::setw(4) << live
                  << "/" << std::left << std::setw(4) << idx.size() << std::right << "ch "
                  << std::setw(9) << rs << " Hz " << std::setw(13) << ns << " samples  "
                  << us << " uV/count\n";
    }
    uint64_t nsp = 0, csp = 0, nev = 0, cev = 0;
    for (const auto& uc : rec.unitCounts) {
        uint64_t t = 0;
        for (const auto& kv : uc) t += kv.second;
        nsp += t;
        csp += t > 0;
    }
    for (uint64_t n : rec.eventCounts) { nev += n; cev += n > 0; }
    std::cout << "  Spike channels : " << csp << " with spikes, " << nsp << " spikes\n"
              << "  Event channels : " << cev << " with events, " << nev << " events\n";
    for (const auto& f : rec.findings) std::cout << "  ! " << f << "\n";
}

void usage(std::ostream& o) {
    o << "usage: process_plxconvert [options] input.{plx,pl2} output_basename\n"
         "  -s, --stream PREFIX    continuous stream to export (default WB, else fastest)\n"
         "  -C, --channels SPEC    1-based indices / ranges / names within the stream\n"
         "  -k, --keep-empty B     keep stream channels without data as zeros (default 1)\n"
         "  -z, --pad-to-zero B    start the .dat at tick 0 (default 0)\n"
         "  -c, --chunk N          output samples per write chunk (default 2^21 / nChannels)\n"
         "  -m, --meta FILE        metadata output (default output_basename.plexon.yaml)\n"
         "  -f, --force            overwrite an existing output_basename.dat\n"
         "  -d, --dry-run          print the file summary and exit\n"
         "  -h, --help             display this help\n";
}

bool parseBool(const string& s) {
    string v;
    for (char c : s) v += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off" || v.empty()) return false;
    throw std::invalid_argument("not a boolean: " + s);
}

}  // namespace

int main(int argc, char** argv) {
    string stream, channelSpec, metaPath;
    bool keepEmpty = true, padToZero = false, force = false, dryRun = false;
    uint64_t chunk = 0;   // 0 = auto: ~4 MB of output per chunk
    vector<string> pos;
    try {
        for (int i = 1; i < argc; ++i) {
            const string a = argv[i];
            auto next = [&]() -> string {
                if (i + 1 >= argc) throw std::invalid_argument("missing value for " + a);
                return argv[++i];
            };
            if (a == "-s" || a == "--stream") stream = next();
            else if (a == "-C" || a == "--channels") channelSpec = next();
            else if (a == "-k" || a == "--keep-empty") keepEmpty = parseBool(next());
            else if (a == "-z" || a == "--pad-to-zero") padToZero = parseBool(next());
            else if (a == "-c" || a == "--chunk") chunk = std::max<uint64_t>(1, std::stoull(next()));
            else if (a == "-m" || a == "--meta") metaPath = next();
            else if (a == "-f" || a == "--force") force = true;
            else if (a == "-d" || a == "--dry-run") dryRun = true;
            else if (a == "-h" || a == "--help") { usage(std::cout); return 0; }
            else if (!a.empty() && a[0] == '-') throw std::invalid_argument("unknown option " + a);
            else pos.push_back(a);
        }
        if (pos.empty() || (!dryRun && pos.size() != 2) || pos.size() > 2)
            throw std::invalid_argument("expected input file and output basename");
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        usage(std::cerr);
        return 2;
    }

    try {
        const fs::path input = pos[0];
        if (!fs::is_regular_file(input)) {
            std::cerr << "error: input '" << input.string() << "' not found\n";
            return 2;
        }
        Reader r(input);
        Recording rec;
        uint8_t head[16] = {};
        if (r.size() >= 16) std::memcpy(head, r.at(0, 16), 16);
        const bool plx = r.size() >= 16 && std::memcmp(head, "PLEX", 4) == 0;
        if (plx) readPlxHeaders(r, rec);
        else if (r.size() >= 16 && looksLikePl2(head)) readPl2Headers(r, rec);
        else throw std::runtime_error(input.string() + ": neither a PLX nor a PL2 file");
        rec.unitCounts.assign(rec.spikeChannels.size(), {});
        rec.eventCounts.assign(rec.eventChannels.size(), 0);

        // Choose the stream before the walk so only its blocks are indexed.
        const auto streams = streamsOf(rec);
        vector<string> candidates;
        if (!stream.empty()) {
            string up;
            for (char c : stream) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (!streams.count(up)) {
                string names;
                for (const auto& kv : streams) names += (names.empty() ? "" : ", ") + kv.first;
                throw std::runtime_error("stream '" + stream + "' not found; streams: " +
                                         (names.empty() ? "none" : names));
            }
            candidates = {up};
        } else {
            candidates = streamCandidates(rec);
        }
        string chosen = dryRun || candidates.empty() ? string() : candidates.front();
        auto walk = [&]() {
            for (auto& c : rec.analog) { c.nSamples = 0; c.nBlocks = 0; c.blocks.clear(); }
            for (auto& uc : rec.unitCounts) uc.clear();
            std::fill(rec.eventCounts.begin(), rec.eventCounts.end(), 0);
            rec.findings.clear();
            if (rec.container == "plx") walkPlx(r, rec);
            else walkPl2(r, rec);
        };
        for (auto& c : rec.analog) c.indexed = !chosen.empty() && streamPrefix(c.name) == chosen;
        const Map headerBeforeWalk = rec.header;
        walk();
        summarize(rec);
        if (dryRun) return 0;

        // An auto-chosen stream whose headers looked live but holds no data:
        // fall back to the best stream that does (needs a second walk).
        auto hasData = [&](const string& k) {
            if (!streams.count(k)) return false;
            for (size_t i : streams.at(k)) if (rec.analog[i].nBlocks) return true;
            return false;
        };
        if (chosen.empty() || !hasData(chosen)) {
            if (!stream.empty()) throw std::runtime_error("stream '" + stream + "' has no recorded data");
            string next;
            for (const auto& [k, v] : streams) {
                if (!hasData(k)) continue;
                if (next.empty()) { next = k; continue; }
                auto rate = [&](const string& s) {
                    double m = 0;
                    for (size_t i : streams.at(s)) m = std::max(m, rec.analog[i].rate);
                    return m;
                };
                if (k == "WB" || (next != "WB" && rate(k) > rate(next))) next = k;
            }
            if (next.empty()) throw std::runtime_error("the file holds no continuous data");
            std::cout << "  (stream " << (chosen.empty() ? "?" : chosen) << " holds no data; using "
                      << next << ")\n";
            chosen = next;
            for (auto& c : rec.analog) c.indexed = streamPrefix(c.name) == chosen;
            rec.header = headerBeforeWalk;
            walk();
        }

        const vector<size_t> sel = parseChannelSpec(channelSpec, streams.at(chosen), rec);
        Layout L = buildLayout(rec, sel, keepEmpty, padToZero);
        const fs::path dat = pos[1] + ".dat";
        if (fs::exists(dat) && !force) {
            std::cerr << "error: " << dat.string() << " exists (use --force)\n";
            return 3;
        }
        std::cout << "  Exporting " << chosen << ": " << L.chans.size() << " channels @ "
                  << scalar(Value(L.rate)) << " Hz, " << L.nSamples << " samples ("
                  << std::fixed << std::setprecision(2) << static_cast<double>(L.nSamples) / L.rate
                  << " s), " << L.segments.size() << " segment(s)\n";
        for (const auto& w : L.warnings) std::cout << "  ! " << w << "\n";
        if (chunk == 0) chunk = std::max<uint64_t>(4096, (1ull << 21) / std::max<size_t>(L.chans.size(), 1));
        writeDat(r, rec, L, dat, chunk);
        const fs::path meta = metaPath.empty() ? fs::path(pos[1] + ".plexon.yaml") : fs::path(metaPath);
        writeMetadata(meta, buildMetadata(rec, chosen, L, input, dat.filename().string()));
        std::cout << "  wrote " << dat.string() << "\n  metadata: " << meta.string() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
