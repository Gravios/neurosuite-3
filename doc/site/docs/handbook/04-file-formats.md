# File Formats {#data-files}

NeuroScope reads electrophysiological data from a set of files. Each of these files has a name of the form `base.ext` or `base.n.ext` (or `base.ext.n`), where `base` stands for the common base name of all files belonging to a single recording session, `ext` is an extension describing the type of data stored in the file, and `n` stands for the ID of the particular group of channels from which the data was recorded. Thus, using this convention the data for session CS150612 would be named `CS150612.dat`, the low-pass filtered data for local field potentials would be named `CS150612.lfp` and the cluster file for tetrode 2 would be named `CS150612.2.clu` or `CS150612.clu.2`.

More specifically, the data files are organized as described in the following sections. The complete format reference for all neurosuite-3 files is in `doc/ndmanager-plugins/formats/` in the source tree.

## Data Files {#data_files}

### The data File {#data-file}

Generic file name: `base.dat` for the raw data, `base.lfp` (formerly `base.eeg`) for local field potentials, `base.fil` for high-pass filtered data, and more generally `base.ext` for any other type of filtered data (where `ext` is extension describing the type of data stored in the file).

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

Format: binary or ASCII (detected automatically; the `.clu` and `.res` files of a group must use the same form). The pipeline writes binary files: a 32-bit header followed by one 32-bit cluster ID per spike (see `doc/ndmanager-plugins/formats/clu.md`).

In the ASCII form, the cluster file lists the cluster ID of each spike, one per line. The first line contains the total number of clusters. Assuming `N` spikes (`spike1...spikeN`), this file looks like:

    nClusters
    clusterID_spike1
    clusterID_spike2
    clusterID_spike3
    ...
    clusterID_spikeN

*Notice that the last line must end with a newline or carriage return.*

### The Spike Time File {#res-file}

Generic file name: `base.n.res` (or `base.res.n`)

Format: binary or ASCII (detected automatically). The pipeline writes binary files: one 64-bit sample index per spike, with no header (see `doc/ndmanager-plugins/formats/res.md`).

The spike time file lists the timestamp for each spike. The timestamp is expressed in multiples of the sampling interval. For instance, for a 20kHz recording (50 microsecond sampling interval), a timestamp of 200 corresponds to 200x0.000050s=0.01s from the beginning of the recording session. In the ASCII form, assuming `N` spikes (`spike1...spikeN`), this file looks like:

    timestamp_spike1
    timestamp_spike2
    timestamp_spike3
    ...
    timestamp_spikeN

*Notice that the last line must end with a newline or carriage return.*

### The Event File {#event-file}

Generic file name: `basename.id.evt` or `basename.evt.id`, where `id` is a three-character identifier with at least one non-digit character (e.g. `CS150612.rip.evt`, `CS150612.evt.s01`). The identifier is shown in the Events Palette.

Format: ASCII

The event file lists the events and their timestamps. The timestamps are given in miliseconds (floating point values) and the event types are arbitrary strings. Assuming `N` events (`event1...eventN`), this file looks like:

    timestamp_event1    description_event1
    timestamp_event2    description_event2
    timestamp_event3    description_event3
    ...
    timestamp_eventN    description_eventN

*Notice that the last line must end with a newline or carriage return.*

### The Positon File {#position-file}

Generic file name: `basename.pos` by convention (any extension is accepted)

Format: ASCII

The position file lists the `x` and `y` coordinates of the each spot. Assuming `N` position samples (`position1...positionN`) and `M` spots on the head of the animal, this file looks like:

    x_position1_spot1   y_position1_spot1   ...  x_position1_spotM  y_position1_spotM
    x_position2_spot1   y_position2_spot1   ...  x_position2_spotM  y_position2_spotM
    ...
    x_positionN_spot1   y_positionN_spot1   ...  x_positionN_spotM  y_positionN_spotM

By convention, spots which have not been detected by the video recording system should be given negatives coordinates. These spots will not be drawn in the position view.

*Notice that the last line must end with a newline or carriage return.*

## The Parameter File {#parameter_file}

Generic file name: `base.yaml` (or `base.yml`)

Format: YAML

The parameter file is a generic file which contains information required by many related programs; NeuroScope only needs a subset of this information. The parameter file is optional: it is usually written by NDManager (part of [neurosuite-3](https://github.com/Gravios/neurosuite-3)), but if you do not provide it, NeuroScope will prompt you for the necessary information and write a parameter file when you save (however, contrary to NDManager, NeuroScope writes only the subset of fields that it requires, and not a full parameter file). When it saves, NeuroScope updates its own fields and keeps the rest of an existing file.

Older parameter files in XML (`base.xml`) are not read; convert them with `ndm_xml2yaml` from the NDManager plugins.

## The Session File {#neuroscope-session-file}

Generic file name: `base.nrs`

Format: YAML. An older XML session file cannot be read: NeuroScope ignores it, opens the data with default display settings and replaces it with a YAML file when the session is saved.

The session file is where NeuroScope stores information related to the working environment: the number of displays and their settings (start time, duration, visible channels, amplitudes, etc.), loaded cluster, event and position files, etc. This ensures that when you reopen a data file, your session is restored exactly as it was when you closed it.

