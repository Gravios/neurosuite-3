/***************************************************************************
                          process_pca.h  -  description
                             -------------------
    copyright            : (C) 2007 by Nicolas Lebas
    email                : nicolas.lebas@college-de-france.fr


 * Description : process_pca is a program which allow us to reduce spikes 
 * dimensions by computing a PCA.
 *
 * Usage : process_pca [options] [input]
 *
 ***************************************************************************/

/***************************************************************************
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 ***************************************************************************/
#ifndef __PROCESS_PCAX_H
#define __PROCESS_PCAX_H

const unsigned long MAX_INPUT_SIZE = 2560000000; // = 128*2*25*400000
const int RECORD_BYTE_SIZE = 2;

#include <iostream>
using namespace std;


// Structure for all arguments
struct arguments {
	char *inputFileName; // Input file name
	char *outputFileName; // Ouput file name
	long long inputSize; // Size of the input (byte)
	int nChannels; // Total number of channels
	int beforeSpike; // number of records to consider before spike
	int afterSpike; // number of records to consider after spike
	int peakPosition; // peak position within the waveform
	int spikeLength; // number of records in a spike
	int nComponents; // number of principal components
	bool isCenteredData; // use or not centered data for the projection
	int offset; // Offset at the beginning of the datas
	
	bool isInputFileProvided;
	bool isOutputFileProvided;
	bool isInputSizeProvided;
	bool isNChannelsProvided;
	bool isBeforeSpikeProvided;
	bool isAfterSpikeProvided;
	bool isPeakPositionProvided;
	bool isSpikeLengthProvided;
	bool isNComponentsProvided;
	bool isExtraFeaturesProvided;
	bool isOffsetProvided;

	// Optional electrode-group number for the progress bar label.
	// When set (>= 0), the bar shows "[PCA-N]" instead of "[PCA]" so
	// users running the wrapper script with parallel groups can tell
	// which group's bar is which.
	int  electrodeGroup;

	// ─── Subset PCA (interneuron / high-rate exclusion) ─────────────────
	// When set, the PCA basis is fit ONLY on spikes whose .clu cluster
	// id is NOT in excludeClusterIds.  All spikes are still projected
	// through the resulting eigenvectors, so the output .fet has one
	// row per spike — the basis just isn't biased toward the high-rate
	// clusters.  Typical use: drop fast-spiking interneurons (which
	// dominate the row count and pull eigenvectors toward whatever
	// distinguishes their fast waveforms) so the basis better separates
	// neighbouring pyramidal cells.  See -l / -e command-line flags.
	const char *cluFileName;          ///< -l: .clu.<g> path; nSpikes int32 ids prefixed by int32 nClusters header
	bool        isCluFileProvided;
	const char *excludeClustersStr;   ///< -e: raw arg string, e.g. "0,1,5,12"
	bool        isExcludeClustersProvided;

	// ─── Varimax rotation (patch52) ─────────────────────────────────────
	// Optional orthogonal rotation of the per-channel kept-K eigenvectors
	// that maximises the variance of the squared loadings within each
	// component.  Result: each rotated component tends to "localise" onto
	// a small set of time samples rather than mixing energy across the
	// full waveform.  Same total explained variance (it's an orthogonal
	// rotation within the kept subspace), better cluster geometry for
	// hand sorting.  See README for details.
	bool   varimax;                   ///< --varimax: 0/1, default 0
	int    varimaxMaxIter;            ///< --varimax-max-iter: default 30
	double varimaxTol;                ///< --varimax-tol: change in criterion to call convergence, default 1e-6

	int    pcaMethod;                 ///< --pca-method: neurosuite::core::Method (0..6) tagged into the PCAE basis; -1 => infer from filename
	bool   isPcaMethodProvided;
};


// Parse arguments and fill corresponding variables
void parseArgs(const int, char **, arguments &);
// Check arguments values
bool checkInputs(const arguments);

#endif
