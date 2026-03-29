/***************************************************************************
 * customtypes.h  (process_pca)
 *
 * Minimal type definitions and constants for process_pca.
 *
 * The original file was a copy of the Neuralynx (NLX) customtypes.h from
 * process_nlxconvert, which contained NCS_*, NVT_*, NEV_*, SMI_* constants
 * that have nothing to do with PCA.  Those definitions have been removed;
 * only the constants that process_pca and progressbar.h actually use remain.
 ***************************************************************************/
#ifndef CUSTOM_TYPES
#define CUSTOM_TYPES

// Maximum number of characters in the label field of a progress bar step.
// Used by progressbar.h/progressbar.cpp to right-pad the step name.
#define PROGRESS_MAX_N_CHARS 6

#endif // CUSTOM_TYPES
