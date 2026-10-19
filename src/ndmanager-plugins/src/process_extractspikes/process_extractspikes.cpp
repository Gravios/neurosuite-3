/***************************************************************************
                          process_extractspikes.cpp  -  description
                             -------------------
    copyright            : (C) 2007 by Nicolas Lebas
    email                : nicolas.lebas@college-de-france.fr

	 Enabling LFS + code cleaning
    copyright            : (C) 2008 by Michaël Zugaro
    email                : michael.zugaro@college-de-france.fr


 * Description : process_extractspikes is a program which detect and extract 
 * spikes from a filtered electrophysiologic record (following a given
 * threshold).
 *
 * Usage : process_extractspikes [options] [input]
 *
 ***************************************************************************/

/***************************************************************************
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
 *                                                                         *
 ***************************************************************************/

#define _LARGEFILE_SOURCE64
#define _FILE_OFFSET_BITS 64

#include "process_extractspikes.h"
#include "extractspikes_detect.h"   // peak search, refractory rule, -c parsing
#include "extractspikes_reextract.h" // -R: re-cut windows at the .res times
#include "progressbar.h"      // BlockProgress — defrag-style stage progress
#include <cmath>
#include <neurosuite/core/fileio64.hpp>
#include <cstdlib>
#include <cstring>
#include <neurosuite/core/strsep.hpp>
#include <string>
#include <cstdint>

#include <sstream>
#include <fstream>
#include <vector>
#include <algorithm>

#include <cerrno>
#include <cstdio>

using namespace std;
using namespace extractspikes;
#ifdef __linux__
#include <sys/sysinfo.h>
#endif
#ifdef _OPENMP
#include <omp.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#endif

int buffer_size = BUFFER_CHANNEL_SIZE; // total buffer size
static bool verbose = false;
const char *program_version = "process_extractspikes 0.8.1 (08-08-2012)";

/** Usage error **/
void error(const char* name) {
	cerr << program_version << endl;
	cerr << "usage: " << name << " [options] basename (type '" << name 
	<< " -h' for details)" << endl;
	exit(1);
} // error


/** Usage information **/
void help(const char* name) {
	cout << name << " -- Detect and extract spikes from high-pass filtered file"
	<< endl;
	cout << "usage: " << name << " [options] basename" << endl;
	cout << " basename\t\tSession file base name (i.e. without extention)" 
	<< endl;
	cout << " -t thresholds\t\tList of thresholds for spike detection "
	<< "(e.g. 10.4,11.2,10:54.1,51,57.2 for 2 groups with 3 channels each)"
	<< endl;
	cout << " -r refractoryPeriod\tRefractory period (i.e minimum time between"
	<< " 2 spikes)" << endl;
	cout << " -w waveformLength\tNumber of samples per waveform" << endl;
	cout << " -p peakSample\t\tPosition of the peak within the waveform, in"
	"number of samples" << endl;
	cout << " -n nChannels\t\tTotal number of channels in the file" << endl;
	cout << " -c channels\t\tList of channels for each group "
	<< "(e.g. 0,1,2:4,5,7 for 2 groups with 3 channels each)" << endl;
	cout << " -l peakSearchLength\t(size of the window within which to look for"
	<< " the peak; do not change this unless you know what you are doing)" 
	<< endl;
	cout << " -R\t\t\tre-extract mode: read existing basename.res.<grp>" << endl
	<< "\t\t\tand re-extract .spk waveforms at those exact timestamps" << endl
	<< "\t\t\t(no detection; threshold/refractory/-l ignored)" << endl;
//	cout << " -a look for the maxi absolute value to look for a spike peak" 
//	<< endl; // Cheat option
	cout << " -v\t\t\tverbose mode" << endl;
	cout << " -h\t\t\tdisplay help" << endl;
	cout << endl << "All arguments are mandatory (except -l peakSearchLength";
	cout << ")." << endl;
	exit(0);
} // help


// -R: re-extract the raw waveforms at the times in <base>.res.<g>
// (extractspikes_reextract.h; one .spk row per .res time).
static int runFromRes(const arguments &args,
                      int **channelList,
                      const int *channelNb_group,
                      int nbGroups,
                      bool verboseFlag)
{
	return reextractFromRes(args, channelList, channelNb_group, nbGroups, verboseFlag,
	                        [](int, neurosuite::core::SpikeWindowSpec&) {});   // raw copy
}


// Start here
int main(int argc,char *argv[]) {
	struct arguments arguments;
	arguments.totalChannelNumber = 0;
	arguments.peakLength = -1;
	arguments.refractoryPeriod = -1;
	arguments.spikeLength = -1;
	arguments.timeBeforeSpike = -1;

	arguments.isInputFileProvided = false;
	arguments.isOutputBaseFileProvided = false;
	arguments.isPeakLengthProvided = false;
	arguments.isRefractoryPeriodProvided = false;
	arguments.isTotalChannelNumberProvided = false;
	arguments.isChannelListProvided = false;
	arguments.isThresListProvided = false;
	arguments.isSpikeLengthProvided = false;
	arguments.isTimeBeforeSpikeProvided = false;
	arguments.isDisableAbs = true; // use real value as default
	arguments.useExistingRes = false; // patch86: -R flag flips this
	
	// buffers with current, previous and next datas value
	short *cur_buffer, *prev_buffer, *nextRec;
	// I/O files
	FILE *inputFile, **spikeTimeOutputFile, **spikeOutputFile;
	// Time ouput files name
	string *spikeTimeOutputFileName;
	// Length to extract after spike peak
	int timeAfterSpike = 0;
	unsigned int *nSpikeTot, nRecTot = 0;
	// nb of read records and nb of read buffers (to det the full position).
	unsigned long long rec_nb = 0, nbLoops = 0;
	// nb of record concerning the current spike in previous buffer
	int *recInPrevBuffer;
	// nb of rec to ignore in next buffer (too close of last peak)
	int *ignoredInNextBuffer;
	// current and previous buffer max. value index
	int *maxId, *prevBuffer_maxID;
	// is current and previous buffer max value negative
	bool *isNegativeMax, *prevBuffer_isNegativeMax;
	// is max in current buffer
	bool *isMaxInCurBuf;
	// Index of previous spike (calculate from the beginning of all datas)
	std::int64_t *lastSpikeFullId;
	// Id of the channel containing a spike in previous buffer (-1 if it is not)
	int *prevBuffer_spkChanId;
	// Id of the channel containing a possible spike (-1 if there is not)
	int *spkChanId;
	// TRUE if this is the last reading loop in input file, FALSE else
	bool isLastLoop = false;
	
	// Total Channels number
	int channelListSize = 0;
	// Number of electrodes groups
	int nbGroups = 0;
	// Store all channels Ids
	int **channelList = new int* [MAX_CHANNO];
	// store number of channels in each group
	int *channelNb_group = new int[MAX_CHANNO];
	
	// Number of threshold groups
	int nbThres = 0;
	// Store all thresholds
	double **thresList = new double* [MAX_CHANNO];
	// store number of thresholds in each group
	int *thresNb_group = new int[MAX_CHANNO];

#ifdef DEBUG
	FILE *debugFile = fopen("process_extractspikes.log", "w");
#endif

	parseArgs(argc, argv, arguments); // get arguments
	buffer_size *= arguments.totalChannelNumber; // increase buffer size
	inputFile = fopen(arguments.inputFileName,"rb");

	if ( verbose ) {
		cout << endl;
		cout << "Input File           = ";
		if (arguments.isInputFileProvided)
			cout << arguments.inputFileName << endl;
		else cout << "N/A" << endl;
		cout << "Output Base Name     = " << arguments.outputBaseFileName 
		<< endl;
		cout << "Peak Search Length   = " << arguments.peakLength << endl;
		cout << "Refractory Period    = " << arguments.refractoryPeriod << endl;
		cout << "Waveform Length      = " << arguments.spikeLength << endl;
		cout << "Peak Position        = " << arguments.timeBeforeSpike << endl;
		cout << "Disable absolute value for threshold = "
		<< arguments.isDisableAbs << endl;
		cout << "Buffer Size           = " << BUFFER_CHANNEL_SIZE << endl;
		cout << endl;
	} // if verbose

	// patch86: -R re-extracts at existing .res times, so the detection-only values
	// (refractory period, peak-search length, thresholds) are never consulted and
	// keep their -1 defaults.  checkInputs range-checks them, so running it in -R
	// mode rejected every extraction with "negative refractory period (-1)".
	// process_extractspikes_sdiff and _stderiv already gate the call this way.
	if(!arguments.useExistingRes && !checkInputs(arguments, buffer_size, inputFile)) // check arguments value
		exit(1);

	// init arrays
	for (int i=0;i<MAX_CHANNO;i++) {
		channelList[i]=new int[MAX_CHANNO]; // Channels
		channelNb_group[i] = -1;

		thresList[i]=new double[MAX_CHANNO]; // Thresholds
		thresNb_group[i] = -1;

		for (int j=0;j<MAX_CHANNO;j++) {
			channelList[i][j]=-1;
			thresList[i][j]=-1;
		} // for j
	} // for i

	// time to keep after spike peak (-1 due to the max record).
	timeAfterSpike = arguments.spikeLength - arguments.timeBeforeSpike - 1;

	// get channels groups
	nbGroups = getChannelsFromArg(channelNb_group, channelList, arguments);

	// patch86: in -R mode, threshold parsing/checks are not needed.
	// Dispatch to runFromRes immediately after channel parsing; the
	// detection state machine setup below is skipped.
	if(arguments.useExistingRes) {
		if(!arguments.isInputFileProvided || !inputFile) {
			cerr << "error: -R mode requires .fil input file." << endl;
			return 1;
		}
		// reextractFromRes opens the .fil itself; close this handle
		fclose(inputFile);
		inputFile = nullptr;
		int rc = runFromRes(arguments, channelList, channelNb_group,
		                    nbGroups, verbose);
		// Best-effort cleanup of the channel arrays before returning
		for(int i = 0; i < MAX_CHANNO; i++) {
			delete[] channelList[i];
			delete[] thresList[i];
		}
		delete[] channelList;
		delete[] channelNb_group;
		delete[] thresList;
		delete[] thresNb_group;
		return rc;
	}

	// get threshold groups
	nbThres = getThresholdsFromArg(thresNb_group, thresList, arguments);

	if(!checkChanAndThres(channelList, channelNb_group, nbGroups, 
						 thresNb_group, nbThres)) {
		cerr << "error: problem with lists of channels and/or thresholds." 
		<< endl;
		return 1;
	} // if
	
	/* init buffers */
	
	// +totChN for next Rec
	cur_buffer = new short[buffer_size+arguments.totalChannelNumber];
	// +totChN: the record after prev_buffer's last one (= cur_buffer's first
	// record, mirrored each loop), read when a peak search in the previous
	// buffer reaches its last record
	prev_buffer = new short[buffer_size+arguments.totalChannelNumber];
	// store next record
	nextRec = new short[arguments.totalChannelNumber];

	// init variables
	
	// id of the previous spike / group
	prevBuffer_spkChanId = new int[nbGroups];
	// id of the current spike / group
	spkChanId = new int[nbGroups];
	// is a negative max detect / group
	isNegativeMax = new bool[nbGroups];
	// is a negative max in previous buffer / group
	prevBuffer_isNegativeMax = new bool[nbGroups];
	// the peak value in current buffer / group
	isMaxInCurBuf = new bool[nbGroups];
	// id for the peak in current spike / group
	maxId = new int[nbGroups];
	// id for the current peak in previous buff/group
	prevBuffer_maxID = new int[nbGroups];
	// nb of records in previous buffer / group
	recInPrevBuffer = new int[nbGroups];
	// nb of records to ignore in next buffer
	ignoredInNextBuffer = new int[nbGroups];
	// last spike peak full id / group
	lastSpikeFullId = new std::int64_t[nbGroups];
	// total nb of spike / group
	nSpikeTot = new unsigned int[nbGroups];

	for (int g =0; g < nbGroups; g++) {
		prevBuffer_spkChanId[g] = -1;
		spkChanId[g] = -1;
		isNegativeMax[g] = false;
		prevBuffer_isNegativeMax[g] = false;
		isMaxInCurBuf[g] = true;
		maxId[g] = -1;
		prevBuffer_maxID[g] = -1;
		recInPrevBuffer[g] = 0;
		ignoredInNextBuffer[g] = 0;
		lastSpikeFullId[g] = -1;
		nSpikeTot[g] = 0;
	} // for g

	if ( verbose ) {
		for (int g =0; g < nbGroups; g++) {
			cout << "Group #"<<g+1<<" : "<< channelNb_group[g]
			<< "channels" << endl;
			cout << "IDs : ";
			for (int i=0; i < channelNb_group[g]; i++)
				cout << channelList[g][i] << " ";
			cout << endl;
		} // for g
		cout << endl << "<<----- Begin Spike Time Extraction" << endl;
		cout << endl;
	} // if verbose

	spikeTimeOutputFile = new FILE*[nbGroups];
	spikeTimeOutputFileName = new string[nbGroups];
	spikeOutputFile = new FILE*[nbGroups];

	// Per-group timestamp accumulators — filled during detection, written once
	// after the loop to avoid interleaving .res writes with .fil reads.
	// Reserve based on the theoretical maximum spike count (one spike per
	// refractory period across the whole file) to eliminate reallocations.
	std::vector<std::vector<int64_t>> resTimestamps(nbGroups);
	{
		neurosuite::core::fseek64(inputFile, 0, SEEK_END);
		const long long fileSamples =
		    neurosuite::core::ftell64(inputFile) / (arguments.totalChannelNumber * (long long)sizeof(short));
		neurosuite::core::fseek64(inputFile, 0, SEEK_SET);
		const size_t maxSpikes = (arguments.refractoryPeriod > 0)
		    ? (size_t)(fileSamples / arguments.refractoryPeriod) + 1
		    : (size_t)fileSamples;
		for(int g = 0; g < nbGroups; g++)
			resTimestamps[g].reserve(maxSpikes);
		if(verbose)
			cout << "Reserving " << maxSpikes << " timestamp slots per group"
			     << " (" << (maxSpikes * sizeof(int64_t) * nbGroups / 1024)
			     << " KB total)" << endl;
	}

	/************** SPIKES TIME DETECTION **************/
	// Build output file names (no file I/O yet — writes happen after the loop).
	for (int grp=0; grp<nbGroups; grp++) {
		if(channelNb_group[grp] == 0) // skip empty group
			continue;

		ostringstream oss;
		oss << arguments.outputBaseFileName << "." << SPIKE_TIME_OUT_EXT << ".";
		oss << (grp+1);
		spikeTimeOutputFileName[grp] = oss.str();

		if(verbose)
			cout << "Positions output file for group #"<<grp+1<<" : "
			<< spikeTimeOutputFileName[grp] << endl;
	} // for grp
	
	// Defrag-style stage progress over the .fil.  Only meaningful with a
	// seekable input file (not stdin streaming).
	BlockProgress prog(arguments.outputBaseFileName);
	long long progTotalSamples = 0;
	if(arguments.isInputFileProvided) {
		std::int64_t cur = neurosuite::core::ftell64(inputFile);
		neurosuite::core::fseek64(inputFile, 0, SEEK_END);
		progTotalSamples = neurosuite::core::ftell64(inputFile)
		    / (arguments.totalChannelNumber * (long long)sizeof(short));
		neurosuite::core::fseek64(inputFile, cur, SEEK_SET);
		prog.beginStage("DETECT", progTotalSamples);
	}

	// ── Double-buffered reader (file input only) ───────────────────────────
	// A background thread reads the next raw block from the .fil into one of
	// two shared buffers while the main thread runs the (OpenMP, per-group)
	// detection on the other — overlapping disk I/O with compute.  Output is
	// byte-identical to the serial path: only WHEN the bytes are read changes.
	// The carry-record assembly (prev_buffer / nextRec) is untouched; each
	// block is just memcpy'd from the prefetched slot instead of fread().
	const bool dblBuf = arguments.isInputFileProvided;
	short* dbSlot[2]  = { nullptr, nullptr };
	size_t dbRecNb[2] = { 0, 0 };
	std::mutex dbMtx;
	std::condition_variable dbFilled, dbFree;
	int  dbFilledCount = 0, dbWriteIdx = 0, dbReadIdx = 0;
	bool dbStop = false, dbReaderStarted = false;
	std::thread dbReader;
	if(dblBuf) {
		dbSlot[0] = new short[buffer_size];
		dbSlot[1] = new short[buffer_size];
	}
	// Producer: sequential blocks into alternating slots; the fread runs
	// outside the lock so it overlaps the consumer's detection.
	auto dbReaderFn = [&]() {
		for(;;) {
			int idx;
			{
				std::unique_lock<std::mutex> lk(dbMtx);
				dbFree.wait(lk, [&]{ return dbFilledCount < 2 || dbStop; });
				if(dbStop) return;
				idx = dbWriteIdx;
			}
			size_t got = fread(dbSlot[idx], sizeof(short),
			                   buffer_size, inputFile);
			bool eof = (got < (size_t)buffer_size);
			{
				std::lock_guard<std::mutex> lk(dbMtx);
				dbRecNb[idx] = got;
				dbWriteIdx  ^= 1;
				dbFilledCount++;
				dbFilled.notify_one();
			}
			if(eof) return;          // short read = EOF; last block delivered
		}
	};

	// Search spikes position loop.  With prefetch on, feof(inputFile) tracks
	// the reader thread (which runs ahead), so termination is driven solely by
	// isLastLoop (set from the consumed block's short read); the serial path
	// keeps the original feof guard.
	while(!isLastLoop && (dblBuf || !feof(inputFile))) {
		
		if(nbLoops == 0) {
			// Store 1st records in nextRec for the init phase
			if(arguments.isInputFileProvided) {
				{ size_t r_ = fread(nextRec, sizeof(short),
				            arguments.totalChannelNumber, inputFile); (void)r_; }
			} else {
				{ size_t r_ = fread(nextRec, sizeof(char),
				            arguments.totalChannelNumber, stdin); (void)r_; }
			} // else
		} else {
			// store previous buffer
			memcpy(prev_buffer, cur_buffer, sizeof(short)*buffer_size);
		} // if nbLoops

		// Get a datas buffer
		if(dblBuf) {
			// Prefetch path: pull the next block from the reader thread (launched
			// lazily, after the prime read above so the file offset is correct).
			if(!dbReaderStarted) { dbReader = std::thread(dbReaderFn); dbReaderStarted = true; }
			std::unique_lock<std::mutex> lk(dbMtx);
			dbFilled.wait(lk, [&]{ return dbFilledCount > 0; });
			const int idx = dbReadIdx;
			rec_nb = dbRecNb[idx];
			memcpy(cur_buffer+arguments.totalChannelNumber, dbSlot[idx],
			       sizeof(short)*rec_nb);
			dbReadIdx ^= 1;
			dbFilledCount--;
			dbFree.notify_one();
		} else {
			rec_nb = fread(cur_buffer+arguments.totalChannelNumber,
						   sizeof(char), buffer_size, stdin);
		} // else
		
		// add the first record (ie the old "next record")
		memcpy(cur_buffer, nextRec, sizeof(short)*arguments.totalChannelNumber);
		// successor of the previous buffer's last record
		memcpy(prev_buffer+buffer_size, cur_buffer,
			   sizeof(short)*arguments.totalChannelNumber);
		
		// there is no next record (ie = last records)
		if(rec_nb < buffer_size || (!dblBuf && feof(inputFile))) {
			isLastLoop = true;
			nextRec = NULL;
			rec_nb += arguments.totalChannelNumber; // correct records number
		}
		else { // store next record
			memcpy(nextRec, cur_buffer+buffer_size, 
				   sizeof(short)*arguments.totalChannelNumber);
		} // if rec_nb
		
		/* Analyse each channel group in parallel.
		 * Safety: all per-group state is stored in arrays indexed by grp
		 * (spkChanId[grp], maxId[grp], etc.) with one independent slot per
		 * group.  Each group has its own resTimestamps[grp] accumulator.
		 * cur_buffer and prev_buffer are read-only inside this loop.
		 * rec_nb is set before this loop and is read-only here.
		 */
#ifdef _OPENMP
		#pragma omp parallel for schedule(dynamic)
#endif
		for(int grp = 0; grp < nbGroups; grp++) {
			if(channelNb_group[grp] == 0) // skip empty group
				continue;
			
			int i = 0;
			// start analyzing after the given time before spike (only at the
			// begening)
			if(nbLoops == 0)
				i = arguments.timeBeforeSpike*arguments.totalChannelNumber;
			else
				i = ignoredInNextBuffer[grp]*arguments.totalChannelNumber;
					//+ arguments.totalChannelNumber; // start after ignored rec
			
			ignoredInNextBuffer[grp] = 0; // reset nb of next buffer ignored rec
			
			// read buffer
			while (i < rec_nb) {
				
				if(spkChanId[grp] == -1) {// no spike actually
					spkChanId[grp]=searchSpikeInChannels(&cur_buffer[i], 
														 channelNb_group[grp],
														 channelList[grp],
														 thresList[grp],
														 isNegativeMax[grp]);
				}
				
				if(spkChanId[grp] > -1) {// threshold is reached
					// Max index for detecting a maximum (~peak)
					int maxEndSpike = i
					+ (arguments.peakLength-recInPrevBuffer[grp]-1)
					* arguments.totalChannelNumber;
					
					// Searching the max id in cur buffer (RELATIVE index /!\)
					if(maxEndSpike >= rec_nb) { // end of spike in next buffer
						// previous record
						std::vector<short> prevVals(arguments.totalChannelNumber);
						memcpy(prevVals.data(), cur_buffer+i-arguments.totalChannelNumber,
							   arguments.totalChannelNumber*sizeof(short));
						
						if(arguments.isDisableAbs) {
							maxId[grp] = lookForMaxNegative(cur_buffer, i, 
												rec_nb-1,
												spkChanId[grp],
												channelNb_group[grp], 
												channelList[grp],
												arguments.totalChannelNumber,
												thresList[grp], prevVals.data(),
												isNegativeMax[grp]);
							prevBuffer_isNegativeMax[grp] = isNegativeMax[grp];
							isNegativeMax[grp] = false;
							
						} else {
							maxId[grp] = lookForMax(cur_buffer, i, rec_nb-1, 
												spkChanId[grp],
												channelNb_group[grp],
												channelList[grp],
												arguments.totalChannelNumber,
												prevVals.data());
						}
					
						if(maxId[grp] != -1) {
							double prevVal = cur_buffer[(maxId[grp]
								- arguments.totalChannelNumber
								+ spkChanId[grp])];
							
							double nextVal; // rec after max
							
							// next rec is current buffer 1st rec
							if((maxId[grp] + 
								arguments.totalChannelNumber) ==
								buffer_size) {
									if(!isLastLoop) {
										nextVal = nextRec[spkChanId[grp]];
									}
							}
							// next rec is in previous buffer
							else {
								nextVal = cur_buffer[(maxId[grp]
									+ arguments.totalChannelNumber
									+ spkChanId[grp])];
							}
							
							if(isRealPeak(cur_buffer[maxId[grp]+spkChanId[grp]],
										prevVal,
										nextVal) ) {
								prevBuffer_maxID[grp] = maxId[grp];
								prevBuffer_spkChanId[grp] = spkChanId[grp];
							} // if isRealPeak
						} // if maxId != -1
						
						recInPrevBuffer[grp] = (rec_nb-i) /
												arguments.totalChannelNumber;
						maxId[grp] = -1;  // reset MAX value for next buffer
						i = buffer_size; // go to the end of the buffer
					
					// spike is in current buffer (if it is a significant one)
					} else {
						std::int64_t maxFullId = -1;
						// previous records
						std::vector<short> prevVals(arguments.totalChannelNumber);
						
						// previous record in current buffer
						if(i >= arguments.totalChannelNumber) {
							memcpy(prevVals.data(), cur_buffer+i-arguments.totalChannelNumber,
									arguments.totalChannelNumber*sizeof(short));
						} else { // previous record is the last of previous buffer
							memcpy(prevVals.data(), prev_buffer+buffer_size
									-arguments.totalChannelNumber, 
									arguments.totalChannelNumber*sizeof(short));
						}
						
						// Look prioritary for a negative peak
						if(arguments.isDisableAbs) {
							maxId[grp] = lookForMaxNegative(cur_buffer, i, 
												maxEndSpike,
												spkChanId[grp],
												channelNb_group[grp],
												channelList[grp],
												arguments.totalChannelNumber,
												thresList[grp], prevVals.data(),
												isNegativeMax[grp]);
							
							double thr = getThresholdFromChan(spkChanId[grp],
												channelNb_group[grp],
												channelList[grp],
												thresList[grp]);
							double prevBuffer_thr = -1;
							if(prevBuffer_spkChanId[grp] != -1) {
								prevBuffer_thr = getThresholdFromChan(
												prevBuffer_spkChanId[grp],
												channelNb_group[grp],
												channelList[grp],
												thresList[grp]);
							}
							
							maxFullId = getNegativePeakFullId(maxId[grp], 
												cur_buffer,
												isNegativeMax[grp],
												spkChanId[grp],
												prevBuffer_maxID[grp], 
												prev_buffer,
												prevBuffer_isNegativeMax[grp],
												prevBuffer_spkChanId[grp],
												buffer_size,
												arguments.totalChannelNumber,
												nbLoops, thr, prevBuffer_thr,
												isMaxInCurBuf[grp]);
						}
						else { // Use absolute value for peak detection
							maxId[grp] = lookForMax(cur_buffer, i, maxEndSpike, 
												spkChanId[grp],
												channelNb_group[grp],
												channelList[grp],
												arguments.totalChannelNumber,
												prevVals.data());
							
							double thr = getThresholdFromChan(spkChanId[grp],
												channelNb_group[grp],
												channelList[grp],
												thresList[grp]);
							
							double prevBuffer_thr = -1;
							if(prevBuffer_spkChanId[grp] != -1) {
								prevBuffer_thr =  getThresholdFromChan(
												prevBuffer_spkChanId[grp],
												channelNb_group[grp],
												channelList[grp],
												thresList[grp]);
							}
							
							maxFullId = getPeakFullId(maxId[grp], cur_buffer, 
												spkChanId[grp],
												prevBuffer_maxID[grp],
												prev_buffer,
												prevBuffer_spkChanId[grp],
												buffer_size,
												arguments.totalChannelNumber,
												nbLoops,
												thr, prevBuffer_thr,
												isMaxInCurBuf[grp]);
						} // if isDisableAbs
						
						// true if new spike detection start
						bool isKeepPrevMax = false;
						
						// there is maybe a spike
						if(maxFullId > -1)  {
							// There is a spike !
							if(!isRefractoryPeriod( lastSpikeFullId[grp],
													maxFullId,
													arguments)) {
								// can extract spike (not eof)
								if(!(isLastLoop &&
									isMaxInCurBuf[grp] &&
									(maxId[grp] +
									(timeAfterSpike *
									arguments.totalChannelNumber)) >= rec_nb)) {
									
										// Accumulate timestamp — written to .res after detection loop.
										resTimestamps[grp].push_back(
											(int64_t)(maxFullId / arguments.totalChannelNumber));
								      lastSpikeFullId[grp] = maxFullId;
								      nSpikeTot[grp]++; // One spike more !
								} // if(!(isLastLop & ...) // not eof
							} // if isRefractoryPeriod
							
							// next spike detection is in current or next buffer
							if(isMaxInCurBuf[grp]) {
								maxEndSpike = maxId[grp] + (arguments.peakLength
											- arguments.timeBeforeSpike - 1)
											* arguments.totalChannelNumber;
								
								if(maxEndSpike >= buffer_size) {
									ignoredInNextBuffer[grp] = 1
												+ (maxEndSpike - buffer_size)
												/ arguments.totalChannelNumber;
								}
							}
							// next spk detection in prev buffer or in cur one
							else if (prevBuffer_maxID[grp] >= 0) {
								// end of last detected spike
								maxEndSpike = prevBuffer_maxID[grp] 
											+ (arguments.peakLength 
											- arguments.timeBeforeSpike - 1) 
											* arguments.totalChannelNumber;
								
								// new spike search in current buffer
								if((maxEndSpike+arguments.totalChannelNumber) >=
									buffer_size) {
									// end of previous spike (new search :
									// i+totalChannelNumber)
									maxEndSpike -= buffer_size;
									isKeepPrevMax = false;
								}
								// new spike search start in previous buffer
								else {
									// start of new spike search
									int x = maxEndSpike
											+ arguments.totalChannelNumber;
									// no more MAX value
									maxId[grp] = -1; isNegativeMax[grp] = false;
									prevBuffer_spkChanId[grp] = -1;
									prevBuffer_maxID[grp] = -1;
									spkChanId[grp] = -1; // no more channel
									
									while(x < buffer_size && 
										  spkChanId[grp] == -1) {
												spkChanId[grp]=
													searchSpikeInChannels(
														&prev_buffer[x],
														channelNb_group[grp],
														channelList[grp],
														thresList[grp],
														isNegativeMax[grp]);
										x += arguments.totalChannelNumber;
									} // while
									
									// new spk start in prev buffer
									if(spkChanId[grp] != -1) {
										// index of the 1st record > thr
										x -=  arguments.totalChannelNumber;
										// previous record
										std::vector<short> prevVals(arguments.totalChannelNumber);
										memcpy(prevVals.data(), prev_buffer+x-arguments.totalChannelNumber,
												arguments.totalChannelNumber*sizeof(short));
										
										if(arguments.isDisableAbs) {
											maxId[grp] = lookForMaxNegative(
												prev_buffer, x, buffer_size-1,
												spkChanId[grp], 
												channelNb_group[grp],
												channelList[grp],
												arguments.totalChannelNumber,
												thresList[grp], prevVals.data(),
												isNegativeMax[grp]);
											prevBuffer_isNegativeMax[grp] = 
															isNegativeMax[grp];
										} else {
											maxId[grp] = lookForMax(prev_buffer,
												x, buffer_size-1,
												spkChanId[grp],
												channelNb_group[grp], 
												channelList[grp],
												arguments.totalChannelNumber,
												prevVals.data());
										} // if isDisableAbs
										
										if(maxId[grp] != -1) {
											double prevVal = prev_buffer[(maxId[grp]
												- arguments.totalChannelNumber
												+ spkChanId[grp])];
											
											double nextVal; // rec after max
											// next rec is current buffer 1st rec
											if((maxId[grp] + 
												arguments.totalChannelNumber) ==
												buffer_size) {
												
												nextVal=cur_buffer[spkChanId[grp]];
											}
											// next rec is in previous buffer
											else {
												nextVal = prev_buffer[(maxId[grp]
													+ arguments.totalChannelNumber
													+ spkChanId[grp])];
											}
											
											if(isRealPeak(
												prev_buffer[maxId[grp]+
												spkChanId[grp]],
												prevVal,
												nextVal)) {
												
												prevBuffer_maxID[grp] = maxId[grp];
												prevBuffer_spkChanId[grp] = 
																	spkChanId[grp];
											} // if isRealPeak
										} // if maxId != -1
										recInPrevBuffer[grp] = (buffer_size - x)
												/ arguments.totalChannelNumber;
										// do not initialize previous buffer par
										isKeepPrevMax = true;
									} // if spkChanId != -1
									// reset to the 1st rec of cur buffer
									// (add totalChannelNumber to this value)
									maxEndSpike = -arguments.totalChannelNumber;
								} // if spike in current buffer
							} // if isMaxInCurBuf
						} // if maxFullId

						i = maxEndSpike;
						
						// Reset all variables
						maxId[grp] = -1;  // no more MAX value
						// no more MAX value
						isNegativeMax[grp] = false;  isMaxInCurBuf[grp] = true;
						maxEndSpike = -1; isMaxInCurBuf[grp] = true;
						
						if(!isKeepPrevMax) {
							spkChanId[grp] = -1; prevBuffer_spkChanId[grp] = -1;
							// nothing in old buffer
							recInPrevBuffer[grp] = 0; 
							prevBuffer_maxID[grp] = -1; 
							prevBuffer_isNegativeMax[grp] = false;
						}
					} // if maxEndSpike >= rec_nb (spike in current buffer)
				} // if spkChanId[grp] > -1

				i+=arguments.totalChannelNumber;
			} // while i (current buffer)
		} // for grp
		
		nRecTot += rec_nb/arguments.totalChannelNumber;
		nbLoops++; // one more loop
		if(arguments.isInputFileProvided) prog.setPosition(nRecTot);
	} // while (inputFile)

	// Tear down the prefetch reader (it normally exits on EOF; dbStop covers
	// an early loop break).
	if(dblBuf) {
		{ std::lock_guard<std::mutex> lk(dbMtx); dbStop = true; }
		dbFree.notify_all();
		if(dbReader.joinable()) dbReader.join();
		delete[] dbSlot[0];
		delete[] dbSlot[1];
	}

	if(arguments.isInputFileProvided) prog.endStage();

	// Write all .res files now — detection loop is complete, no more
	// .fil reads in flight, so .res writes are fully sequential.
	for(int g = 0; g < nbGroups; g++) {
		if(channelNb_group[g] == 0)
			continue;
		FILE *resFile = fopen(spikeTimeOutputFileName[g].c_str(), "wb");
		if(!resFile) {
			fprintf(stderr, "error: cannot write %s\n",
			        spikeTimeOutputFileName[g].c_str());
			exit(1);
		}
		if(!resTimestamps[g].empty())
			fwrite(resTimestamps[g].data(), sizeof(int64_t),
			       resTimestamps[g].size(), resFile);
		fclose(resFile);
	} // for g

	cout << "Number of spikes:" << endl;
	for (int g = 0; g < nbGroups; g++)
		cout << "- Group " << g+1 << " = " << nSpikeTot[g] << endl;
	if ( verbose ) {
		cout << "Total number of records : " << nRecTot << endl;
		cout << endl << " End Of Spike Time Extraction ----->>" << endl;
		cout << endl;
		cout << "Total number of channels  = " << arguments.totalChannelNumber
		<< endl;
		cout << "List of channels          = " << arguments.channelList << endl;
		cout << "List of thresholds        = " << arguments.thresList << endl;
		cout << "Number of groups          = " << nbGroups << endl;
		cout << "Number of channels        = " << channelListSize << endl;
		cout << "Number of loops           = " << nbLoops << endl;
	}

	delete[] prev_buffer; delete[] nextRec;
	delete[] spkChanId; delete[] prevBuffer_spkChanId; delete[] isNegativeMax;
	delete[] maxId; delete[] prevBuffer_maxID;
	delete[] prevBuffer_isNegativeMax; delete[] isMaxInCurBuf;
	delete[] recInPrevBuffer; delete[] ignoredInNextBuffer;
	delete[] lastSpikeFullId; delete[] nSpikeTot;

	/************** SPIKE RECORDS EXTRACTION **************/
	if ( verbose ) {
		cout << endl << "<<----- Begin Spike Records Extraction " << endl;
		cout << endl;
	} // end if

	// cur_buffer from Pass 1 is no longer needed.
	delete[] cur_buffer; cur_buffer = nullptr;
	// spikeTimeOutputFile was allocated but never used for I/O in Pass 1
	// (timestamps were accumulated in resTimestamps instead).
	delete[] spikeTimeOutputFile; spikeTimeOutputFile = nullptr;

	/* Pass 2: extract waveforms — single sequential read, then sequential write.
	 *
	 * STRATEGY: single-pass read → sequential write
	 * -----------------------------------------------
	 * Previous approach: each group made its own forward-sweep pass over the
	 * .fil (N passes total, even if sorted by offset within each group).
	 * N concurrent read streams compete for the same device queue.
	 *
	 * New approach:
	 *   Phase A  — read all .res files, build per-group waveform buffers and
	 *              a merged event list across all groups.  Groups that exceed
	 *              MEM_CAP_BYTES get their .spk file opened for streaming.
	 *
	 *   Phase A2 — single sequential pass through the .fil.
	 *              The merged event list is sorted by file offset so the
	 *              single FILE* moves strictly forward — true sequential I/O,
	 *              zero seeks.  Each frame is dispatched to the correct group's
	 *              waveform buffer (in-memory groups) or written directly to
	 *              the .spk file (over-cap groups).
	 *
	 *   Phase B  — sequential writes for in-memory groups (one fwrite each).
	 *              No reads in flight — device sees clean sequential writes.
	 *
	 * One file open, one forward read from byte 0 to the last spike window.
	 * The single sequential read saturates NVMe bandwidth rather than making
	 * N independent seek-and-read sweeps over the same file.
	 */

	// Maximum waveform buffer per group (default 2 GB). Override via environment
	// variable PROCESS_EXTRACTSPIKES_MEM_CAP_GB if needed.
	const char *_memCapEnv = getenv("PROCESS_EXTRACTSPIKES_MEM_CAP_GB");
	const long long MEM_CAP_BYTES = _memCapEnv
	    ? (long long)(atof(_memCapEnv) * 1024LL * 1024 * 1024)
	    : 2LL * 1024 * 1024 * 1024;
	fprintf(stderr, "info: waveform memory cap = %.1f GB  (PROCESS_EXTRACTSPIKES_MEM_CAP_GB=%s)\n",
	        MEM_CAP_BYTES / (1024.0 * 1024.0 * 1024.0),
	        _memCapEnv ? _memCapEnv : "<not set, defaulting to 2 GB>");

	// One entry per spike across all groups, used to drive the sequential pass.
	struct SpikeEvent {
		std::int64_t fileOffset; // byte offset of waveform start in .fil
		int   grp;        // which group owns this spike
		int   origIdx;    // chronological index within that group
	};

	// Per-group waveform buffers (in-memory groups only).
	std::vector<std::vector<short>> allWaveforms(nbGroups);
	// Track which groups fit in memory.
	std::vector<bool> useInMemory(nbGroups, false);
	// Output file names.
	std::vector<std::string> spkFileNames(nbGroups);
	// .spk FILE* for over-cap groups — opened in Phase A, written in Phase A2.
	std::vector<FILE*> fallbackSpkFiles(nbGroups, nullptr);

	// ── Phase A: size buffers and build merged event list from in-memory ────
	// resTimestamps[] is already populated from Pass 1 — no .res file I/O.
	// Pre-size allEvents exactly so no reallocation occurs during population.
	size_t _totalSpikes = 0;
	for(int g = 0; g < nbGroups; g++) _totalSpikes += resTimestamps[g].size();
	std::vector<SpikeEvent> allEvents;
	allEvents.reserve(_totalSpikes);


	for(int grp = 0; grp < nbGroups; grp++) {
		if(channelNb_group[grp] == 0)
			continue;

		const int nChanGrp  = channelNb_group[grp];
		const int wavLen    = arguments.spikeLength * nChanGrp;
		const int nSpikes   = (int)resTimestamps[grp].size(); // exact, from Pass 1
		const long long waveformBytes = (long long)nSpikes * wavLen * sizeof(short);

		{
			ostringstream oss;
			oss << arguments.outputBaseFileName << "." << SPIKE_REC_OUT_EXT
			    << "." << (grp+1);
			spkFileNames[grp] = oss.str();
		}

		fprintf(stderr,
		        "info: group %d  spikes=%d  waveform buffer=%.1f MB  cap=%.1f GB  -> %s\n",
		        grp+1, nSpikes,
		        waveformBytes / (1024.0 * 1024.0),
		        MEM_CAP_BYTES / (1024.0 * 1024.0 * 1024.0),
		        waveformBytes <= MEM_CAP_BYTES ? "in-memory" : "streaming fallback");

		if(waveformBytes > MEM_CAP_BYTES) {
			fprintf(stderr,
			        "warning: group %d waveform buffer (%.1f MB) exceeds cap "
			        "(%.1f MB); will stream directly to .spk during read pass\n",
			        grp,
			        waveformBytes / (1024.0 * 1024.0),
			        MEM_CAP_BYTES  / (1024.0 * 1024.0));
			useInMemory[grp] = false;
			fallbackSpkFiles[grp] = fopen(spkFileNames[grp].c_str(), "wb");
			if(!fallbackSpkFiles[grp]) {
				fprintf(stderr, "error: cannot open '%s' for writing\n",
				        spkFileNames[grp].c_str());
				exit(1);
			}
		} else {
			useInMemory[grp] = true;
			// Exact allocation: nSpikes × (spikeLength × nChanGrp) shorts.
			allWaveforms[grp].resize((size_t)(nSpikes * wavLen));
		}

		// Append this group's spikes to the merged event list.
		for(int i = 0; i < nSpikes; i++) {
			SpikeEvent ev;
			ev.fileOffset = ((std::int64_t)(resTimestamps[grp][i] - arguments.timeBeforeSpike)
			                 * (std::int64_t)arguments.totalChannelNumber
			                 * (std::int64_t)sizeof(short));
			ev.grp     = grp;
			ev.origIdx = i;
			allEvents.push_back(ev);
		}

		if(verbose)
			cout << "Phase A  group " << grp+1
			     << "  spikes=" << nSpikes
			     << "  " << (useInMemory[grp] ? "buffered" : "streaming")
			     << "  -> " << spkFileNames[grp] << "\n";
	} // Phase A

	// ── Phase A2: single sequential pass through .fil ────────────────────────
	// Sort merged event list by file offset — the FILE* will move strictly
	// forward, never seeking.
	std::sort(allEvents.begin(), allEvents.end(),
	          [](const SpikeEvent &a, const SpikeEvent &b){
	              return a.fileOffset < b.fileOffset;
	          });

	if(verbose)
		cout << "Phase A2  single sequential read  total events="
		     << allEvents.size() << "\n";

	{
		const int rawLen = arguments.spikeLength * arguments.totalChannelNumber;
		std::vector<short> frameBuf(rawLen);
		// Discard buffer for skipping gaps — reuse frameBuf-sized chunks.
		const size_t discardChunk = (size_t)rawLen * sizeof(short);

		FILE *seqFile = fopen(arguments.inputFileName, "rb");
		if(!seqFile) {
			fprintf(stderr, "error: cannot open '%s' for sequential read\n",
			        arguments.inputFileName);
			exit(1);
		}

		std::int64_t filePos = 0;

		const std::int64_t progFileBytes = (std::int64_t)progTotalSamples
		    * arguments.totalChannelNumber * (std::int64_t)sizeof(short);
		if(arguments.isInputFileProvided)
			prog.beginStage("EXTRACT", progFileBytes);

		for(const SpikeEvent &ev : allEvents) {
			if(arguments.isInputFileProvided) prog.setPosition(filePos);
			const int grp      = ev.grp;
			const int nChanGrp = channelNb_group[grp];
			const int wavLen   = arguments.spikeLength * nChanGrp;

			// Two windows closer than spikeLength samples (in any two groups --
			// the event list merges them all) overlap: the previous read has
			// already passed this window's start.  Seek back to it.  This used
			// to read on from the current position instead, so the waveform of
			// every spike within a window's length after another one started
			// late, by up to spikeLength samples, against its .res time.
			if(ev.fileOffset < filePos) {
				if(neurosuite::core::fseek64(seqFile, ev.fileOffset, SEEK_SET) != 0) {
					fprintf(stderr, "error: cannot seek to offset %lld\n",
					        (long long)ev.fileOffset);
					fclose(seqFile);
					exit(1);
				}
				filePos = ev.fileOffset;
			}
			// Skip forward to this waveform's start by reading and discarding
			// the gap, so the reads stay sequential between overlaps.
			if(ev.fileOffset > filePos) {
				std::int64_t gap = ev.fileOffset - filePos;
				while(gap > 0) {
					size_t toRead = (gap < (std::int64_t)discardChunk)
					                ? (size_t)gap : discardChunk;
					size_t got = fread(frameBuf.data(), 1, toRead, seqFile);
					if(got == 0) break;
					gap     -= (std::int64_t)got;
					filePos += (std::int64_t)got;
				}
			}

			// Read the waveform window.
			size_t got = fread(frameBuf.data(), sizeof(short), rawLen, seqFile);
			if((int)got != rawLen) {
				if(feof(seqFile)) {
					// Waveform window extends past EOF — the last spike(s) in the
					// recording land too close to the end of the file for a full
					// waveform to be read.  allEvents is sorted by fileOffset so
					// all remaining events are also at or past EOF.  Warn and stop.
					size_t remaining = (size_t)(&allEvents.back() - &ev);
					fprintf(stderr,
					        "warning: waveform window for group %d at offset %lld "
					        "extends past end of file (%zu/%d samples); "
					        "skipping last %zu event(s)\n",
					        grp, (long long)ev.fileOffset,
					        got, rawLen, remaining + 1);
					break;
				}
				fprintf(stderr,
				        "error: short read at offset %lld group %d "
				        "(got %zu, want %d)\n",
				        (long long)ev.fileOffset, grp, got, rawLen);
				fclose(seqFile);
				exit(1);
			}
			filePos += (std::int64_t)rawLen * (std::int64_t)sizeof(short);

			// Dispatch to the correct group's destination.
			if(useInMemory[grp]) {
				// Store at chronological slot — buffer is already in order.
				short *dst = allWaveforms[grp].data()
				             + (size_t)ev.origIdx * wavLen;
				for(int s = 0; s < arguments.spikeLength; s++) {
					const short *frame = frameBuf.data()
					                     + s * arguments.totalChannelNumber;
					for(int c = 0; c < nChanGrp; c++)
						dst[s * nChanGrp + c] = frame[channelList[grp][c]];
				}
			} else {
				// Over-cap group: write directly to .spk.
				// Chronological order is preserved because spike timestamps are
				// monotonically increasing, so fileOffset is also monotonically
				// increasing within each group.  The global sort by fileOffset
				// therefore visits each group's spikes in their original
				// chronological sequence — no secondary sort needed.
				for(int s = 0; s < arguments.spikeLength; s++) {
					const short *frame = frameBuf.data()
					                     + s * arguments.totalChannelNumber;
					for(int c = 0; c < nChanGrp; c++)
						fwrite(&frame[channelList[grp][c]], sizeof(short), 1,
						       fallbackSpkFiles[grp]);
				}
			}
		} // for allEvents

		fclose(seqFile);
	} // Phase A2

	if(arguments.isInputFileProvided) {
		prog.endStage();
		prog.finish();
	}

	// Close fallback .spk files written during the read pass.
	for(int grp = 0; grp < nbGroups; grp++) {
		if(fallbackSpkFiles[grp]) {
			fclose(fallbackSpkFiles[grp]);
			fallbackSpkFiles[grp] = nullptr;
		}
	}

	// ── Phase B: sequential writes for in-memory groups ─────────────────────
	// All reads are complete; write each group's buffer to its .spk file one
	// at a time — no concurrent reads, clean sequential writes.
	for(int grp = 0; grp < nbGroups; grp++) {
		if(channelNb_group[grp] == 0 || !useInMemory[grp])
			continue;

		const int nChanGrp = channelNb_group[grp];
		const int wavLen    = arguments.spikeLength * nChanGrp;
		const int nSpikes   = (int)(allWaveforms[grp].size() / wavLen);

		if(verbose)
			cout << "Phase B  group " << grp+1
			     << "  writing " << nSpikes << " spikes -> "
			     << spkFileNames[grp] << "\n";

		FILE *spkFile = fopen(spkFileNames[grp].c_str(), "wb");
		if(!spkFile) {
			fprintf(stderr, "error: cannot open '%s' for writing\n",
			        spkFileNames[grp].c_str());
			exit(1);
		}
		if(nSpikes > 0)
			fwrite(allWaveforms[grp].data(), sizeof(short),
			       (size_t)(nSpikes * wavLen), spkFile);
		fclose(spkFile);

		allWaveforms[grp].clear();
		allWaveforms[grp].shrink_to_fit();

	} // Phase B

	if ( verbose ) {
		cout << endl << " End Of Spike Records Extraction ----->>" << endl;
		cout << endl;
	} // end if
	
	//************** FREE VARIABLES **************
	if(arguments.isInputFileProvided)
		fclose ( inputFile );
	
	// Free Memory
	delete[] channelNb_group;
	delete[] thresNb_group;
	for(int i=0;i<MAX_CHANNO;i++) {
		delete[] channelList[i];
		delete[] thresList[i];
	}
	delete[] channelList;
	delete[] thresList;

	delete[] spikeTimeOutputFileName;
	// spikeTimeOutputFile freed above; spikeOutputFile managed within Pass 2.
	delete[] spikeOutputFile;

	if ( verbose ) cout << endl;

#ifdef DEBUG
	fclose(debugFile);
#endif

	return 0;
} // main


// isRefractoryPeriod, isRealPeak, getNegativePeakFullId, getPeakFullId,
// lookForMaxNegative, lookForMax, searchSpikeInChannels and getThresholdFromChan
// are in extractspikes_common/extractspikes_detect.h, shared with the
// spatial-derivative extractors.


/**
 * Fill arguments values from user's argv
 * @param argc number of arguments
 * @param argv list of arguments
 * @param arguments user arguments to fill
 */
void parseArgs(const int argc, char **argv, arguments &arguments) {
	// Parse command-line
	int nOptions = argc;
	int i;

	if(nOptions < 2)
		error(argv[0]);
	
	for ( i = 1 ; i < nOptions ; ++i ) {
		if ( argv[i][0] != '-' ) break;
		if ( strlen(argv[i]) < 2 ) error(argv[0]);
		switch ( argv[i][1] ) {

			case 'h': // help
				help(argv[0]);
				break;

			case 't': // thresholds
				if ( i+1 > nOptions ) error(argv[0]);
 				arguments.thresList = argv[++i];
				arguments.isThresListProvided = true;
				break;

			case 'l': // spike length (search window length)
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.peakLength = atoi(argv[++i]);
				arguments.isPeakLengthProvided = true;
				break;

			case 'r': // refractory period
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.refractoryPeriod = atoi(argv[++i]);
				arguments.isRefractoryPeriodProvided = true;
				break;

			case 'w': // Extraction window length
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.spikeLength = atoi(argv[++i]);
				arguments.isSpikeLengthProvided = true;
				break;

			case 'p': // peak position into the extracted spike
				if ( i+1 > nOptions ) error(argv[0]);
				// shift to extract before the spike peak
				arguments.timeBeforeSpike = atoi(argv[++i])-1;
				arguments.isTimeBeforeSpikeProvided = true;
				break;

			case 'n': // Total number of channels
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.totalChannelNumber = atoi(argv[++i]);
				arguments.isTotalChannelNumberProvided = true;
				break;

			case 'c': // List of channels for each group
				if ( i+1 > nOptions ) error(argv[0]);
				arguments.channelList = argv[++i];
				arguments.isChannelListProvided = true;
				break;

			
			case 'a': // use Absolute value for threshold
				arguments.isDisableAbs = false;
				break;

			case 'R': // patch86: re-extract using existing .res files (skip detection)
				arguments.useExistingRes = true;
				break;

			case 'v': // verbose mode
				verbose = true;
				break;

			default:
				cerr << "error: unknown option '" << argv[i] << "'." << endl;
				exit(1);
				break;
		}
	} // for
	
	// input file
	if ( i >= argc ) {
		cerr << "error: missing session file basename." << endl;
		exit(1);
	}
	arguments.outputBaseFileName = argv[i];
	arguments.isOutputBaseFileProvided = true;
	// The help used to offer "basename -" to read stdin, but that never worked:
	// the test for it could not be true, so basename.fil was always opened,
	// and the extraction pass reads the file a second time by name, which a
	// pipe cannot give.  Say so instead of silently reading basename.fil.
	if (i + 1 < argc && strcmp(argv[i + 1], "-") == 0) {
		cerr << "error: reading the signal from stdin is not supported; "
		        "give the session basename (basename.fil is read twice)." << endl;
		exit(1);
	}
	{
		arguments.isInputFileProvided = true;
		{
			size_t baseLen = strlen(arguments.outputBaseFileName);
			arguments.inputFileName = new char[baseLen + 5]; // +4 for ".fil" +1 for NUL
			memcpy(arguments.inputFileName, arguments.outputBaseFileName, baseLen);
			memcpy(arguments.inputFileName + baseLen, ".fil", 5);
		}
	}
	
	// Make sure we get the correct arguments.
	// patch86: in -R mode, detection-only args (threshold, refractory,
	// peakLength) are not consulted, so don't require them.
	if(!arguments.isThresListProvided && !arguments.useExistingRes)  {
		cerr << "error: missing list of thresholds." << endl;
		exit(1);
	}
	if(!arguments.isPeakLengthProvided)  {
		if(!arguments.useExistingRes)
			cerr << "warning : missing peak search length."
			<< " Using default value " << arguments.spikeLength << endl;
		arguments.peakLength = arguments.spikeLength; // default
	}
	if(!arguments.isRefractoryPeriodProvided && !arguments.useExistingRes)  {
		cerr << "error: missing refractory period." << endl;
		exit(1);
	}
	if(!arguments.isTotalChannelNumberProvided) {
		cerr << "error: missing total number of channels." << endl;
		exit(1);
	}
	if(!arguments.isSpikeLengthProvided)  {
		cerr << "error: missing waveform length." << endl;
		exit(1);
	}
	if(!arguments.isTimeBeforeSpikeProvided)  {
		cerr << "error: missing position of the peak within the waveform."
		<< endl;
		exit(1);
	}
	if(!arguments.isChannelListProvided)  {
		cerr << "error: missing list of channels." << endl;
		exit(1);
	}
} // parseArgs


// checkInputs and checkChanAndThres: extractspikes_detect.h.


/**
 * Extract thresholds values from arguments
 * @param thresNb_group number of thresholds per group (to be completed)
 * @param thresList list of the groups thresholds value (to be completed)
 * @param arguments user arguments
 * @return The number of thresholds
 */
int getThresholdsFromArg(int *thresNb_group, double **thresList, 
						 const struct arguments arguments) {

	char zero = '0'; // Zero as a char (used to test arguments)
	int nbGroups = 0; // Number of thresholds groups
	char *groups, *currentGroup, *thresholds; // Groups of electrodes and thr

	// extract thresholds from arguments
	std::string thresListCopy(arguments.thresList); // was strdupa: lives until return
	groups = thresListCopy.data(); // Split groups
	currentGroup = neurosuite::core::strsep(&groups, GROUP_SEPARATOR); // focus on the 1st group

	while(currentGroup != NULL) {
		// initialization
		thresNb_group[nbGroups] = 0; // init the group thresholds number

		thresholds = neurosuite::core::strsep(&currentGroup, CHANNEL_SEPARATOR); // split thr
		while(thresholds != NULL) {
			if(atoi(thresholds)<1 && *thresholds != zero) {
				if(thresNb_group[nbGroups] == 0 && atoi(thresholds) == 0) {
					if(verbose)
						cerr << "Warning: threshold group " << nbGroups+1 << " is empty." << endl;
					break;
				}
			}
			
			// current group threshold
			thresList[nbGroups][thresNb_group[nbGroups]] = atof(thresholds);

			thresholds = neurosuite::core::strsep(&currentGroup, CHANNEL_SEPARATOR);
			if(thresholds == NULL) {
				thresNb_group[nbGroups]++;
				break;
			} // if
			thresNb_group[nbGroups]++;
		} // while (thresholds)

		currentGroup = neurosuite::core::strsep(&groups, GROUP_SEPARATOR);
		if(currentGroup == NULL) {
			nbGroups++;
			break;
		} // if
		nbGroups++;
	} // while (currentGroup)

	// No delete[]: groups/currentGroup/thresholds point into the thresListCopy
	// buffer.  They are NULL on the usual exit, but an empty group ("...:,4")
	// leaves `thresholds` pointing into it, and freeing that aborted the run.
	return nbGroups;

} // getThresholdsFromArg


// getChannelsFromArg: extractspikes_detect.h.
