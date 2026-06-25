/***************************************************************************
 * convert_to_binary.cpp
 *
 * Standalone converter: text .res/.fet/.clu → binary format.
 *
 * Usage:
 *   convert_to_binary <basename> <group>
 *
 * Reads:  <basename>.res.<group>  <basename>.fet.<group>  <basename>.clu.<group>
 * Writes: same paths (originals backed up as .bak)
 *
 * Binary formats:
 *   .res  — N * int64_t  (no header)
 *   .fet  — int32_t nDimensions; N * nDimensions * int64_t  (row-major)
 *   .clu  — int32_t nClusters;   N * int32_t
 ***************************************************************************/

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

static bool convertRes(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path.c_str()); return false; }
    std::vector<int64_t> ts;
    int64_t v;
    while (fscanf(f, "%lld", (long long*)&v) == 1)
        ts.push_back(v);
    fclose(f);

    std::string bak = path + ".bak";
    rename(path.c_str(), bak.c_str());

    FILE* w = fopen(path.c_str(), "wb");
    if (!w) { fprintf(stderr, "Cannot write %s\n", path.c_str()); return false; }
    fwrite(ts.data(), sizeof(int64_t), ts.size(), w);
    fclose(w);
    printf("  .res: %zu timestamps\n", ts.size());
    return true;
}

static bool convertFet(const std::string& path, int& nDimOut, int64_t& nSpikesOut)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path.c_str()); return false; }

    int nDim = 0;
    if (fscanf(f, "%d", &nDim) != 1 || nDim <= 0) {
        fclose(f); fprintf(stderr, "Bad .fet header\n"); return false;
    }

    std::vector<int64_t> data;
    int64_t v;
    while (fscanf(f, "%lld", (long long*)&v) == 1)
        data.push_back(v);
    fclose(f);

    int64_t nSpikes = static_cast<int64_t>(data.size()) / nDim;
    if (nSpikes * nDim != static_cast<int64_t>(data.size())) {
        fprintf(stderr, "WARNING: .fet size %zu not divisible by nDim=%d\n",
                data.size(), nDim);
    }

    std::string bak = path + ".bak";
    rename(path.c_str(), bak.c_str());

    FILE* w = fopen(path.c_str(), "wb");
    if (!w) { fprintf(stderr, "Cannot write %s\n", path.c_str()); return false; }
    int32_t nDim32 = static_cast<int32_t>(nDim);
    fwrite(&nDim32, sizeof(int32_t), 1, w);
    fwrite(data.data(), sizeof(int64_t), data.size(), w);
    fclose(w);

    nDimOut = nDim;
    nSpikesOut = nSpikes;
    printf("  .fet: %lld spikes x %d dims\n", static_cast<long long>(nSpikes), nDim);
    return true;
}

static bool convertClu(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path.c_str()); return false; }

    int nClu = 0;
    if (fscanf(f, "%d", &nClu) != 1) {
        fclose(f); fprintf(stderr, "Bad .clu header\n"); return false;
    }

    std::vector<int32_t> ids;
    int v;
    while (fscanf(f, "%d", &v) == 1)
        ids.push_back(static_cast<int32_t>(v));
    fclose(f);

    std::string bak = path + ".bak";
    rename(path.c_str(), bak.c_str());

    FILE* w = fopen(path.c_str(), "wb");
    if (!w) { fprintf(stderr, "Cannot write %s\n", path.c_str()); return false; }
    int32_t nClu32 = static_cast<int32_t>(nClu);
    fwrite(&nClu32, sizeof(int32_t), 1, w);
    fwrite(ids.data(), sizeof(int32_t), ids.size(), w);
    fclose(w);

    printf("  .clu: %zu ids  nClusters=%d\n", ids.size(), nClu);
    return true;
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: convert_to_binary <basename> <group>\n");
        return 1;
    }
    std::string base(argv[1]);
    std::string grp(argv[2]);

    printf("Converting %s group %s ...\n", base.c_str(), grp.c_str());

    int nDim = 0;
    int64_t nSpikes = 0;

    bool ok = true;
    ok &= convertRes(base + ".res." + grp);
    ok &= convertFet(base + ".fet." + grp, nDim, nSpikes);

    std::string cluPath = base + ".clu." + grp;
    FILE* test = fopen(cluPath.c_str(), "r");
    if (test) {
        fclose(test);
        ok &= convertClu(cluPath);
    } else {
        printf("  .clu not found — skipping\n");
    }

    printf(ok ? "Done.\n" : "Completed with errors.\n");
    return ok ? 0 : 1;
}
