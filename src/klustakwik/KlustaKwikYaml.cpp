/***************************************************************************
 * KlustaKwikYaml.cpp
 *
 * See KlustaKwikYaml.h for documentation.
 ***************************************************************************/

#include "KlustaKwikYaml.h"

#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Internal helper: try to open <base><ext>, return "" on failure.
// ---------------------------------------------------------------------------
static std::string tryPath(const char* base, const char* ext)
{
    std::string p = std::string(base) + ext;
    FILE* f = fopen(p.c_str(), "r");
    if (f) { fclose(f); return p; }
    return {};
}

// ---------------------------------------------------------------------------
// kkReadYamlSpikeParams
// ---------------------------------------------------------------------------
KKYamlSpikeParams kkReadYamlSpikeParams(const char* fileBase, int elecNo)
{
    KKYamlSpikeParams out;

    // Locate the YAML file: prefer .yaml, then .yml
    std::string path = tryPath(fileBase, ".yaml");
    if (path.empty()) path = tryPath(fileBase, ".yml");
    if (path.empty()) {
        // No YAML present — silent, not an error; XML-only setups are fine.
        return out;
    }

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        fprintf(stderr,
                "KlustaKwik: warning — failed to parse '%s': %s\n",
                path.c_str(), e.what());
        return out;
    }

    out.valid = true;

    // ── acquisitionSystem ────────────────────────────────────────────────
    try {
        const auto& acq = root["acquisitionSystem"];
        if (acq && acq.IsMap()) {
            if (acq["samplingRate"]) out.samplingRate = acq["samplingRate"].as<double>(0.0);
            if (acq["nBits"])        out.nBits        = acq["nBits"].as<int>(0);
        }
    } catch (...) {}   // missing / wrong-type fields → keep defaults

    // Data-type check: nBits must be ≤16 for the hardcoded int16 pipeline.
    // 32-bit ADCs are uncommon and would require a custom extractspikes build.
    if (out.nBits > 0 && out.nBits != 16) {
        if (out.nBits == 32) {
            fprintf(stderr,
                    "KlustaKwik: note — acquisitionSystem.nBits=%d in '%s'.\n"
                    "  The ndmanager pipeline (process_medianfilter, process_extractspikes)\n"
                    "  always stores .spk waveforms as int16 regardless of ADC resolution.\n"
                    "  NbBytesPerSample is therefore 2. If your .spk files were produced\n"
                    "  by a custom 32-bit extractor, pass -NbBytesPerSample 4 explicitly.\n",
                    out.nBits, path.c_str());
        } else {
            // 12/14-bit ADCs: values are stored as int16, lower bits unused.
            // Completely normal; no action needed.
        }
    }

    // ── spikeDetection ───────────────────────────────────────────────────
    try {
        const auto& sd = root["spikeDetection"];
        if (!sd || !sd.IsMap()) return out;
        const auto& groups = sd["channelGroups"];
        if (!groups || !groups.IsSequence()) return out;

        const int idx = elecNo - 1;  // YAML list is 0-based; ElecNo is 1-based
        if (idx < 0 || idx >= static_cast<int>(groups.size())) {
            fprintf(stderr,
                    "KlustaKwik: warning — ElecNo=%d but '%s' has only %d "
                    "spikeDetection group(s)\n",
                    elecNo, path.c_str(), static_cast<int>(groups.size()));
            return out;
        }

        const auto& grp = groups[idx];
        if (!grp || !grp.IsMap()) return out;

        // channels: count the channel IDs to obtain NbChannels.
        //
        // ndmanager's YAML schema stores channels as a map with a "channel"
        // sub-sequence:
        //   channels:
        //     channel: [0, 1, 2, 3, 4, 5, 6, 7]
        //
        // Some hand-authored files use a flat sequence instead:
        //   channels: [0, 1, 2, 3, 4, 5, 6, 7]
        //
        // Both cases are handled here; either gives the correct count.
        if (grp["channels"]) {
            const auto& ch = grp["channels"];
            if (ch.IsSequence()) {
                // Flat list
                out.nbChannels = static_cast<int>(ch.size());
            } else if (ch.IsMap()) {
                // ndmanager schema: channels.channel is the sequence
                if (ch["channel"] && ch["channel"].IsSequence())
                    out.nbChannels = static_cast<int>(ch["channel"].size());
            }
        }

        // nSamples: scalar
        if (grp["nSamples"])
            out.nbSamples = grp["nSamples"].as<int>(0);

    } catch (const YAML::Exception& e) {
        fprintf(stderr,
                "KlustaKwik: warning — error reading spikeDetection from '%s': %s\n",
                path.c_str(), e.what());
    }

    return out;
}
