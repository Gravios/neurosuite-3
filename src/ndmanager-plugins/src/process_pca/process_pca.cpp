/***************************************************************************
 *   process_pca.cpp							   							*
 *									   										*
 *   Copyright (C) 2010 by Nicolas Lebas   				   					*
 *   nicolas.lebas@college-de-france.fr   				   					*
 *                                                                         	*
 * Description : process_pca compute a Principal Component Analysis (PCA) 	*
 * with spikes data							   								*
 *									   										*
 * Usage : process_pca [options] [input]				   					*
 *									   										*
 *   This program is free software; you can redistribute it and/or modify  	*
 *   it under the terms of the GNU General Public License as published by  	*
 *   the Free Software Foundation; either version 2 of the License, or     	*
 *   (at your option) any later version.                                   	*
 *                                                                         	*
 *   This program is distributed in the hope that it will be useful,       	*
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        	*
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         	*
 *   GNU General Public License for more details.                          	*
 *                                                                         	*
 *   You should have received a copy of the GNU General Public License     	*
 *   along with this program; if not, write to the                         	*
 *   Free Software Foundation, Inc.,                                       	*
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             	*
 *									   										*
 ***************************************************************************/
#include "process_pca.h"
#include "progressbar.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#include <gsl/gsl_statistics_double.h> // covariance calculation
#include <gsl/gsl_eigen.h> // find eigen values and vectors
#include <gsl/gsl_blas.h> // matrix operations
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <string>
using namespace std;


static bool verbose = false;
const char *programVersion = "process_pca 0.9.4 (28-11-2011)";

unsigned long int nRecords = 0; // number of records in all channels
unsigned long int nRecordsPerChannel = 0; // number of records for a channel


// ───────────────────────────────────────────────────────────────────────────
// Subset-PCA helpers
// ───────────────────────────────────────────────────────────────────────────
//
// parseExcludeList parses a comma-separated list of cluster ids ("0,1,5,12")
// into a sorted std::vector<int>.  Whitespace is tolerated.  Returns true on
// success, false on any parse error (with a message on stderr).
static bool parseExcludeList(const char *raw, std::vector<int> &out)
{
	out.clear();
	if (!raw || !*raw) {
		cerr << "error: -e: exclude list is empty." << endl;
		return false;
	}
	std::string buf;
	auto flush = [&](void) -> bool {
		if (buf.empty()) return true;
		// Trim
		size_t a = 0, b = buf.size();
		while (a < b && isspace((unsigned char)buf[a])) ++a;
		while (b > a && isspace((unsigned char)buf[b-1])) --b;
		if (a == b) { buf.clear(); return true; }
		std::string tok = buf.substr(a, b-a);
		buf.clear();
		// Validate: signed integer
		size_t i = 0;
		if (tok[0] == '+' || tok[0] == '-') i = 1;
		if (i >= tok.size()) {
			cerr << "error: -e: bad cluster id '" << tok << "'." << endl;
			return false;
		}
		for (; i < tok.size(); ++i) {
			if (!isdigit((unsigned char)tok[i])) {
				cerr << "error: -e: bad cluster id '" << tok << "'." << endl;
				return false;
			}
		}
		out.push_back(atoi(tok.c_str()));
		return true;
	};
	for (const char *p = raw; *p; ++p) {
		if (*p == ',') {
			if (!flush()) return false;
		} else {
			buf.push_back(*p);
		}
	}
	if (!flush()) return false;
	std::sort(out.begin(), out.end());
	out.erase(std::unique(out.begin(), out.end()), out.end());
	return true;
}

// loadCluIds reads a .clu file (binary: int32 nClusters header + N×int32 ids;
// or legacy text: ASCII first-line nClusters, then one id per line).  On
// success the returned vector has exactly *expectedN* entries.  Auto-detects
// format by checking whether the first 4 bytes plausibly form an int32 small
// enough to be a cluster count (≤ 1<<20) — text files start with ASCII digits
// which decode to enormous int32 values and never trigger the binary path.
static bool loadCluIds(const char *path,
                       unsigned long expectedN,
                       std::vector<int> &out)
{
	out.clear();
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		cerr << "error: cannot open .clu file '" << path << "'." << endl;
		return false;
	}
	f.seekg(0, std::ios::end);
	std::streamoff fsize = f.tellg();
	f.seekg(0, std::ios::beg);
	if (fsize < 4) {
		cerr << "error: .clu file '" << path << "' is too small (" << fsize
		     << " bytes)." << endl;
		return false;
	}

	// Probe binary header
	int32_t hdr = 0;
	f.read(reinterpret_cast<char*>(&hdr), 4);
	const std::streamoff bodyBytes = fsize - 4;
	const bool binaryPlausible = (hdr > 0 && hdr <= (1<<20)
	                              && bodyBytes == (std::streamoff)expectedN * 4);

	if (binaryPlausible) {
		out.resize(expectedN);
		f.read(reinterpret_cast<char*>(out.data()),
		       (std::streamsize)expectedN * 4);
		if ((unsigned long)f.gcount() != expectedN * 4) {
			cerr << "error: .clu '" << path << "': short read of body"
			     << endl;
			return false;
		}
		return true;
	}

	// Text fallback — re-open as text
	f.close();
	std::ifstream tf(path);
	if (!tf) {
		cerr << "error: .clu '" << path << "': cannot reopen as text"
		     << endl;
		return false;
	}
	int nClusters = 0;
	if (!(tf >> nClusters)) {
		cerr << "error: .clu '" << path << "': expected leading int32 "
		     << "nClusters header (binary) or ASCII nClusters line "
		     << "(text); got neither." << endl;
		return false;
	}
	out.reserve(expectedN);
	int v;
	while (tf >> v) out.push_back(v);
	if (out.size() != expectedN) {
		cerr << "error: .clu '" << path << "' has " << out.size()
		     << " ids but spike file implies " << expectedN << "." << endl;
		return false;
	}
	return true;
}


/**
 * Usage error
 * @param name Name of the program
 */
void error(const char* name)
{
	cerr << programVersion << endl;
	cerr << "usage: " << name << " [options] input (type '" << name << " -h' for details)" << endl;
	exit(1);
} // error


/**
 * Usage information (~help)
 * @param name Name of the program
 */
void help(const char* name)
{
	cout << name << ": perform principal component analysis (PCA)." << endl;
	cout << "usage: " << name << " [options] input" << endl;
	cout << "usage: " << name << " [options] -" << endl;
	cout << "(use the second form to read from stdin)" << endl << endl;
	cout << " input           input filename" << endl;
	cout << " -f output       output filename" << endl;
	cout << " -n channels     number of channels in the spike file" << endl;
	cout << " -p position     position of the peak within the waveform, in number of samples"<< endl;
	cout << " -b length       number of samples to consider for PCA before spike" << endl;
	cout << " -a length       number of samples to consider for PCA after spike" << endl;
	cout << " -w length       number of samples per waveform" << endl;
	cout << " -d components   number of principal components per channel" << endl;
	cout << " -s size         input data size in bytes (ex : 32000000) when reading from standard input" << endl;
	cout << " -c              use centered data for the projection" << endl;
	cout << " -x              include extra features in output file (spike peak values)" << endl;
	cout << " -g group        electrode-group number for progress bar label (e.g. -g 7 → [PCA-7])" << endl;
	cout << " -l clu-file     .clu.<g> path; required when -e is given.  Used only for the" << endl;
	cout << "                 subset-PCA fit (see -e).  All spikes are still projected through" << endl;
	cout << "                 the resulting basis." << endl;
	cout << " -e ids          comma-separated cluster ids to EXCLUDE from the PCA fit (e.g." << endl;
	cout << "                 '-e 5,12').  Useful for dropping high-rate units (interneurons)" << endl;
	cout << "                 that would otherwise pull eigenvectors toward whatever" << endl;
	cout << "                 distinguishes their fast waveforms.  Requires -l." << endl;
	cout << " --varimax       apply Varimax rotation to each channel's kept-K eigenvectors" << endl;
	cout << "                 before projection.  Same explained variance, but each rotated" << endl;
	cout << "                 component tends to localise onto a small set of time samples" << endl;
	cout << "                 rather than mix energy across the full waveform.  Improves" << endl;
	cout << "                 hand-sorting in Klusters by aligning feature axes with the" << endl;
	cout << "                 most parsimonious spike-shape sources." << endl;
	cout << " --varimax-max-iter N  max rotation iterations (default 30)" << endl;
	cout << " --varimax-tol T      relative change in criterion to call convergence (default 1e-6)" << endl;
	cout << " -v              verbose mode" << endl;
	cout << " -h              display help" << endl;
	cout << endl << "All arguments are mandatory except" << endl;
	cout << "  -x (-p is necessary if you use it)" << endl;
	cout << "  -s which is required only when reading from standard input" << endl;
	cout << "  -b, -a and -p could be not used (all spike length will be considered)" << endl;
	cout << endl;
	exit(0);
} // help


// ── Varimax rotation (patch52) ─────────────────────────────────────────────
//
// Apply an orthogonal Varimax rotation to a (data2use × nComp) loadings
// matrix L in place.  The rotation maximises the Kaiser variance criterion
//
//     V = Σ_j  [ (1/p) Σ_i  L²_ij  −  ( (1/p) Σ_i L_ij )²]
//        ≈ Σ_j Var_i(L²_ij)
//
// across the kept components, encouraging each component to load strongly
// on a few time samples and near zero on the rest.
//
// IMPORTANT: total explained variance is INVARIANT under orthogonal rotation
// — Varimax does not "extract more variance" than the original PCA's top-K
// projection.  What it does is redistribute the SAME total variance into a
// basis that is more parsimonious for hand-sorting interpretation:
//
//   * Each rotated component tends to localise on specific waveform features
//     (rising edge, trough, recovery shoulder) rather than mixing them.
//   * Cluster scatter plots in (PC_i, PC_j) reveal physical spike-shape
//     differences more directly.
//
// Algorithm: pairwise (j, k) sweeps (Kaiser 1958).  Each sweep rotates one
// pair of columns by an angle θ chosen to maximise the criterion over that
// pair while holding all other columns fixed.  Pairwise updates commute
// with the optimum within numerical tolerance, so the algorithm converges
// monotonically.  Single-precision tol of 1e-6 is overkill; the default
// converges in 5–15 sweeps for nComp ≤ 5 on typical spike data.
//
// Caller-controlled escape valves: maxIter caps total sweeps; tol short-
// circuits when the criterion's relative change drops below tol.
//
// Returns the number of sweeps performed.
int applyVarimaxRotation(gsl_matrix *L, int maxIter, double tol)
{
	const size_t p = L->size1;        // rows: data2use (time samples)
	const size_t k = L->size2;        // cols: nComponents

	if (k < 2) return 0;              // nothing to rotate against

	// Track criterion across sweeps for convergence check.
	auto criterion = [&]() {
		double V = 0.0;
		for (size_t j = 0; j < k; ++j) {
			double s2 = 0.0, s4 = 0.0;
			for (size_t i = 0; i < p; ++i) {
				const double v = gsl_matrix_get(L, i, j);
				const double v2 = v * v;
				s2 += v2;
				s4 += v2 * v2;
			}
			V += s4 - (s2 * s2) / (double)p;
		}
		return V;
	};

	double prevV = criterion();
	int    iter  = 0;
	for (iter = 1; iter <= maxIter; ++iter) {
		// One sweep: rotate every (j, k') pair once.
		for (size_t j = 0; j + 1 < k; ++j) {
			for (size_t kk = j + 1; kk < k; ++kk) {
				// Build u_i = L²_ij − L²_ikk,  v_i = 2 L_ij L_ikk
				// Then the optimal rotation angle for (j, kk) is
				//   4θ = atan2(  2 (p Σ uv − Σu · Σv),  p Σ(u²−v²) − (Σu)² + (Σv)² )
				double sU = 0.0, sV = 0.0;
				double sUU_VV = 0.0, sUV = 0.0;
				for (size_t i = 0; i < p; ++i) {
					const double a = gsl_matrix_get(L, i, j);
					const double b = gsl_matrix_get(L, i, kk);
					const double u = a * a - b * b;
					const double v = 2.0 * a * b;
					sU      += u;
					sV      += v;
					sUU_VV  += u * u - v * v;
					sUV     += u * v;
				}
				const double pD       = (double)p;
				const double num      = 2.0 * (pD * sUV - sU * sV);
				const double den      = pD * sUU_VV - (sU * sU - sV * sV);
				if (num == 0.0 && den == 0.0) continue;   // degenerate
				const double theta    = 0.25 * std::atan2(num, den);
				const double c        = std::cos(theta);
				const double s        = std::sin(theta);
				if (std::fabs(theta) < 1e-12) continue;   // already optimal
				// Apply 2-D rotation to columns j and kk:
				//   [L_ij'  L_ikk'] = [L_ij  L_ikk] · [[c, -s], [s, c]]
				for (size_t i = 0; i < p; ++i) {
					const double a = gsl_matrix_get(L, i, j);
					const double b = gsl_matrix_get(L, i, kk);
					gsl_matrix_set(L, i, j,  c * a + s * b);
					gsl_matrix_set(L, i, kk, -s * a + c * b);
				}
			}
		}
		const double V = criterion();
		// Relative-change convergence test on the criterion.
		const double rel = (prevV > 0.0) ? std::fabs(V - prevV) / prevV
		                                 : std::fabs(V - prevV);
		if (rel < tol) break;
		prevV = V;
	}

	// After rotation the columns are no longer in any particular order.
	// Re-sort columns by descending ||column||² (which under orthogonal
	// rotation also equals the per-component variance after projection),
	// so PC_0 still carries the "biggest" rotated factor.  This keeps the
	// .fet output indexing semantically close to the un-rotated case.
	{
		std::vector<std::pair<double, size_t>> norms(k);
		for (size_t j = 0; j < k; ++j) {
			double s = 0.0;
			for (size_t i = 0; i < p; ++i) {
				const double v = gsl_matrix_get(L, i, j);
				s += v * v;
			}
			norms[j] = {s, j};
		}
		std::sort(norms.begin(), norms.end(),
		          [](const auto& x, const auto& y) { return x.first > y.first; });
		// Apply column permutation.  Copy old columns into a temp.
		gsl_matrix *tmp = gsl_matrix_alloc(p, k);
		gsl_matrix_memcpy(tmp, L);
		for (size_t j = 0; j < k; ++j) {
			const size_t src = norms[j].second;
			for (size_t i = 0; i < p; ++i)
				gsl_matrix_set(L, i, j, gsl_matrix_get(tmp, i, src));
		}
		gsl_matrix_free(tmp);
	}

	return iter;
}


// Start here
int main(int argc,char *argv[])
{
	struct arguments arguments;
	arguments.inputSize = 0;
	arguments.nChannels = 0;
	arguments.beforeSpike = -1;
	arguments.afterSpike = -1;
	arguments.peakPosition = -1;
	arguments.spikeLength = -1;
	arguments.nComponents = 0; // number of principal components
	arguments.isCenteredData = false;
	arguments.offset = 0;
	
	arguments.isInputFileProvided = false;
	arguments.isOutputFileProvided = false;
	arguments.isInputSizeProvided = false;
	arguments.isNChannelsProvided = false;
	arguments.isBeforeSpikeProvided = false;
	arguments.isAfterSpikeProvided = false;
	arguments.isPeakPositionProvided = false;
	arguments.isSpikeLengthProvided = false;
	arguments.isNComponentsProvided = false;
	arguments.isExtraFeaturesProvided = false;
	arguments.isOffsetProvided = false;
	arguments.electrodeGroup = -1;  // -1 == no group label (legacy mode)
	arguments.cluFileName = nullptr;
	arguments.isCluFileProvided = false;
	arguments.excludeClustersStr = nullptr;
	arguments.isExcludeClustersProvided = false;
	arguments.varimax        = false;      // patch52 defaults
	arguments.varimaxMaxIter = 30;
	arguments.varimaxTol     = 1e-6;
	
	parseArgs(argc,argv,arguments); // Parse command-line
	
	short *rawData; // data, means/channel
	short **peakVal; // peak values for extra features output
	// means, sum and variance-cov matrix for each dimension in each channel
	double **mean,**sum;
	// data for each channel/ spike dimension/ spike & reduced data (PCA)
	gsl_matrix **datSpkChanCenter, ** datSpkChan,**reducedData;
	gsl_matrix **varcov;// variance-cov matrix for each dimension
	FILE *inputFile = NULL,*outputFile = NULL;
	unsigned long int nSpikes = -1,nRecordsRead = 0;
	int data2use = arguments.spikeLength; // number of record to use in the waveform
	int recShift = 0; // shift for first record to consider in the waveform
	if(arguments.isBeforeSpikeProvided)
	{
		data2use = (arguments.beforeSpike+1+arguments.afterSpike);
		recShift = arguments.peakPosition-arguments.beforeSpike;
	}
	gsl_vector *eigenValues = gsl_vector_alloc(data2use);
	gsl_matrix *eigenVectors = gsl_matrix_alloc(data2use,data2use);
	gsl_eigen_symmv_workspace * w = gsl_eigen_symmv_alloc(data2use);
	gsl_matrix_view reducedEigenVectors; // for storing final data
	// Saved eigenvectors per channel for writing .pca.N file
	gsl_matrix **savedEvec = new gsl_matrix*[arguments.nChannels];
	for (int i = 0; i < arguments.nChannels; ++i) savedEvec[i] = nullptr;
	
	// open input
	if ( arguments.isInputFileProvided )
	{
		inputFile = fopen(arguments.inputFileName,"rb");
		if ( inputFile == NULL )
		{
			cerr << "error: cannot open '" << arguments.inputFileName << "'." << endl;
			exit(1);
		} // if
		fseeko(inputFile,0,SEEK_END); // put the position indicator at the end of the stream
		arguments.inputSize = ftello(inputFile); // value of the position indicator of the stream (here ~file size)
	} // if
	
	// Check Input Size
	if ( !checkInputs(arguments) ) // check arguments value
		exit(1);
	
	if ( verbose )
	{
		cout << endl;
		cout << "Input File            = ";
		if ( arguments.isInputFileProvided ) cout << arguments.inputFileName << endl;
		else cout << "-" << endl;
		cout << "Output File           = " << arguments.outputFileName << endl;
		cout << "Input Size            = ";
		if ( arguments.isInputSizeProvided ) cout << arguments.inputSize << endl;
		else cout << "N/A" << endl;
		
		cout << endl;
	} // if verbose
	
	// number of records (for all and one channels)
	nRecords = arguments.inputSize/RECORD_BYTE_SIZE;
	nRecordsPerChannel = nRecords/arguments.nChannels;
	nSpikes = nRecordsPerChannel/arguments.spikeLength; // nb spikes
	
	if ( nRecords < 1 || nRecordsPerChannel < 1 )
	{
		cerr << "error: not enough records (size " << arguments.inputSize << ")" << endl;
		exit(1);
	}
	if ( nSpikes < 1 )
	{
		cerr << "error: incorrect spike number (" << nSpikes << "), check number of channels ("
				<< arguments.nChannels << ") and input size (" << arguments.inputSize << ")." << endl;
		exit(1);
	}
	
	if ( verbose )
	{
		cout << "Waveform length                 = " << arguments.spikeLength << endl;
		cout << "Number of principal components  = " << arguments.nComponents << endl;
		if(arguments.isBeforeSpikeProvided)
		{
			cout << "Number of samples before peak   = " << arguments.beforeSpike << endl;
			cout << "Number of samples after peak    = " << arguments.afterSpike << endl;
			cout << "Peak position in waveform       = " << arguments.peakPosition << endl;
		}
		cout << "Projection with centered data   = " << arguments.isCenteredData << endl;
		///cout << "Record size basis               = " << (RECORD_BYTE_SIZE*8) << " bits" << endl;
		cout << "Number of input records         = " << nRecords << endl;
		cout << "Number of records per channel   = " << nRecordsPerChannel << endl;
		cout << "Number of spikes                = " << nSpikes <<endl;
		cout << endl;
	}

	// ─── Subset PCA setup (interneuron exclusion) ──────────────────────────
	//
	// When -e/-l are supplied, build a per-spike keep mask such that:
	//   keepMask[k] == true   ⇒ spike k contributes to the covariance fit
	//   keepMask[k] == false  ⇒ spike k is projected through the resulting
	//                           basis but does NOT influence its direction
	// This matches the canonical "fit on subset, project all" semantics:
	// the eigenvectors are computed entirely from kept spikes, but every
	// row of the input still produces a row in the output .fet so the
	// caller's spike count and indexing are preserved.
	std::vector<bool> keepMask;
	std::vector<int>  excludeList;
	unsigned long nFitSpikes = nSpikes;
	if (arguments.isCluFileProvided != arguments.isExcludeClustersProvided) {
		cerr << "error: -l and -e must be given together (or both omitted)."
		     << endl;
		exit(1);
	}
	if (arguments.isExcludeClustersProvided) {
		// isCenteredData inputs are pre-centered against the FULL set's mean
		// outside this binary; subset PCA needs to re-centre against the
		// fit set's mean, which we cannot reconstruct from already-centred
		// data.  Refuse the combination explicitly rather than silently
		// producing a basis fit on the wrong centroid.
		if (arguments.isCenteredData) {
			cerr << "error: -c (pre-centered input) is incompatible with "
			     << "-e (subset PCA).  Re-run on uncentered input."
			     << endl;
			exit(1);
		}
		if (!parseExcludeList(arguments.excludeClustersStr, excludeList))
			exit(1);
		std::vector<int> cluIds;
		if (!loadCluIds(arguments.cluFileName, nSpikes, cluIds))
			exit(1);

		keepMask.assign(nSpikes, true);
		nFitSpikes = 0;
		// Linear scan: for each spike, drop if its cluster is in the exclude
		// set.  excludeList is small (typically 1-3 entries) and sorted, so
		// std::binary_search is cheap.
		for (unsigned long k = 0; k < nSpikes; ++k) {
			if (std::binary_search(excludeList.begin(),
			                       excludeList.end(),
			                       cluIds[k])) {
				keepMask[k] = false;
			} else {
				++nFitSpikes;
			}
		}

		if (nFitSpikes == 0) {
			cerr << "error: -e: every spike was excluded; cannot fit basis."
			     << endl;
			exit(1);
		}
		if (nFitSpikes < (unsigned long)data2use) {
			cerr << "error: -e: only " << nFitSpikes << " spikes remain "
			     << "after exclusion, but PCA over data2use=" << data2use
			     << " samples needs at least that many spikes for a "
			     << "non-degenerate covariance.  Pick a smaller exclude "
			     << "set." << endl;
			exit(1);
		}

		cout << "Subset PCA: excluding clusters {";
		for (size_t i = 0; i < excludeList.size(); ++i) {
			cout << excludeList[i];
			if (i + 1 < excludeList.size()) cout << ", ";
		}
		cout << "}; fitting on " << nFitSpikes << "/" << nSpikes
		     << " spikes (" << (nSpikes - nFitSpikes) << " dropped, "
		     << (100.0 * (nSpikes - nFitSpikes) / (double)nSpikes)
		     << "%)." << endl;
	}

	// Build the progress bar's step tag.  When a group number was
	// passed (-g N), include it so parallel-group runs from the
	// wrapper script can be distinguished visually: "[PCA-7]" for
	// group 7's bar.  Without -g we fall back to the legacy "[PCA]"
	// for direct-CLI compatibility.
	std::string pcaStepTag = "PCA";
	if (arguments.electrodeGroup >= 0) {
		pcaStepTag += "-";
		pcaStepTag += std::to_string(arguments.electrodeGroup);
	}
	ProgressBar *progress = new ProgressBar("", pcaStepTag, (arguments.nChannels+4));
	
	// Init arrays
	rawData = new short[nRecords]; // Buffer for all data (all channels)
	mean = new double* [arguments.nChannels]; // means init
	sum = new double* [arguments.nChannels]; // sums init
	if(arguments.isExtraFeaturesProvided)
		peakVal = new short* [arguments.nChannels]; // peak values init&
	if(!arguments.isCenteredData)
		datSpkChan = new gsl_matrix* [arguments.nChannels];
	datSpkChanCenter = new gsl_matrix* [arguments.nChannels];
	reducedData = new gsl_matrix* [arguments.nChannels];
	varcov = new gsl_matrix* [arguments.nChannels];
	
	progress->start(); // Start progress bar
	///progress->message("Extracting data");
	// Get data
	if ( arguments.isInputFileProvided )
	{
		rewind(inputFile); // put the position indicator at the beginning of the stream
		nRecordsRead = fread(rawData,sizeof(short),nRecords,inputFile);
		fclose(inputFile);
	}
	else
	{
		nRecordsRead = fread(rawData,sizeof(short),nRecords,stdin);
	} // else
	
	if ( nRecordsRead != nRecords )
	{
		cerr << "error: insufficient number of records in the file (" << nRecordsRead 
		<< ", expecting " << nRecords << ")" << endl;
		exit(1);
	}
	progress->advance(); // Complete data importation
	
	// Fill arrays with rec values & compute means
	///progress->message("Preparing data for PCA");
	for ( int i = 0 ; i < arguments.nChannels ; ++i )
	{
		mean[i] = new double[data2use];
		sum[i] = new double[data2use];
		if(!arguments.isCenteredData)
			datSpkChan[i] = gsl_matrix_alloc(data2use,nSpikes);
		if(arguments.isExtraFeaturesProvided)
			peakVal[i] = new short[nSpikes];

		varcov[i] = gsl_matrix_alloc(data2use,data2use);
		reducedData[i] = gsl_matrix_alloc(arguments.nComponents,nSpikes);
		// datSpkChanCenter holds the centred FIT set (subset when -e is used,
		// full set otherwise — nFitSpikes equals nSpikes when no exclusion is
		// active).  The covariance dgemm below reads this matrix, so its
		// column count is nFitSpikes — eigenvectors are computed strictly
		// from the kept spikes.  Projection later uses datSpkChan (uncentred,
		// full size), preserving every spike's row in the output .fet.
		datSpkChanCenter[i] = gsl_matrix_alloc(data2use, nFitSpikes);
		for ( int j = 0 ; j < data2use ; ++j )
		{
			mean[i][j] = -1;
			sum[i][j] = 0;
			// First pass: populate datSpkChan (full, uncentred) and
			// accumulate the FIT-set sum for the mean.
			for ( unsigned int k = 0 ; k < nSpikes ; ++k )
			{
				double v = rawData[(arguments.nChannels*arguments.spikeLength)*k+((j+recShift)*arguments.nChannels)+i];
				if(!arguments.isCenteredData)
					gsl_matrix_set(datSpkChan[i],j,k,v);
				if (keepMask.empty() || keepMask[k])
					sum[i][j] += v;
				if(arguments.isExtraFeaturesProvided && (j+recShift)==arguments.peakPosition)
					peakVal[i][k] = v; // if this is the peak, store it !
			} // for k
			mean[i][j] = sum[i][j]/(double)nFitSpikes;

			// Second pass: emit one column of datSpkChanCenter per kept
			// spike, in the same temporal order as the input.  fitIdx is
			// the running index into the subset; with no exclusion it
			// simply equals k throughout.
			unsigned long fitIdx = 0;
			for ( unsigned int k = 0 ; k < nSpikes ; ++k )
			{
				if (!keepMask.empty() && !keepMask[k]) continue;
				double v = rawData[(arguments.nChannels*arguments.spikeLength)*k+((j+recShift)*arguments.nChannels)+i];
				gsl_matrix_set(datSpkChanCenter[i], j, fitIdx, v - mean[i][j]);
				++fitIdx;
			} // for k
		} // for j
	} // for i
	delete[] rawData; // free memory
	progress->advance(); // Complete data initialization
	
	if ( verbose )
	{
		cout << endl << endl;
		cout << "Total number of channels = " << arguments.nChannels << endl;
		for ( int c = 0 ; c < arguments.nChannels ; ++c )
		{
			cout << "Means for channel #" << c << " = ";
			for ( int d = 0 ; d < data2use ; ++d )
			{
				cout << mean[c][d] << ", ";
			} // for d
			cout << endl;
			cout << endl;
		} // for c
		cout << endl;
	} // verbose
	
	// PCA statement - parallelized across channels with per-thread GSL workspaces
	///progress->message("Computing PCA");
#ifdef _OPENMP
	#pragma omp parallel for schedule(static)
#endif
	for ( int i = 0 ; i < arguments.nChannels ; ++i )
	{
		// Per-thread GSL workspace (required for thread safety)
		gsl_vector *tEigenValues  = gsl_vector_alloc(data2use);
		gsl_matrix *tEigenVectors = gsl_matrix_alloc(data2use, data2use);
		gsl_eigen_symmv_workspace *tw = gsl_eigen_symmv_alloc(data2use);

		// compute variance-covariance matrix (Data * DataTrans)
		// Compute the variance-covariance matrix (Data * DataTrans).
		// The divisor uses the number of columns actually present in
		// datSpkChanCenter — that is nFitSpikes, which equals nSpikes when
		// no exclusion is active and nFitSpikes < nSpikes under -e.  Using
		// the wrong divisor would only change eigenvalue magnitudes, not
		// the eigenvector directions sorted by gsl_eigen_symmv_sort, but
		// "right divisor" matches the unbiased sample covariance estimator
		// the comment above promises.
		gsl_blas_dgemm(CblasNoTrans,CblasTrans,(1.0/(nFitSpikes-1)),datSpkChanCenter[i],datSpkChanCenter[i],0.0,varcov[i]);

		// solve eigen system to get eigen values and vectors
		gsl_eigen_symmv(varcov[i],tEigenValues,tEigenVectors,tw);
		gsl_eigen_symmv_sort(tEigenValues,tEigenVectors,GSL_EIGEN_SORT_VAL_DESC);

		// keep only eigen vectors for the given first principal components
		gsl_matrix_view tReducedEigenVectors = gsl_matrix_submatrix(tEigenVectors,0,0,data2use,arguments.nComponents);

		// patch52: optional Varimax rotation of the kept basis.  Operates
		// in-place on the submatrix view, so both the projection (below)
		// and the saved .pca.N basis use the rotated version.  No-op when
		// nComponents < 2 or --varimax was not requested.
		if (arguments.varimax && arguments.nComponents >= 2) {
			int nSweeps = applyVarimaxRotation(
				&tReducedEigenVectors.matrix,
				arguments.varimaxMaxIter,
				arguments.varimaxTol);
			if (verbose) {
				#pragma omp critical
				cout << "  Varimax channel #" << i << ": "
				     << nSweeps << " sweep(s)"
				     << (nSweeps >= arguments.varimaxMaxIter ? " (hit max-iter)" : "")
				     << endl;
			}
		}

		// compute new coordinates for data : Trans(evec_reduce) x Trans(data)
		if(!arguments.isCenteredData)
			gsl_blas_dgemm(CblasTrans,CblasNoTrans,1.0,&tReducedEigenVectors.matrix,datSpkChan[i],0.0,reducedData[i]);
		else
			gsl_blas_dgemm(CblasTrans,CblasNoTrans,1.0,&tReducedEigenVectors.matrix,datSpkChanCenter[i],0.0,reducedData[i]);

		// Save the reduced eigenvectors for this channel (data2use x nComponents)
		savedEvec[i] = gsl_matrix_alloc(data2use, arguments.nComponents);
		gsl_matrix_memcpy(savedEvec[i], &tReducedEigenVectors.matrix);

		gsl_eigen_symmv_free(tw);
		gsl_matrix_free(tEigenVectors);
		gsl_vector_free(tEigenValues);

		progress->advance(); // Complete PCA for channel i
	} // for i
	
	///progress->message("Saving");
	// Write output file: binary PCA features
	// Format: int32_t nFeatureCols; then nSpikes * nFeatureCols * int64_t (row-major)
	// Timestamps are NOT written here; ndm_pca will merge with .res via process_mergefeatures
	const int nFeatureCols = arguments.isExtraFeaturesProvided
	                        ? (arguments.nChannels * arguments.nComponents + arguments.nChannels)
	                        : (arguments.nChannels * arguments.nComponents);
	outputFile = fopen(arguments.outputFileName, "wb");
	if (!outputFile) {
		cerr << "error: cannot open output file '" << arguments.outputFileName << "'." << endl;
		exit(1);
	}
	// Write .fet output — pack all values into a single buffer then write once.
	// nSpikes * nFeatureCols int64_t values plus the int32_t header.
	{
		int32_t nFeat32 = (int32_t)nFeatureCols;
		fwrite(&nFeat32, sizeof(int32_t), 1, outputFile);
	}
	{
		const size_t nVals = (size_t)nSpikes * (size_t)nFeatureCols;
		std::vector<int64_t> fetBuf(nVals);
		for ( unsigned int k = 0 ; k < nSpikes ; ++k )
		{
			size_t col = 0;
			for ( int i = 0 ; i < arguments.nChannels ; ++i )
				for ( int j = 0 ; j < arguments.nComponents ; ++j )
					fetBuf[(size_t)k * nFeatureCols + col++] =
						(int64_t)llround(*gsl_matrix_ptr(reducedData[i], j, k));
			if ( arguments.isExtraFeaturesProvided )
				for ( int i = 0; i < arguments.nChannels ; ++i )
					fetBuf[(size_t)k * nFeatureCols + col++] = (int64_t)peakVal[i][k];
		} // for k
		fwrite(fetBuf.data(), sizeof(int64_t), nVals, outputFile);
	}
	fclose(outputFile);
	progress->advance(); // Complete saving results

	// Write .pca.N eigenvector file for use by klusters spike realignment.
	// Binary format:
	//   int32_t nCh, data2use, nComp, centered, recShift
	//   for each channel: data2use * double  (mean vector)
	//   for each channel: data2use * nComp * double  (eigenvectors, col-major)
	{
		// Derive .pca.N path from outputFileName: strip .tmp, replace .fet. with .pca.
		std::string pcaPath(arguments.outputFileName);
		// Remove .tmp suffix if present
		const std::string tmpSuffix(".tmp");
		if (pcaPath.size() >= tmpSuffix.size() &&
		    pcaPath.compare(pcaPath.size()-tmpSuffix.size(), tmpSuffix.size(), tmpSuffix) == 0)
			pcaPath.erase(pcaPath.size()-tmpSuffix.size());
		// Replace .fetD. with .pcaD., or .fet. with .pca.
		const std::string fetDStr(".fetD.");
		const std::string pcaDStr(".pcaD.");
		const std::string fetStr(".fet.");
		const std::string pcaStr(".pca.");
		size_t pos = pcaPath.rfind(fetDStr);
		if (pos != std::string::npos)
			pcaPath.replace(pos, fetDStr.size(), pcaDStr);
		else {
			pos = pcaPath.rfind(fetStr);
			if (pos != std::string::npos)
				pcaPath.replace(pos, fetStr.size(), pcaStr);
		}

		FILE *pcaFile = fopen(pcaPath.c_str(), "wb");
		if (!pcaFile) {
			cerr << "warning: cannot write .pca file: " << pcaPath << endl;
		} else {
			// Header: 5 x int32_t
			int32_t hdr[5];
			hdr[0] = (int32_t)arguments.nChannels;
			hdr[1] = (int32_t)data2use;
			hdr[2] = (int32_t)arguments.nComponents;
			hdr[3] = arguments.isCenteredData ? 1 : 0;
			hdr[4] = (int32_t)recShift;
			fwrite(hdr, sizeof(int32_t), 5, pcaFile);
			// Per-channel means (data2use doubles each)
			for (int i = 0; i < arguments.nChannels; ++i)
				fwrite(mean[i], sizeof(double), (size_t)data2use, pcaFile);
			// Per-channel eigenvectors (data2use * nComponents doubles, col-major).
			// Pack into a contiguous buffer and write in one fwrite per channel.
			{
				std::vector<double> evecBuf((size_t)data2use * arguments.nComponents);
				for (int i = 0; i < arguments.nChannels; ++i) {
					if (savedEvec[i]) {
						for (int c = 0; c < arguments.nComponents; ++c)
							for (int r = 0; r < data2use; ++r)
								evecBuf[(size_t)c * data2use + r] =
									gsl_matrix_get(savedEvec[i], r, c);
						fwrite(evecBuf.data(), sizeof(double),
						       (size_t)data2use * arguments.nComponents, pcaFile);
					}
				}
			}
			fclose(pcaFile);
			if (verbose) cout << "Wrote " << pcaPath << endl;
		}
	}

	// Free Memory
	///progress->message("Free Memory");
	for ( int i = 0 ; i < arguments.nChannels ; ++i )
	{
		delete[] mean[i];
		delete[] sum[i];
		if(arguments.isExtraFeaturesProvided)
			delete[] peakVal[i];
		if(!arguments.isCenteredData)
			gsl_matrix_free(datSpkChan[i]);
		gsl_matrix_free(datSpkChanCenter[i]);
		gsl_matrix_free(varcov[i]);
		gsl_matrix_free(reducedData[i]);
	}
	
	if(arguments.isExtraFeaturesProvided)
		delete[] peakVal;
	if(!arguments.isCenteredData)
		delete[] datSpkChan;
	delete[] datSpkChanCenter;
	delete[] mean;
	delete[] sum;
	delete[] varcov;
	delete[] reducedData;
	
	for (int i = 0; i < arguments.nChannels; ++i)
		if (savedEvec[i]) gsl_matrix_free(savedEvec[i]);
	delete[] savedEvec;

	gsl_eigen_symmv_free(w);
	gsl_vector_free(eigenValues);
	gsl_matrix_free(eigenVectors);
	
	progress->advance(); // Complete free memory
	delete progress;
	
	if ( verbose ) cout << endl;
		
	return 0;
} // main


/**
 * Fill arguments values from user's argv
 * @param argc number of arguments
 * @param argv list of arguments
 * @param arguments user arguments to fill
 */
void parseArgs(const int argc,char **argv,arguments &arguments)
{
	// Parse command-line
	int nOptions = argc;
	int i;
	
	if ( argc == 2 && (!strcmp(argv[1],"-h") || !strcmp(argv[1],"--help")) ) help(argv[0]);
	if ( nOptions < 2 ) error(argv[0]);
	
	for ( i = 1 ; i < nOptions ; ++i )
	{
		if ( argv[i][0] != '-' ) break;
		if ( strlen(argv[i]) < 2 || argv[i][0] != '-' ) error(argv[0]);

		// patch52: long options ("--varimax", "--varimax-max-iter", "--varimax-tol")
		// are matched on the full string before falling into the single-char
		// switch.  Continue with `continue` so the switch below doesn't see them.
		if ( !strcmp(argv[i], "--varimax") ) {
			arguments.varimax = true;
			continue;
		}
		if ( !strcmp(argv[i], "--varimax-max-iter") ) {
			if ( i+1 >= nOptions ) error(argv[0]);
			arguments.varimaxMaxIter = atoi(argv[++i]);
			if ( arguments.varimaxMaxIter < 1 ) {
				cerr << "error: --varimax-max-iter must be >= 1" << endl;
				exit(1);
			}
			continue;
		}
		if ( !strcmp(argv[i], "--varimax-tol") ) {
			if ( i+1 >= nOptions ) error(argv[0]);
			arguments.varimaxTol = atof(argv[++i]);
			if ( !(arguments.varimaxTol > 0.0) ) {
				cerr << "error: --varimax-tol must be > 0" << endl;
				exit(1);
			}
			continue;
		}

		switch ( argv[i][1] )
		{
			case 's': // input size
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.inputSize = atoi(argv[++i]);
				arguments.isInputSizeProvided = true;
				break;
				
			case 'n': // Total number of channels
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.nChannels = atoi(argv[++i]);
				arguments.isNChannelsProvided = true;
				break;
				
			case 'b': // Number of record before spike to consider
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.beforeSpike = atoi(argv[++i]);
				arguments.isBeforeSpikeProvided = true;
				break;
				
			case 'p': // peak position into the extracted spike
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.peakPosition = atoi(argv[++i]);
				arguments.isPeakPositionProvided = true;
				break;
				
			case 'a': // Number of record after spike to consider
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.afterSpike = atoi(argv[++i]);
				arguments.isAfterSpikeProvided = true;
				break;
				
			case 'w': // Extraction waveform length
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.spikeLength = atoi(argv[++i]);
				arguments.isSpikeLengthProvided = true;
				break;
				
			case 'd': // number of principal components (dimensions)
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.nComponents = atoi(argv[++i]);
				arguments.isNComponentsProvided = true;
				break;
				
			case 'f': // output file (for writing transformed data)
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.outputFileName = argv[++i];
				arguments.isOutputFileProvided = true;
				break;
				
			
			case 'c': // use centered data for projection
				arguments.isCenteredData = true;
				break;
				
			case 'x': // add extra features in output file
				arguments.isExtraFeaturesProvided = true;
				break;
				
			case 't': // OpenMP thread count (useful when running groups in parallel)
				if ( i+1 > nOptions ) error(argv[0]);
#ifdef _OPENMP
				omp_set_num_threads(atoi(argv[++i]));
#else
				++i; // consume the argument even without OpenMP
				cerr << "warning: -t ignored (not compiled with OpenMP)" << endl;
#endif
				break;

			case 'g': // electrode-group number for the progress bar label
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.electrodeGroup = atoi(argv[++i]);
				break;

			case 'l': // .clu file path for subset PCA (paired with -e)
				if ( i+1 >= nOptions ) error(argv[0]);
				arguments.cluFileName = argv[++i];
				arguments.isCluFileProvided = true;
				break;

			case 'e': // comma-separated cluster ids to exclude from the fit
				if ( i+1 >= nOptions ) error(argv[0]);
				arguments.excludeClustersStr = argv[++i];
				arguments.isExcludeClustersProvided = true;
				break;

			case 'v': // verbose mode
				verbose = true;
				break;
				
			case 'h': // show help
				help(argv[0]);
				break;
			
			default:
				cerr << "error: unknown option '" << argv[i] << "'." << endl;
				exit(1);
				break;
		}
	} // for
	
	// input file
	if ( i >= argc )
	{
		cerr << "error: missing input file." << endl;
		exit(1);
	}
	
	if ( i == argc && strcmp(argv[i],"-") ) arguments.isInputFileProvided = false;
	else
	{
		arguments.inputFileName = argv[i];
		arguments.isInputFileProvided = true;
	}
	
	// Make sure we get the correct arguments.
	if ( !arguments.isInputSizeProvided )
	{
		if ( !arguments.isInputFileProvided )
		{
			cerr << "error: missing size for standard input." << endl;
			exit(1);
		}
	} // if
	if ( !arguments.isNChannelsProvided )
	{
		cerr << "error: missing number of channels." << endl;
		exit(1);
	}
	if ( !arguments.isSpikeLengthProvided )
	{
		cerr << "error: missing waveform length." << endl;
		exit(1);
	}
	if ( !arguments.isNComponentsProvided)
	{
		cerr << "error: missing number of principal components to keep after PCA." << endl;
		exit(1);
	}
	if ( !arguments.isOutputFileProvided)
	{
		cerr << "error: missing output file." << endl;
		exit(1);
	}
	if( arguments.isBeforeSpikeProvided)
		if ( !arguments.isPeakPositionProvided || !arguments.isAfterSpikeProvided)
		{
			cerr << "error : missing number of samples after peak or peak position." << endl;
			exit(1);
		}
	if( arguments.isAfterSpikeProvided )
		if( !arguments.isBeforeSpikeProvided  || !arguments.isBeforeSpikeProvided)
		{
			cerr << "error : missing number of samples before peak or peak position." << endl;
			exit(1);
		}
} // parseArgs


/**
 * Check argument values
 * @param arguments Values of user's arguments
 * @return TRUE if everything is Ok, FALSE else.
 */
bool checkInputs(const arguments arguments)
{
	// Make sure we get the correct arguments.
	if ( arguments.inputSize < 1 )
	{
		cerr << "error: incorrect input size " << arguments.inputSize << "." << endl<< endl;
		return false;
	}
	else if ( arguments.inputSize%RECORD_BYTE_SIZE != 0 )
	{
		cerr << "error: input size " << arguments.inputSize << " is not a multiple of resolution " << RECORD_BYTE_SIZE << "." << endl<< endl;
		return false;
	}
	else if ( arguments.inputSize%arguments.spikeLength != 0 )
	{
		cerr << "error: input size " << arguments.inputSize << " is not a multiple of spike size " << arguments.spikeLength << "." << endl<< endl;
		return false;
	}

	if ( arguments.spikeLength > arguments.inputSize )
	{
		cerr << "error: waveform length (" << arguments.spikeLength << ") is greater than input size (" << arguments.inputSize << ")." << endl<< endl;
		return false;
	}
	
	if( arguments.isExtraFeaturesProvided && !arguments.isPeakPositionProvided)
	{
		cerr << "error: missing peak position for extra features." << endl<< endl;
		return false;
	}
	
	if ( arguments.isBeforeSpikeProvided )//|| arguments.isAfterSpikeProvided )
	{
		if( (arguments.beforeSpike+arguments.afterSpike) >= arguments.inputSize )
		{
			cerr << "error: interval between before ("<<arguments.beforeSpike<<") and after ("<<arguments.afterSpike
			<<") > waveform length ("<< arguments.spikeLength << ")." << endl<< endl;
			return false;
		}
		else if( arguments.peakPosition >= arguments.spikeLength )
		{
			cerr << "error: peak position ("<<arguments.peakPosition<<") must be in [0 - "<< arguments.spikeLength-1 << "]." << endl<< endl;
			return false;
		}
		else if( arguments.peakPosition < arguments.beforeSpike )
		{
			cerr << "error: peak position ("<<arguments.peakPosition<<") < number of samples before peak ("<< arguments.beforeSpike << ")." << endl<< endl;
			return false;
		}
		else if( (arguments.peakPosition+arguments.afterSpike) >= arguments.spikeLength )
		{
			cerr << "error: number of samples after spike ("<<arguments.afterSpike<<") is too large (max "
			<<(arguments.spikeLength-arguments.peakPosition-1)<< ")." << endl<< endl;
			return false;
		}
	}
	else if(arguments.isPeakPositionProvided)
	{
		cerr << "warning: incomplete information for PCA on partial waveform, "
		<< "using entire waveform length." << endl<< endl;
	}
	if ( arguments.nComponents < 1 )
	{
		cerr << "error: incorrect number of principal components (" << arguments.nComponents << ")." << endl<< endl;
		return false;
	}
	else if ( arguments.nComponents > arguments.spikeLength )
	{
		cerr << "error: number of principal components (" << arguments.nComponents << ") exceeds spike length (" << arguments.spikeLength << ")." << endl<< endl;
		return false;
	}
	else if( arguments.isBeforeSpikeProvided && arguments.isAfterSpikeProvided && 
		(arguments.beforeSpike+arguments.afterSpike+1) < arguments.nComponents )
	{
		cerr << "error: number of principal components (" << arguments.nComponents 
		<< ") exceeds interval used for PCA ("<<(arguments.beforeSpike+arguments.afterSpike+1)<<")." << endl<< endl;
		return false;
	}

	return true;
} // checkInputs
