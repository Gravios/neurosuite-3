/***************************************************************************
 *  process_refeaturize_stderiv.cpp
 *
 *  Recompute PCA features for a subset of spikes using a model built by
 *  ndm_pca_stderiv.
 *
 *  Since ndm_extractspikes_stderiv writes spatial+temporal derivative
 *  waveforms directly into .spk, this tool simply projects those waveforms
 *  onto the eigenvectors stored in the .pca.N model — no transformation is
 *  applied here.
 *
 *  The .pca.N model has nChannels = rawChannels-1 for sdiff orders 1 and 3
 *  (one linearly dependent channel dropped during training).  Pass -n equal
 *  to the channel count in the .spk file, which must equal model.nChannels.
 *
 *  Usage:
 *    process_refeaturize_stderiv -p model.pca.N -w waveformLen -n nChannels
 *                                [-i indices] [-x] spikes.spk.N
 *
 *  Options (identical to process_refeaturize):
 *    -p  .pca.N file, mandatory
 *    -w  samples per channel per waveform in .spk, mandatory
 *    -n  channels per waveform in .spk (= rawChan-1 for orders 1,3), mandatory
 *    -i  text file of 0-based spike indices (default: all)
 *    -x  include per-channel peak values
 *    -v  verbose
 *
 *  Copyright (C) 2025 Gravios / NeuroSuite-3 contributors
 *  License: GPL v3+
 ***************************************************************************/

#define _LARGEFILE_SOURCE64
#define _FILE_OFFSET_BITS 64

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cerrno>
#include <iostream>
#include <fstream>
#include <vector>
#include <neurosuite/core/pca_projection.hpp> // canonical PCAE loader (block-wise)

static const char *VERSION = "process_refeaturize_stderiv 1.0";

static void usage(const char *name)
{
    std::cerr << VERSION << "\n"
        << "usage: " << name
        << " -p model.pca.N -w waveformLen -n nChannels [-i idx] [-x] spk\n"
        << "  -p  .pca.N file from ndm_pca_stderiv, mandatory\n"
        << "  -w  samples per channel in .spk, mandatory\n"
        << "  -n  channels per waveform (= rawChan-1 for sdiff orders 1 or 3)\n"
        << "  -i  text file of 0-based spike indices (default: all)\n"
        << "  -x  include per-channel peak values\n"
        << "  -v  verbose\n";
    std::exit(1);
}

struct PcaModel {
    int nChannels, data2use, nComponents, recShift;
    bool isCentered;
    std::vector<std::vector<double>> mean;
    std::vector<std::vector<double>> eigvec;
};

static bool loadPcaModel(const char *path, PcaModel &m)
{
    // Canonical PCAE loader.  Fixes the same interleave bug as process_refeaturize:
    // the inline reader parsed the PCAE header but read the body interleaved
    // (mean[ch] then evec[ch] per channel) instead of block-wise (all means, then
    // all eigenvectors), scrambling every channel past the first.  core also reads
    // v1 and v2 (the inline reader rejected v2).
    neurosuite::core::PcaBasis basis;
    if (!neurosuite::core::loadPca(path, basis)) {
        std::cerr << "error: cannot load PCAE pca file '" << path << "'\n";
        return false;
    }
    m.nChannels   = basis.nCh;
    m.data2use    = basis.data2use;
    m.nComponents = basis.nComp;
    m.recShift    = basis.recShift;
    m.isCentered  = basis.centered;
    m.mean        = basis.means;   // [ch][data2use]
    m.eigvec      = basis.evec;    // [ch][data2use * nComp], col-major
    return true;
}

int main(int argc, char *argv[])
{
    const char *pcaPath=nullptr, *spkPath=nullptr, *idxPath=nullptr;
    int waveformLength=-1, nChannels=-1;
    bool extraFeatures=false, verbose=false;

    for (int i=1; i<argc; i++) {
        if      (!strcmp(argv[i],"-p")&&i+1<argc) pcaPath       =argv[++i];
        else if (!strcmp(argv[i],"-w")&&i+1<argc) waveformLength=atoi(argv[++i]);
        else if (!strcmp(argv[i],"-n")&&i+1<argc) nChannels     =atoi(argv[++i]);
        else if (!strcmp(argv[i],"-i")&&i+1<argc) idxPath       =argv[++i];
        else if (!strcmp(argv[i],"-x"))            extraFeatures =true;
        else if (!strcmp(argv[i],"-v"))            verbose       =true;
        else if (argv[i][0]!='-')                  spkPath       =argv[i];
        else { std::cerr<<"unknown option: "<<argv[i]<<"\n"; usage(argv[0]); }
    }
    if (!pcaPath||!spkPath||waveformLength<1||nChannels<1) {
        std::cerr<<"error: missing required arguments\n"; usage(argv[0]);
    }

    PcaModel model;
    if (!loadPcaModel(pcaPath,model)) return 1;
    if (model.nChannels != nChannels) {
        std::cerr << "error: pca model has " << model.nChannels
                  << " channels, -n says " << nChannels
                  << " (for stderiv orders 1/3, pass nRawChannels-1)\n";
        return 1;
    }

    FILE *spkFile=fopen(spkPath,"rb");
    if (!spkFile) { std::cerr<<"error: cannot open '"<<spkPath<<"'\n"; return 1; }

    fseeko(spkFile,0,SEEK_END);
    long long spkSize=static_cast<long long>(ftello(spkFile));
    rewind(spkFile);

    long long bytesPerSpike=static_cast<long long>(nChannels)*waveformLength*2;
    if (spkSize%bytesPerSpike!=0) {
        std::cerr<<"error: spike file size not multiple of bytes-per-spike\n";
        fclose(spkFile); return 1;
    }
    long long nSpikes=spkSize/bytesPerSpike;

    std::vector<long long> indices;
    if (idxPath) {
        std::ifstream idxFile(idxPath);
        if (!idxFile) { std::cerr<<"error: cannot open '"<<idxPath<<"'\n"; return 1; }
        long long idx;
        while (idxFile>>idx) indices.push_back(idx);
    } else {
        indices.resize(static_cast<size_t>(nSpikes));
        for (long long i=0; i<nSpikes; ++i) indices[static_cast<size_t>(i)]=i;
    }

    if (verbose)
        std::cerr << "channels=" << nChannels << " spikes=" << nSpikes
                  << " projecting=" << indices.size() << "\n";

    std::vector<short>  waveform(static_cast<size_t>(nChannels*waveformLength));
    std::vector<double> proj(static_cast<size_t>(model.nChannels*model.nComponents));
    std::vector<short>  peaks(static_cast<size_t>(model.nChannels));

    for (long long idx : indices) {
        if (idx<0||idx>=nSpikes) {
            std::cerr<<"warning: index "<<idx<<" out of range, skipping\n";
            continue;
        }
        off_t offset=static_cast<off_t>(idx)*static_cast<off_t>(bytesPerSpike);
        if (fseeko(spkFile,offset,SEEK_SET)!=0) {
            std::cerr<<"error: seek failed for spike "<<idx<<"\n"; continue;
        }
        size_t sps=static_cast<size_t>(nChannels*waveformLength);
        if (fread(waveform.data(),2,sps,spkFile)!=sps) {
            std::cerr<<"error: read failed for spike "<<idx<<"\n"; continue;
        }

        // Project directly — .spk already contains stderiv waveforms.
        for (int ch=0; ch<model.nChannels; ++ch) {
            const auto &mu=model.mean[static_cast<size_t>(ch)];
            const auto &ev=model.eigvec[static_cast<size_t>(ch)];
            short peak=0;
            for (int k=0; k<model.nComponents; k++) {
                double val=0.0;
                for (int s=0; s<model.data2use; s++) {
                    int si=model.recShift+s;
                    double x=static_cast<double>(waveform[si*nChannels+ch]);
                    if (model.isCentered) x-=mu[static_cast<size_t>(s)];
                    val+=ev[static_cast<size_t>(s*model.nComponents+k)]*x;
                    if (extraFeatures) {
                        short v=waveform[si*nChannels+ch];
                        if (abs((int)v)>abs((int)peak)) peak=v;
                    }
                }
                proj[static_cast<size_t>(ch*model.nComponents+k)]=val;
            }
            peaks[static_cast<size_t>(ch)]=peak;
        }

        for (int ch=0; ch<model.nChannels; ch++)
            for (int k=0; k<model.nComponents; k++)
                printf("%d ",static_cast<int>(
                    proj[static_cast<size_t>(ch*model.nComponents+k)]));
        if (extraFeatures)
            for (int ch=0; ch<model.nChannels; ch++)
                printf("%d ",static_cast<int>(peaks[static_cast<size_t>(ch)]));
        printf("\n");
    }

    fclose(spkFile);
    return 0;
}
