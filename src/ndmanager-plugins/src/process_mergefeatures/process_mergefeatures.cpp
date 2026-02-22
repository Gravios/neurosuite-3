/***************************************************************************
 * process_mergefeatures.cpp
 *
 * Merges binary PCA feature output (from process_pca) with a binary .res
 * file (from process_extractspikes) to produce a final binary .fet file.
 *
 * Called by ndm_pca to replace the bash paste/tail/cat pipeline.
 *
 * Usage:
 *   process_mergefeatures <features.tmp> <spikes.res.N> <output.fet.N>
 *
 * Input formats:
 *   features.tmp:
 *     int32_t  nFeatureCols          (PCA cols, no timestamp)
 *     nSpikes * nFeatureCols * int64_t  (row-major)
 *
 *   spikes.res.N:
 *     nSpikes * int64_t              (timestamps, no header)
 *
 * Output format (binary .fet):
 *   int32_t  nDimensions             (= nFeatureCols + 1)
 *   nSpikes * nDimensions * int64_t  (PCA cols then timestamp, row-major)
 *
 ***************************************************************************/

#define _LARGEFILE_SOURCE64
#define _FILE_OFFSET_BITS 64

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void die(const char* msg) {
    fprintf(stderr, "process_mergefeatures: %s\n", msg);
    exit(1);
}

int main(int argc, char* argv[])
{
    if (argc != 4) {
        fprintf(stderr,
            "usage: process_mergefeatures <features.tmp> <res.N> <fet.N>\n");
        return 1;
    }

    const char* featPath = argv[1];
    const char* resPath  = argv[2];
    const char* fetPath  = argv[3];

    // ── Read feature file ─────────────────────────────────────────────────
    FILE* ff = fopen(featPath, "rb");
    if (!ff) die("cannot open features.tmp");

    int32_t nFeatCols = 0;
    if (fread(&nFeatCols, sizeof(int32_t), 1, ff) != 1 || nFeatCols <= 0)
        die("bad features.tmp header");

    // Determine spike count from file size
    fseeko(ff, 0, SEEK_END);
    int64_t featBytes = (int64_t)ftello(ff) - (int64_t)sizeof(int32_t);
    fseeko(ff, sizeof(int32_t), SEEK_SET);

    int64_t nSpikes = featBytes / ((int64_t)sizeof(int64_t) * nFeatCols);
    if (nSpikes * (int64_t)sizeof(int64_t) * nFeatCols != featBytes)
        die("features.tmp size not a multiple of nFeatCols * 8");

    std::vector<int64_t> feats((size_t)(nSpikes * nFeatCols));
    if ((int64_t)fread(feats.data(), sizeof(int64_t),
                       (size_t)(nSpikes * nFeatCols), ff)
        != nSpikes * nFeatCols)
        die("short read in features.tmp");
    fclose(ff);

    // ── Read .res file ────────────────────────────────────────────────────
    FILE* rf = fopen(resPath, "rb");
    if (!rf) die("cannot open .res file");

    fseeko(rf, 0, SEEK_END);
    int64_t resBytes = (int64_t)ftello(rf);
    rewind(rf);

    int64_t nResSpikes = resBytes / (int64_t)sizeof(int64_t);
    if (nResSpikes != nSpikes) {
        fprintf(stderr,
            "process_mergefeatures: spike count mismatch: "
            "features=%lld  res=%lld\n",
            (long long)nSpikes, (long long)nResSpikes);
        fclose(rf);
        return 1;
    }

    std::vector<int64_t> timestamps((size_t)nSpikes);
    if ((int64_t)fread(timestamps.data(), sizeof(int64_t),
                       (size_t)nSpikes, rf) != nSpikes)
        die("short read in .res file");
    fclose(rf);

    // ── Write .fet file ───────────────────────────────────────────────────
    FILE* wf = fopen(fetPath, "wb");
    if (!wf) die("cannot open output .fet for writing");

    int32_t nDim = nFeatCols + 1;  // feature cols + timestamp col
    fwrite(&nDim, sizeof(int32_t), 1, wf);

    for (int64_t k = 0; k < nSpikes; ++k) {
        // Feature columns
        const int64_t* row = feats.data() + k * nFeatCols;
        fwrite(row, sizeof(int64_t), (size_t)nFeatCols, wf);
        // Timestamp as last column
        fwrite(&timestamps[(size_t)k], sizeof(int64_t), 1, wf);
    }
    fclose(wf);

    return 0;
}
