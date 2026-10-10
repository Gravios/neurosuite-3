# Using NeuroScope

## Starting a New Session {#starting}

To start a new session, select [File \> Open]{.menuchoice}. In the Open dialog, you can select any type of data file (`dat`, `eeg`, `nrs` or `xml`: see [File Formats](./04-file-formats.md#data-files)).

A session can also be started from Konqueror by clicking on one of the above files, or on the command line by typing:

    % neuroscope filename

First, NeuroScope needs some information about the acquisition system. You will be prompted to provide this information in the Properties dialog. More generally, this dialog is where you can set all the parameters for the various files you view and edit (this will be presented in later sections of this manual). For now, suffice it to introduce the Channels tab, which is the only one enabled at this point.

<a id="fig-properties"></a> File Properties.

All the fields are self-explanatory, but these fields may need clarification:

-   Initial offset: some acquisition systems record the data with a fixed offset; this can be specified here so that NeuroScope can recenter all the channels in the display.

-   Voltage range: the total voltage range, e.g. if the system is set to ±5V, the range is 10V.

-   Screen gain: millivolts per screen centimeter when the magnification level is set to 1x.

The information provided in the Properties dialog can be later displayed and modified at any time by selecting [File \> Properties]{.menuchoice}.

When starting NeuroScope from a terminal, this information can also be provided as command line options. To list available options, type

    % neuroscope --help
    Usage: neuroscope [Qt-options] [KDE-options] [options] file

    NeuroScope - Viewer for local field potentials, spikes and events

    Generic options:
      --help                    Show help about options
      --help-qt                 Show Qt specific options
      --help-kde                Show KDE specific options
      --help-all                Show all options
      --author                  Show author information
      -v, --version             Show version information
      --license                 Show license information
      --                        End of options

    Options:
      -r, --resolution          Resolution of the acquisition system.
      -c, --nbChannels          Number of channels.
      -o, --offset              Initial offset.
      -m, --voltageRange        Voltage range.
      -a, --amplification       Amplification.
      -g, --screenGain          Screen gain.
      -s, --samplingRate        Sampling rate.
      -t, --timeWindow          Initial time window (in miliseconds).

    Arguments:
      file                      Document to open.

The options provided on the command line will be displayed in the Properties dialog.

Once the information is properly set, the first 1000 ms of data are loaded and displayed in the main window, which should look like this:

<a id="fig-initialView"></a> initialView.

Note: if the channel amplitudes are too large, they can be easily adjusted (see [Traces](./04-file-formats.md#data-files)).

The window contains two functionally distinct areas: the Palettes on the left, and the Display containing a Trace View on the right. These will be discussed in the following sections.

## Palettes

NeuroScope provides several palettes to manipulate the data: the Anatomical Groups Palette, the Spike Groups Palette, as well as the Units Palette and the Events Palette (the latter two become available upon loading cluster and event files, respectively). To display a palette, click on the corresponding tab. Each tab has a distinctive icon next to the palette name to make it more easily identifiable. Optionally, palette names can be hidden, leaving only the icons, to save space in the main window (see [Settings](./03-settings.md#settings)).

<a id="fig-palettes"></a> Palettes.

### Anatomical Groups and Spike Groups

The Anatomical Groups Palette and the Spike Groups Palette are both used to manipulate the local field potential traces, and they share very similar behaviors. While the purpose of both palettes is to selectively display and group related channels, their main difference lies in how the groups are defined.

Suppose you record data from both the neocortex and the hippocampus: you may want to assign all channels recorded from the neocortex into one group, and all channels recorded from the hippocampus into another group, or maybe have two distinct groups for CA1 and CA3, respectively. This is what anatomical groups are for. More generally however, anatomical groups can be used to implement any classification you may wish, e.g. sites from the same shank in recordings using silicon probes. Anatomical groups are handled in the Anatomical Groups Palette.

On the other hand, spike groups are related to unit activity. In most experimental paradigms, you will want to study neuronal firing patterns, and thus you will need to extract unit activity from local field potentials. Usually, the spikes emitted by each single unit appear only on a limited subset of the channels. In NeuroScope, each such set of channels (e.g. a tetrode) is referred to as a spike group. Although NeuroScope will not perform spike extraction and classification (many other tools are available to perform these tasks), it will help you define the spike groups for later processing. Remember that spike groups are not necessarily equivalent to physical electrode groups: for instance, if one channel from a tetrode is floating or too noisy, discarding it altogether will yield better results during subsequent spike sorting. In general, these groups cannot be efficiently defined without first inspecting the data. This is why NeuroScope provides the ability to define spike groups. These are handled in the Spike Groups Palette

### Units

Unit activity can be extracted and sorted using a combination of automatic and manual cluster cutting. Spike sorting applications include Kenneth D. Harris' [KlustaKwik](http://klustakwik.sourceforge.net) (a powerful automatic cluster cutting command-line tool) and Lynn Hazan's [Klusters](http://klusters.sourceforge.net) (an advanced cluster viewer/editor application for KDE). Cluster files can be loaded into NeuroScope, which allows for browsing identified unit activity together with local field potentials. The Units Palette is where you perform all operations related to spike clusters.

### Events

Many experimental paradigms involve correlating brain activity with behavior. The latter is often described using timestamped markers listed in an event file. These event markers can be loaded and displayed in NeuroScope, which allows for visually assessing relations between behavioral events (water delivery, tone onset, etc.) and the occurrence of neurophysiological activity (increased neuronal firing, theta rhythm, etc.) The Events Palette is where you perform all operations related to behavioral events.

## Editing Channel Groups {#channel-groups}

The first task you will want to perform upon starting NeuroScope is to group related channels. Usually, this is first done in the Anatomical Groups Palette, because this determines how the channels are displayed. Indeed, in the display channels are layed out in the same order as they appear in the Anatomical Groups Palette: the first channel of the first group appears at the top, the second channel of the first group appears right below, etc. until the last channel of the last group which appears at the bottom of the display. Also, groups are separated by small gaps so they are more readily visible.

### Creating a New Group

Initially, all channels belong to the same group. In order to create a new group, you must first select the channels you want to group by clicking on the corresponding colored circles in the palette. Use Shift or Ctrl for multiple selections (see the Edit menu for more choices). Then, group these channels by selecting [Channels \> Move Channels to New Group]{.menuchoice} or by clicking on ![New Group](Images/new_group.png).

<a id="fig-newGroup"></a> initialView.

On the display, groups are spatially separated by small gaps to make them more readily visible.

### Discarding Bad Channels

You may want to discard floating or noisy channels altogether. This can be done by selecting the channels and selecting [Channels \> Discard Channels]{.menuchoice} or by clicking on ![Discard](Images/discard.png). The channels will be moved to the Discarded Group, as indicated by the small trash icon next to the group box in the palette.

!!! note

    Notice that discarding channels *does not modify your data* in any way: it does not actually remove the channels from the data files, but merely hides them from the display. Discarded channels can be brought back by moving them to an existing group (this is explained in [Moving Channels between Groups](./02-using-neuroscope.md#moving-groups)).


<a id="fig-discardingChannels"></a> Discard Channels.

You may have noticed in the above screenshot that the circle for the channel in the Discarded Group is now hollow. This is a visual hint to indicate that this channel is now hidden from the display (for more information on hidden channels, see [Showing and Hiding Channels](./02-using-neuroscope.md#show-hide)).

### Skipping Channels

If you are viewing data recorded from silicon probes and one or more channels are unusable (e.g. floating or extremely noisy), you may want to discard those channels using the method described in the previous section. However, this would not be appropriate here because valuable spatial information would be lost: due to dynamical rearrangement of the display, the layout of the traces would no longer reflect the spatial arrangement of the recording sites on the probes. The solution is instead to have NeuroScope 'skip' the unusable channels by selecting the channels and choosing [Channels \> Skip Channels]{.menuchoice} or by clicking on ![Skip channel](Images/skip.png). The traces will be hidden and replaced with a small gap, thus ensuring that the overall layout of the channels remains unchanged in the displays. The corresponding circles in the palettes will also be hidden (leaving only the channel numbers).

You can bring back skipped channels by selecting the channels and choosing [Channels \> Keep Channels]{.menuchoice} or by clicking on ![Keep channel](Images/keep.png). The channels will be reappear in the displays and the palettes.

<a id="fig-skippingChannels"></a> Keep Channels. Skip Channels.

### Moving Channels between Groups {#moving-groups}

Channels can be moved from one group to another group by simply dragging and dropping the corresponding circles in the palette. This includes moving channels to and from the Discarded Group.

<a id="fig-movingChannels"></a> Discard Channels.

!!! note

    Note that the channels cannot be dropped on top of another channel (if you attempt this, the pointer will turn into a "forbidden" cursor and the operation will be canceled). Instead, they must be dropped on an empty space.


### Reordering Channels within a Group {#reordering-channels}

Channels can be easily reordered within a group by dragging and dropping the corresponding circles in the group box, similar to moving channels between groups except that the displacement occurs within a single group.

### Reordering Groups

Groups can also be reordered using a drag and drop approach. To select a group, click on its ID (the number on the left side of the group box). You can then drag it and drop it just above or below the ID of another group to move it there.

### Synchronizing Anatomical and Spike Groups {#synchronizing}

As mentioned earlier, the typical way to organize channels is to first work with anatomical groups. In many cases, spike groups are mostly identical to anatomical groups: anatomical groups could thus be used as a basis to define spike groups. In such cases, NeuroScope can reuse the groups in the Anatomical Groups Palette to define spike groups: select [Channels \> Synchronize Groups]{.menuchoice}. Since this will override any existing spike groups, you will be prompted for confirmation if you have already defined spike groups.

### Removing Channels from a Spike Group {#removing-from-group}

While all the above mentioned operations apply to both anatomical and spike groups, this feature is only relevant for spike groups. It provides a way to deal with those channels that did not record any unit activity, although they did record valuable local field potential information. Obviously, these channels should not be discarded. However, they should not be used for spike extraction and sorting, either. In order to indicate that these channels do not belong to any spike group, select them and choose [Channels \> Remove Channels from Group]{.menuchoice} or click on ![New Group](Images/remove.png). The channels will be moved to the Undefined Spike Group, indentified by the question mark. Notice that this is the same group where all channels are initially listed before you start defining spike groups.

<a id="fig-removingChannels"></a> Discard Channels.

!!! note

    Notice that removing channels from a spike group *does not modify your data* in any way: it does not actually remove the channels from the data files, but removes their IDs from the list of spike groups used for subsequent spike extraction and sorting. Removed channels can be brought back by moving them to an existing spike group (this is explained in [Moving Channels between Groups](./02-using-neuroscope.md#moving-groups)).


## Managing the Display {#managing-display}

### Selecting Channels {#direct-selection}

As mentioned previously, channels can be selected by clicking on the corresponding circles in the palettes. Clicking on a group ID (the number on the left side of a group box) will select or deselect all the channels in the group at once. Another, more graphical way is to directly click on the traces in the display using the Select Channels tool. To activate this tool, select [Tools \> Select Channels]{.menuchoice} or click on ![Select Channels](Images/select_tool.png). Use Ctrl for multiple selections (see [Labels and Calibration Bars](./02-using-neuroscope.md#selection-labels) for yet another way to select channels).

### Channel Colors

In order to make individual channels as well as channel groups more readily visible, channels can be colored using three different schemes:

-   By Anatomical Group: A color can be assigned to all the channels in a given anatomical group by middle-clicking on the group ID in the Anatomical Groups Palette. This brings up the Color Chooser. As soon as you validate your new choice, the palettes and display will be updated accordingly. The new colors are referred to as the Anatomical Group colors for the given channels.

-   By Spike Group: Similarly, a color can be assigned to all the channels in a given spike group by middle-clicking on the group ID in the Spike Groups Palette. The new colors are referred to as the Spike Group colors for the given channels.

-   By Channel: Additionally, an individual channel can also be assigned a color by middle-clicking on the corresponding circle in either palette.

<a id="fig-colors"></a> Colors.

Although each channel can only have one single color at a time, Anatomical Group colors and Spike Group colors are always kept in memory. This means that although assigning an individual channel a new color will change its color in the display, this will not override its Anatomical Group color nor its Spike Group color (in memory): these can still be later reactivated. Similarly, changing Anatomical Group colors does not affect Spike Group colors and vice versa.

To recolor all channels using Anatomical Group colors, select [Channels \> Color by Anatomical Groups]{.menuchoice}. Similarly, to recolor all channels using Spike Group colors, select [Channels \> Color by Spike Groups]{.menuchoice}.

!!! note

    Contrary to Anatomical Group colors and Spike Group colors, individual channel colors are not kept in memory. You can think of them as temporary colors: they are typically used when you need to temporarily identify one particular channel from the other channels in the same group.


### Showing and Hiding Channels {#show-hide}

When you first open a data file with NeuroScope, all the channels are shown in the display. But you can decide to hide some channels, for instance to temporarily focus on a subset of the data. To hide channels, select them (in either palette or [directly in the display](./02-using-neuroscope.md#direct-selection)), and choose [Channels \> Hide Channels]{.menuchoice} or click on ![Hide Channels](Images/eye_close.png). The remaining channels will be reorganized in the display to optimally utilize the available space. The circles in the palette will become hollow to indicate that the channels are now hidden (see the above [figure](./02-using-neuroscope.md#fig-colors)).

To make hidden channels visible again, select the corresponding circles and choose [Channels \> Show Channels]{.menuchoice} or click on ![Show Channels](Images/eye.png).

### Adjusting Amplitudes and Offsets

Channel amplitudes can be adjusted by selecting channels and choosing [Traces \> Increase Selected Channel Amplitudes]{.menuchoice} or [Traces \> Decrease Selected Channel Amplitudes]{.menuchoice}. To adjust all channels at once, select [Traces \> Increase All Channel Amplitudes]{.menuchoice} or [Traces \> Decrease All Channel Amplitudes]{.menuchoice}. To reset the amplitudes to their default (initial) settings, select channels and choose [Traces \> Reset Selected Channel Amplitudes]{.menuchoice}.

Channels can be offset by clicking on the traces and dragging them by the appropriate amount using the Select Channels tool. To activate this tool, select [Tools \> Select Channels]{.menuchoice} or click on ![Select Channels](Images/select_tool.png). Use Ctrl for multiple selections (see [Labels and Calibration Bars](./02-using-neuroscope.md#selection-labels) for yet another way to select channels). Notice that in order to provide visual feedback the traces become slightly thicker when the channels are selected using the Select Channels tool. As you drag them around, their new positions are indicated by white outlines. Upon releasing the mouse button, the traces will move to their new positions.

<a id="fig-offsets"></a> Offsets.

When the physical organisation of your electrodes is more complex than vertical arrays (e.g. epidural electrodes covering the surface of the brain), NeuroScope provides a way to store your electrode layout. This can be done by redefining the default offsets which are stored in the parameter file (see [File Formats](./04-file-formats.md#parameter_file)). To set the current offsets as the default offsets, select [Traces \> Set Current Offsets as Default]{.menuchoice}. To set the default offsets back to zero, select [Traces \> Set Default Offsets to Zero]{.menuchoice}.

<a id="fig-epidural"></a> Epidural electrode.

!!! note

    Defaut offsets can be reused for subsequent experiments. This can be done using [NDManager](http://ndmanager.sourceforge.net), which is part of the larger data analysis framework including NeuroScope and [Klusters](http://klusters.sourceforge.net). Alternatively, this can also be done manually by duplicating and modifing the parameter file (see [File Formats](./04-file-formats.md#parameter_file)).


## Advanced Display Management

### Multiple Column Mode

A very useful feature when recording from multisite silicon probes is the ability to display each shank as a separate 'column', so that the traces are laid out on the screen in a similar way as the physical recording sites in the brain. Select [Traces \> Multiple Columns]{.menuchoice}. Channels from each Anatomical Group now appear in separate columns.

<a id="fig-multiple-columns"></a> Multiple Column Mode.

!!! note

    Keep in mind that although Anatomical Groups were primarily designed to help you organize the channels to reflect the positions of the electrodes in the brain, they can be more generally used to implement any kind of classification that may suit your needs.


When channels are hidden in multiple column mode, dynamical reorganization of the display may not always be the appropriate behavior. Indeed, if you are viewing data recorded from silicon probes, you will most likely prefer keeping the channels always laid out in the same way no matter how many channels are displayed, rather than having them moved around to optimize the available space. In order to hide channels while keeping their spatial arrangement, use this trick: change the color of the channels you wish to hide to the background color (black, unless you modified it in the <a id="static-hiding"></a>[Settings](./03-settings.md#settings-background)).

### Labels and Calibration Bars

Labels provide information about the traces: the ID of the corresponding channel, and the current amplitude. Labels can be showed by selecting [Traces \> Show Labels]{.menuchoice} (selecting this menu item a second time will toggle its status and hide the labels).

<a id="fig-labels"></a> Labels.

Amplitudes are described as follows. By convention, 'x1' corresponds to the <a id="screen-gain"></a>[current screen gain](./02-using-neuroscope.md#properties). Thus, assuming for instance a screen gain of 1 mV/cm, an amplitude of 'x0.38' corresponds to 0.38 mV/cm. Notice that different traces can have different amplitudes, as is the case in the above screenshot where local field potentials have a gain of x0.38 and the synchronization pulse has a gain of x0.12.

Calibration bars also provide scaling information. The horizontal bar indicates the time scale, and the vertical bar indicates the voltage scale *for channel 0* (notice that there is only one vertical bar, but each channel can have a different amplitude, hence the need for a convention). Showing calibration bars is most useful when printing traces. To display or hide the calibration bars, toggle [Settings \> Show Calibration]{.menuchoice}.

!!! note

    Labels can be shown or hidden on a display-by-display basis, but showing or hiding calibration bars applies to all displays at once (which is why these functions appear in different menus).


Besides clicking on the corresponding circles in the palette or <a id="selection-labels"></a>[on the traces](./02-using-neuroscope.md#direct-selection) in the display, channels can also be selected by clicking on the corresponding labels using the Select Channel tool. For multiple selections, this offers the possibility to use the Shift key (for continuous selections) in addition to the Ctrl key (for non-continuous selections).

### View Mode and Edit Mode

All the features described up to this point are most useful in the initial stages of data exploration. But once all groups are defined, the interface can be simplified by enabling only those features related to data browsing. This is know as the View Mode, as opposed to the elaborate (default) Edit Mode. You can toggle between the two modes by selecting [Edit \> Edit Mode]{.menuchoice} or clicking on ![View Mode and Edit Mode](Images/edit.png). In View Mode, the channels are represented by colored squares in the palettes: this visual feedback allows you to instantly differentiate between modes. Channels can now be shown or hidden by simply selecting or deselecting the corresponding squares - you no longer need to explicitly show or hide them.

<a id="fig-modes"></a> View Mode and Edit Mode.

Notice that the editing tools are now disabled: you can no longer create, modify or delete groups, nor can you change channel offsets.

In View Mode, the palettes behave in the same way as in [Klusters](http://klusters.sourceforge.net). This is also how the Units Palette and the Events Palette behave.

### Using Multiple Displays

NeuroScope provides the possibility to use several displays at once, which can be useful to examine different subsets of the data in parallel. To open a new display, select [Displays \> New Display]{.menuchoice}. The new display is copied from the currently active display as a starting point, and can then be independently modified. After you have modified the contents of a display to fit your needs, you may wish to change its title (the label of the tab) to reflect your changes. Select [Displays \> Rename Active Display]{.menuchoice}. This will bring up a dialog where you can type the new title for the currently active display.

## Browsing Local Field Potentials {#browsing-lfp}

There are several ways to navigate through the data:

-   To move to a particular point in the data, enter the corresponding time in the Start time text boxes (below the traces). Time is typically entered in minutes, seconds and milliseconds. Alternatively, you can use cumulative seconds or milliseconds: for instance, entering `0 min 180 s 0 ms`, or `0 min 0 s 180000 ms` is equivalent to entering `3 min 0 s 0 ms`.

-   To modify the duration of the time window shown in the display (1 s by default), change the value in the Duration text box (below the traces). This value can be doubled by hitting the + key, or divided by two by hitting the - key.

-   To display the next time window, click on the scroll bar below the traces, in the empty space on the right side of the slider. You can also hit the Page Down key. Similarly, clicking on the left side of the slider, or hitting the Page Up key, displays the previous time window.

-   To move by smaller amounts, click on the arrows of the scroll bar, or hit Left Arrow or Right Arrow.

-   To move to the beginning or the end of the data, hit the Home or End keys, respectively.

-   Dragging the slider in the scroll bar is another way to move within the data.

!!! note

    The shortcut keys described above will not work if the focus is in one of the text boxes below the traces. This is because in this case the shortcuts are applied to the text boxes rather than to the display (e.g. hitting the End key will move the insertion point to the end of the text). To activate the navigation shortcut keys, move the focus elsewhere, for instance by clicking on the scroll bar.


## Tools

NeuroScope provides a set of tools to help you efficiently browse and edit your data. Whichever tool you select, when the cursor is located within the display, the time below the pointer (counted from the beginning of the data) is indicated in the Status Bar.

Only one tool is active at a time: choosing a tool replaces the previous one, and the active tool stays highlighted on the toolbar. Each tool can be selected from the Tools menu, from the toolbar, or with its keyboard shortcut: Z (Zoom), C (Select Channels), V (Measure), T (Select Time), E (Select Event) and L (Draw Time Line); Add Event is chosen from its toolbar drop-down. When several displays are open, the active tool is remembered for each display, so the toolbar highlight follows the display you are working in. All of these shortcuts can be listed or changed as described in [Keyboard and Mouse Shortcuts](./02-using-neuroscope.md#shortcuts).

### Zoom

To zoom on a particular area in the display, select [Tools \> Zoom]{.menuchoice} or click on ![Zoom tool.](Images/zoom_tool.png) . Now click on the center of the area you wish to enlarge. Alternatively, you can select the area to enlarge (click and hold the left button, then drag the mouse to select the area). Double-clicking will bring the zoom level back to the initial state.

### Draw Time Line

In order to quickly assess the temporal relationships between physiological events occurring on different channels, a vertical time line can be temporarily displayed on the traces. To display the time line, select [Tools \> Draw Time Line]{.menuchoice} or click on ![Time Line tool.](Images/time_line_tool.png) . Now click and drag the mouse around in the trace view: the time line follows the cursor until you release the mouse button. In multiple column mode, the time line is drawn in each of the columns at the same position in time.

As usual, while you move the pointer within the display, the time at the cursor position is displayed in the Status Bar.

### Select Channels

To select one or more channels directly from the display, choose [Tools \> Select Channels]{.menuchoice} or click on ![Select tool.](Images/select_tool.png) . Now hold the Ctrl key and click on the channels you wish to select. As a visual feedback, the traces become slightly thicker when the channels are selected. Notice that this does not allow for continuous selection. Use [this method](./02-using-neuroscope.md#selection-labels) instead.

### Measure

To measure on the traces the amplitude and duration of a neurophysiological event (an action potential, a theta cylce, etc.), select [Tools \> Measure]{.menuchoice} or click on ![Measure tool.](Images/measure_tool.png) . Click on the upper left corner of the rectangular region you wish to measure, and drag the pointer to the lower right corner: the corresponding voltage and duration are indicated in the Status Bar.

<a id="fig-measure"></a> Measure.

### Select Time

To graphically refine the displayed time window, select [Tools \> Select Time]{.menuchoice} or click on ![Time Select tool.](Images/time_tool.png) . Click at the start time of the window you wish to define, then drag the pointer to the end time: the displayed time window will be updated accordingly. Notice that this is different from zooming in two respects: first, only the horizontal component of the selection (time) is taken into account, and second this actually redefines both the Start time and the Duration, rather than temporarily focusing on a particular portion of the display.

<a id="fig-select-time"></a> Select time.

As usual, while you move the pointer within the display, the time at the cursor position is displayed in the Status Bar.

### Select Event

To select a single event, choose [Tools \> Select Event]{.menuchoice} or click on ![Event Select tool.](Images/event_tool.png) , and click on the event line in the display. The event can now be moved or deleted. For more details, see [Events](./02-using-neuroscope.md#events).

### Add Event

To add a single event to an existing event file, select an event type under [Tools \> Add Event]{.menuchoice} or click on ![Add Event tool.](Images/add_event_tool.png) , optionally selecting an event type in the drop down menu, and click in the display where the new event should be added. For more details, see [Events](./02-using-neuroscope.md#events).

## Unit Activity {#units}

### Displaying Unit Activity

Unit activity is described in two files: a spike time file (listing the timestamps of the spikes) and a cluster file (assigning each spike to a cluster which corresponds to a putative unit). In addition, if you are working with a `.dat` file, spike waveforms can be extracted from that file. This is explained in more detail in [File Formats](./04-file-formats.md#data-files). The spike time file and the cluster file can be loaded by selecting [File \> Load Cluster File(s)...]{.menuchoice} (multiple files can be opened in parallel). This creates a new cluster box in the Units Palette, where all the clusters are listed (the Units Palette is created upon loading the first cluster file).

To display the spikes fired by certain clusters, select the corresponding clusters by clicking on the colored squares in the palette. Use Shift or Ctrl for multiple selections. Clicking on a cluster file ID (the number on the left side of a unit box) will select or deselect all the clusters in the file at once. Hold the Shift key while clicking on a cluster file ID to select all clusters except 0 and 1 (artefacts and noise, respectively).

<a id="fig-clusters"></a> Units.

NeuroScope provides three different graphical representations for spikes. You can activate or deactivate one or more representations by selecting or deselecting the corresponding items in the Units menu.

-   Vertical Lines: each spike is represented as a vertical line spanning the entire display.

-   Rasters: spikes are represented as small vertical segments below the local field potential traces, grouped by unit. In the display, the total height devoted to the rasters (vs the traces) can be adjusted by selecting [Units \> Increase Height]{.menuchoice} or [Units \> Decrease Height]{.menuchoice}.

-   Waveforms: spike waveforms are highlighted on their respective traces (for instance, since the `.clu.5` file describes the clusters of spike group 5, selecting a cluster from this group will cause NeuroScope to draw the corresponding waveforms on the traces listed in the spike group 5). Highlighting is rendered even more visible if the traces are drawn using shades of gray instead of colors. This can be obtained by selecting [Traces \> Grey-Scale]{.menuchoice}.

!!! note

    The waveforms can be drawn only when working with raw data (`.dat` files), as the waveform information is not available in low-pass filtered data.


!!! note

    In multiple column mode, spikes recorded from a given set of electrodes are shown only in the column containing the corresponding traces. If however these traces are split across two or more columns, the spikes will be shown in each of these columns.


### Browsing Unit Activity

Pyramidal or granule cells typically fire at low rates. Thus, when examining their firing patterns, you may sometimes find it difficult to locate those epochs when the neurons discharge. NeuroScope provides a mechanism to easily 'browse' spikes, i.e. to automatically move the time window to the previous or next spike fired by one or more neurons. First, indicate which clusters you wish to browse by holding both Ctrl and Alt and clicking on the corresponding colored squares in the palette (only already selected clusters can be used). The squares are now replaced by triangles to reflect their activated state. Now, choose [Units \> Previous Spike]{.menuchoice} or click on ![Previous spike.](Images/backCluster.png) to move the time window to the previous spike fired by one of the activated clusters, or choose [Units \> Next Spike]{.menuchoice} or click on ![Next spike.](Images/forwardCluster.png) to move the time window to the next spike.

!!! note

    Holding Ctrl and Alt while clicking on a cluster file ID will activate or deactivate all selected clusters in the corresponding file.


!!! note

    Since only selected clusters can be browsed, deselecting a unit will automatically reset its activation state.


## Events {#events}

### Displaying Events

Events are described in one or more event files (listing the timestamps and descriptions of the events, see [File Formats](./04-file-formats.md#data-files)). Event files can be loaded by selecting [File \> Load Event File(s)...]{.menuchoice} (multiple files can be opened in parallel). This creates a new box in the Events Palette, where all the events are listed (the Events Palette is created upon loading the first event file).

To display certain events, click on the colored squares in the palette. Use Shift or Ctrl for multiple selections. Clicking on an event file ID (the three letters on the left side of an event box) will select or deselect all the events in the file at once. Events are represented as colored dashed lines spanning the whole display.

<a id="fig-events"></a> Events.

### Browsing Events

NeuroScope provides a mechanism to easily 'browse' events, i.e. to automatically move the time window to the previous or next event among a list of activated events. First, indicate which events you wish to browse by holding both Ctrl and Alt and clicking on the corresponding colored squares in the palette (only already selected events can be used). The squares are now replaced by triangles to reflect their activated state. Now, choose [Events \> Previous Event]{.menuchoice} or click on ![Previous event.](Images/backEvent.png) to move the time window to the previous activated event, or choose [Events \> Next Event]{.menuchoice} or click on ![Next event.](Images/forwardEvent.png) to move the time window to the next activated event.

!!! note

    Holding Ctrl and Alt while clicking on an event file ID will activate or deactivate all selected events in the corresponding file.


!!! note

    Since only selected events can be browsed, deselecting an event will automatically reset its activation state.


### Editing Events

#### Selecting Events

To select a single event, choose [Tools \> Select Event]{.menuchoice} or click on ![Event Select tool.](Images/event_tool.png) , and click on the event line in the display. The event line becomes slightly thicker. The event can now be moved or deleted.

#### Moving an Event

To move an event, i.e to change its timestamp, select it in the display, and move the pointer while holding the mouse button. As you drag the event around, its new position is indicated by a white outline. Upon releasing the mouse button, the event will move to its new position.

#### Deleting an Event

To delete an event, select it in the display, and choose [Events \> Delete Event]{.menuchoice}.

#### Adding an Event

To add a single event to an existing event file, first choose in the palette the event file where the event should be added by clicking on the three-letter ID on the left side of the corresponding event box. Then, select an event type under [Tools \> Add Event]{.menuchoice} or click on ![Add Event tool.](Images/add_event_tool.png) (optionally selecting an event type in the drop down menu). Now click in the display where the new event should be added.

New event types can also be created, by selecting [New Event...]{.menuchoice} under either of the two menus mentioned above. A dialog box will pop up to allow you to provide a new event description.

#### Undo and Redo

To undo the last operation (addition, deletion, or timestamp change), select [Edit \> Undo]{.menuchoice}. Conversely, after canceling an operation, selecting [Edit \> Redo]{.menuchoice} will apply the changes again.

#### Creating a New Event File

A new event file can be created by selecting [File \> Create Event File...]{.menuchoice} and choosing a new name in the file creation dialog. A new box appears in the Event Palette, where new events can now be added.

## Positions

In certain behavioral experiments, e.g. when studying place cells or head direction cells, it is necessary to record brain signals as well as the ongoing position and orientation of the animal. The latter is usually stored in a file listing the positions across time of one or more small lights (spots) attached to the head of the animal (see [File Formats](./04-file-formats.md#position-file)). NeuroScope has the ability to simultaneously display brain signals and the successive positions of the animal during the same episode.

To open a position file, select [File \> Load Position File...]{.menuchoice}. A new view is added in the display, showing the successive positions recorded during the current time window. The spots are represented as colored circles linked by gray lines (e.g., a segment for two spots, a triangle for three spots, etc.) The front spot is drawn in red, and all the other spots in green. Also, the last position in the current time window is highlighted: the circles are larger, and the lines are white. This helps determine the direction of movement.

<a id="fig-positions"></a> Positions.

Events can be superimposed on the position display as colored crosses. Events are drawn at the position where the animal was at the time of their occurent. Notice that if no the position of the animal could be detected at the time of the event, this will not be drawn. To display or hide the events, toggle [Positions \> Show Events]{.menuchoice}.

By default, the positions are drawn against a black background, but NeuroScope can instead use an overview of the behavioral maze as a background image. Select [File \> Properties]{.menuchoice}, and choose the Positions tab, where you can specify the image location on your disk. You can also flip and rotate the image (the positions will be flipped and rotated accordingly). In addition, NeuroScope can draw over the background (be it a black background or an image) an outline of the trajectory of the animal throughout the experiment. This can be activated using the Draw trajectory checkbox.

## File Properties {#properties}

Selecting [File \> Properties]{.menuchoice} brings up the File Properties dialog. In this dialog, you can check and edit a number of parameters related to the current session. These parameters are described in [Settings](./03-settings.md#defaults) (notice that although the File Properties dialog is very similar to the Defaults tab of the Settings dialog, the former is related to the current session only, whereas the latter defines the default values for new sessions). Changes in the File Properties dialog will be applied immediately upon validation.

One item in the File Properties dialog needs special mention: the Sampling Rate for this File entry. This is useful when you work with data files other than the `.dat` file (which contains raw data, and thus has the same sampling rate as the acquisition system).

## Printing and Exporting {#print}

### Printing

To print the current display, select [File \> Print]{.menuchoice}. This brings up the standard KDE print dialog. Notice the additional Background Color tab: it allows you to set the background color to white for printing (this saves printer ink). If the tabs are hidden, just click on the Expand button to display them.

![Printer.](Images/printer.png)

If a position view is opened, each view is printed on a separate page. The file name, the starting time and time frame of the trace view are printed below the figure. The filie name of the position view is printed below the position figure.

### Exporting

Exporting is achieved in a similar way as printing. Select [File \> Print]{.menuchoice}. In the print dialog, click on the printer drop down list, and choose Print to File (PostScript) to export the display as a PostScript figure, or Print to File (PDF) to export it as a Portable Document File.

The Postscript format is ideal to import a figure into a drawing program for additional editing: it is a widely used standard, and all the information is stored in vectorial form. The PDF format is ideal for sharing a final figure with other investigators: it is platform independent (it will look identical whether in Linux, MacOS or Windows) and much more compact than PostScript (thus the output file is smaller and can easily be sent as an attachment via e-mail).

## Session Management {#saving}

In a typical session, you open a data file, browse the local field potentials, maybe create several different displays, load and display spikes, events and positions, etc. You then end your session. The next time you reopen the same data file, NeuroScope will make sure that your session is restored exactly as it was when you closed it. Even better, this will happen whatever file you open from the same recording, be it the `.dat` file, the `.eeg` file, or any other recording file. This is made possible because all the relevant information is automatically saved to disk when you end a session (you do not need to explicitly save it yourself). Notice that this is different from saving your work via the Save menu entry or toolbar button, which applies to changes in the spike and anatomical groups, as well as to changes in the events.

## Keyboard and Mouse Shortcuts {#shortcuts}

Every menu command, tool and mouse gesture in NeuroScope is driven by one central list of bindings. You can see the whole list at any time, and you can change almost any of them to suit your habits.

### Viewing the Shortcuts {#shortcuts-viewing}

Select [Help \> Keyboard Shortcuts...]{.menuchoice} to open a read-only list of every command together with the key, mouse button or wheel action currently assigned to it. The list is split into a Keyboard section and a Mouse section, and within each the commands are grouped by category: the menu or tool they belong to.

### Customizing the Shortcuts {#shortcuts-customizing}

To change a binding, open [Settings \> Configure neuroscope]{.menuchoice} and select the Input page. Each command is shown on its own row with its current binding:

-   For a *keyboard* command, click the field and press the new key combination.

-   For a *mouse* command, choose the button (or wheel direction) and any modifier keys from the controls on the row.

If two commands that apply in the same context are given the same binding, both fields are highlighted so you can resolve the clash. A few bindings belong to a temporary mode and are shown read-only.

Each row has a Reset that restores that command's shipped default. To undo all of your changes at once, use Reset all to defaults at the top of the page. Changes take effect when you click Apply or OK, and are remembered across sessions.

### Keymap Layouts {#shortcuts-layouts}

The top of the Input page carries a Keymap layout bar that lets you keep several named sets of bindings and switch between them:

-   Pick a layout from the list and click Apply to load it into the rows below.

-   Save As... captures your current bindings as a new, named layout.

-   Rename and Delete act on your own saved layouts. Built-in layouts are read-only and cannot be renamed or deleted.

NeuroScope ships with one built-in layout, Default, which holds the program's original keyboard and mouse bindings. Applying it is a convenient way to return to the shipped configuration (the same effect as Reset all to defaults). As with individual edits, a change of layout is only stored when you click Apply or OK.

