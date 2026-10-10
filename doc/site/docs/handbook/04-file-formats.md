# File Formats {#data-files}

NeuroScope reads electrophysiological data from a set of files. Each of these files has a name of the form `base.ext` or `base.n.ext` (or `base.ext.n`), where `base` stands for the common base name of all files belonging to a single recording session, `ext` is an extension describing the type of data stored in the file, and `n` stands for the ID of the particular group of channels from which the data was recorded. Thus, using this convention the data for session CS150612 would be named `CS150612.dat`, the filter data for local filed potentials would be named `CS150612.eeg` and the cluster file for tetrode 2 would be named `CS150612.2.clu` or `CS150612.clu.2`.

More specifically, the data files are organized as described in the following sections.

## Data Files {#data_files}

### The data File {#data-file}

Generic file name: `base.dat` for the raw data, `base.eeg` for local field potentials, and more generally `base.ext` for any other type of filtered data (where `ext` is extension describing the type of data stored in the file).

Format: 12, 14, 16 or 32 bit binary.

The data file contains the data for each channel. Assuming `N1` channels (`channel1...channelN1`) and `N2` samples (`sample1...sampleN2`) the data are stored as follows:

    sample1_channel1
    sample1_channel2
    ...
    sample1_channelN1
    sample2_channel1
    sample2_channel2
    ...
    sample2_channelN1
    ...
    sampleN2_channelN1

### The Cluster File {#cluster-file}

Generic file name: `base.n.clu` (or `base.clu.n`)

Format: ASCII

The cluster file lists the cluster ID of each spike, one per line. The first line contains the total number of clusters. Assuming `N` spikes (`spike1...spikeN`), this file looks like:

    nClusters
    clusterID_spike1
    clusterID_spike2
    clusterID_spike3
    ...
    clusterID_spikeN

*Notice that the last line must end with a newline or carriage return.*

### The Spike Time File {#res-file}

Generic file name: `base.n.res` (or `base.res.n`)

Format: ASCII

The spike time file lists the timestamp for each spike. The timestamp is expressed in multiples of the sampling interval. For instance, for a 20kHz recording (50 microsecond sampling interval), a timestamp of 200 corresponds to 200x0.000050s=0.01s from the beginning of the recording session. Assuming `N` spikes (`spike1...spikeN`), this file looks like:

    timestamp_spike1
    timestamp_spike2
    timestamp_spike3
    ...
    timestamp_spikeN

*Notice that the last line must end with a newline or carriage return.*

### The Event File {#event-file}

Generic file name: `basename.ext.evt` or `basename.evt.ext` (where `ext` is a three-letter extension describing the type of data stored in the file)

Format: ASCII

The event file lists the events and their timestamps. The timestamps are given in miliseconds (floating point values) and the event types are arbitrary strings. Assuming `N` events (`event1...eventN`), this file looks like:

    timestamp_event1    description_event1
    timestamp_event2    description_event2
    timestamp_event3    description_event3
    ...
    timestamp_eventN    description_eventN

*Notice that the last line must end with a newline or carriage return.*

### The Positon File {#position-file}

Generic file name: `basename.ext` (where `ext` can be any three-letter extension)

Format: ASCII

The position file lists the `x` and `y` coordinates of the each spot. Assuming `N` position samples (`position1...positionN`) and `M` spots on the head of the animal, this file looks like:

    x_position1_spot1   y_position1_spot1   ...  x_position1_spotM  y_position1_spotM
    x_position2_spot1   y_position2_spot1   ...  x_position2_spotM  y_position2_spotM
    ...
    x_positionN_spot1   y_positionN_spot1   ...  x_positionN_spotM  y_positionN_spotM

By convention, spots which have not been detected by the video recording system should be given negatives coordinates. These spots will not be drawn in the position view.

*Notice that the last line must end with a newline or carriage return.*

## The Parameter File {#parameter_file}

Generic file name: `base.xml`

Format: XML

The parameter file is a generic file which contains information required by many related programs; NeuroScope only needs a subset of this information. The parameter file is optional: it is usually generated using [NDManager](http://ndmanager.sourceforge.net), a dedicated application, but if you do not provide it, NeuroScope will prompt you for the necessary information and automatically generate a parameter file for you (however, contrary to NDManager, NeuroScope will actually generate only the subset of fields that it requires, and not a full parameter file).

## The Session File {#neuroscope-session-file}

Generic file name: `base.nrs`

Format: XML

The session file is where NeuroScope stores information related to the working environment: the number of displays and their settings (start time, duration, visible channels, amplitudes, etc.), loaded cluster, event and position files, etc. This ensures that when you reopen a data file, your session is restored exactly as it was when you closed it.

