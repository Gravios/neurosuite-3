/***************************************************************************
                          process_extractspikes.h  -  description
                             -------------------
    copyright            : (C) 2007 by Nicolas Lebas
    email                : nicolas.lebas@college-de-france.fr


 * Description : process_extractspikes is a program which detect and extract spikes from a
 * filtered electrophysiologic record (following a given threshold).
 *
 * Usage : process_extractspikes [options] [input]
 *
 ***************************************************************************/

/***************************************************************************
 *
 *   This file is a part of the free software process_extractspikes ; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as published by the Free
 *   Software Foundation; either version 3 of the License, or (at your option) any later version.
 *
 ***************************************************************************/
#ifndef __PROCESS_DETECT_SPK_H
#define __PROCESS_DETECT_SPK_H

#define BUFFER_CHANNEL_SIZE 50000
#define MAX_CHANNO 512  // was 128; raised for high-density probes
#define RECORD_BYTE_SIZE 2
#define GROUP_SEPARATOR ":"
#define CHANNEL_SEPARATOR ","
#define SPIKE_TIME_OUT_EXT "res"
#define SPIKE_REC_OUT_EXT "spk"

#include <iostream>
#include <string>
#include <sys/types.h>


// Structure for all arguments
struct arguments {
	char *inputFileName; // Input files name
	char *outputBaseFileName; // Ouput base name (i.e without extension)
	int peakLength; // Timestep for a spike duration
	int refractoryPeriod; // Timestep for the refractory period
	int totalChannelNumber; // Total number of channels
	char* channelList;	// List of channels for each group (comma separated integers between
										// channel numbers in a group and semicolon separated between two groups)
	char* thresList;	// List of thresholds for each group (comma separated integers between
									// thresholds in a group and semicolon separated between two groups)
	int spikeLength; // Extract window length
	int timeBeforeSpike; // Time before spike to extract

	bool isInputFileProvided, isOutputBaseFileProvided;
	bool isPeakLengthProvided, isRefractoryPeriodProvided;
	bool isTotalChannelNumberProvided;
	bool isChannelListProvided;
	bool isThresListProvided;
	bool isSpikeLengthProvided;
	bool isTimeBeforeSpikeProvided;
	bool isDisableAbs;

	// patch86: when true, read per-group .res files instead of detecting,
	// then re-extract waveforms at those exact timestamps.  Used by KKExp
	// post-TimeShiftFinalize to refresh .spk content after alignment.
	// Threshold / refractory / peak-search args are ignored in this mode.
	bool useExistingRes;
};

// isRefractoryPeriod, isRealPeak, getNegativePeakFullId, getPeakFullId,
// lookForMaxNegative, lookForMax, searchSpikeInChannels, getThresholdFromChan,
// checkInputs, checkChanAndThres and getChannelsFromArg:
// extractspikes_common/extractspikes_detect.h.
// Parse arguments and store them in arguments
void parseArgs(const int, char**, arguments &);
// Extract thresholds from arguments
int getThresholdsFromArg(int *, double **, const arguments);

#endif
