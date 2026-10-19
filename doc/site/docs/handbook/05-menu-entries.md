# Menu Entries

The shortcuts listed here are the defaults. **Help \> Keyboard Shortcuts...** lists the bindings currently in effect, and they can be changed on the Input page of **Settings \> Preferences**. On macOS, Ctrl is the Command key.

## The File Menu {#menu-file}

**File \> Open** (Ctrl+O)

:   Opens a data file, or a session file (`.nrs`).

**File \> Open Recent**

:   Opens a file selected from a list of recently opened files.

**File \> Load Cluster File(s)...**

:   Loads one or more cluster files (each with its spike time file).

**File \> Load Event File(s)...**

:   Loads one or more event files.

**File \> Create Event File...**

:   Creates an event file.

**File \> Load Position File...**

:   Loads a single position file.

**File \> Load Overlay Trace File...**

:   Loads a second trace file with the same channel layout and draws it on top of each channel (see [Overlay Trace Files](./02-using-neuroscope.md#overlay-traces)).

**File \> Save** (Ctrl+S)

:   Saves the parameter file, the session file and any modified event files.

**File \> Save As** (Ctrl+Shift+S)

:   Saves the session file in a new location.

**File \> Print** (Ctrl+P)

:   Prints (or exports as PDF) each view of the current display in a separate page.

**File \> Properties**

:   Opens a dialog displaying the properties of the currently opened file. These properties can be edited.

**File \> Close**

:   Closes the session (will prompt for saving if necessary).

**File \> Close Cluster File**

:   Closes the cluster file currently selected in the Units Palette.

**File \> Close Event File**

:   Closes the event file currently selected in the Events Palette.

**File \> Close Position File**

:   Closes the position file.

**File \> Quit** (Ctrl+Q)

:   Quits NeuroScope (will prompt for saving if necessary).

## The Edit Menu {#menu-edit}

**Edit \> Undo** (Ctrl+Z)

:   Undoes the last change (for event edition only).

**Edit \> Redo** (Ctrl+Shift+Z)

:   Redoes the last undone change (for event edition only). The shortcut is the platform's Redo key: Ctrl+Shift+Z on Linux, Ctrl+Y on Windows.

**Edit \> Select All** (Ctrl+A)

:   Selects all the items in the current palette (channels, clusters or events).

**Edit \> Select All except 0 and 1** (Ctrl+Shift+A)

:   Selects all clusters except artefacts (cluster 0) and noise (cluster 1).

**Edit \> Deselect All** (Ctrl+U)

:   Deselects all the items in the current palette (channels, clusters or events).

**Edit \> Edit Mode** (Ctrl+E)

:   Switches between edit and view mode.

## The Tool Menu {#menu-tools}

**Tool \> Zoom** (Z)

:   Allows you to zoom on a point (left click) or area (click and drag); double-click to reset the zoom level to the initial state.

**Tool \> Draw Time Line** (L)

:   Allows you to temporarily draw a vertical line where you left click.

**Tool \> Select Channels** (C)

:   Allows you to select channels in order to act on them (move them to compensate for an offset, change the amplitude...)

**Tool \> Measure** (V)

:   Allows you to measure the time and the voltage between two points.

**Tool \> Select Time** (T)

:   Allows you to refine the time frame used to display the data.

**Tool \> Select Event** (E)

:   Allows you to select one event in order to move it or delete it.

**Tool \> Add Event**

:   Allows you to add a new event (select the type of event from the submenu).

## The Channels Menu {#menu-Channels}

**Channels \> Show Channels** (Ctrl+C)

:   Shows the selected channels.

**Channels \> Hide Channels** (Ctrl+H)

:   Hides the selected channels.

**Channels \> Move Channels to New Group** (Ctrl+G)

:   Moves the selected channels to a newly created group (either an anatomical group or a spike group).

**Channels \> Remove Channels from Group** (Shift+Delete)

:   Removes the selected channels from their current group and puts them in the Undefined Group (the "?" group). This does not actually remove the channels from the data file.

**Channels \> Discard Channels** (Delete)

:   Removes the selected channels from their current group and puts them in the Discarded Group (simultaneously acts on the anatomical and spike groups). This does not actually remove the channels from the data file.

**Channels \> Keep Channels** (Ctrl+Shift+K)

:   Brings skipped channels back into the display.

**Channels \> Skip Channels**

:   Skips the selected channels: they are no longer drawn, but keep their place in the layout.

**Channels \> Synchronize Groups**

:   Synchronizes the anatomical and spike groups. The existing spike groups are discarded and replaced with the anatomical groups.

**Channels \> Color by Anatomical Groups**

:   Colors the channels using the anatomical group colors.

**Channels \> Color by Spike Groups**

:   Colors the channels using the spike group colors.

## The Units Menu {#menu-clusters}

**Units \> Vertical Lines**

:   Draws spikes for selected clusters as long vertical lines.

**Units \> Raster**

:   Draws spikes for selected clusters as rasters below the local field potential traces.

**Units \> Waveforms**

:   Draws spike waveforms for selected clusters directly on the traces (only for `.dat` and `.fil` files).

**Units \> Increase Height** (Ctrl++)

:   Increases the height of the rasters.

**Units \> Decrease Height** (Ctrl+-)

:   Decreases the height of the rasters.

**Units \> Next Spike** (Ctrl+Shift+F)

:   Moves the time window to the next spike fired by one of the currently activated clusters.

**Units \> Previous Spike** (Ctrl+Shift+B)

:   Moves the time window to the previous spike fired by one of the currently activated clusters.

## The Events Menu {#menu-events}

**Events \> Next Event** (Ctrl+F)

:   Moves the time window to the next activated event.

**Events \> Previous Event** (Ctrl+B)

:   Moves the time window to the previous activated event.

**Events \> Remove Event** (Ctrl+K)

:   Removes the currently selected event.

## The Positions Menu {#menu-positions}

**Positions \> Show Position View**

:   Shows or hides the position view.

**Positions \> Show Events**

:   Shows or hides the selected events in the position view.

## The Traces Menu {#menu-traces}

**Traces \> Multiple Columns**

:   Toggles between single and multiple column mode. In multiple column mode, each anatomical group is represented on a separate column.

**Traces \> Grey-Scale**

:   Displays the channels in grey-scale.

**Traces \> Spectral View** (S)

:   Toggles the active display between the traces and the [Spectral View](./02-using-neuroscope.md#spectral-view).

**Traces \> Increase All Channel Amplitudes** (Ctrl+I)

:   Increases the amplitudes of all the channels (both visible and hidden).

**Traces \> Decrease All Channel Amplitudes** (Ctrl+D)

:   Decreases the amplitudes of all the channels (both visible and hidden).

**Traces \> Increase Selected Channel Amplitudes** (Ctrl+Shift+I)

:   Increases the amplitudes of the selected channels.

**Traces \> Decrease Selected Channel Amplitudes** (Ctrl+Shift+D)

:   Decreases the amplitudes of the selected channels.

**Traces \> Reset Selected Channel Offsets**

:   Resets the offsets of the selected channels to the default offsets.

**Traces \> Reset Selected Channel Amplitudes**

:   Resets the amplitudes of the selected channels.

**Traces \> Set Current Offsets as Defaults**

:   Sets the current offsets as the default offsets to be stored in the parameter file.

**Traces \> Set Default Offsets to Zero**

:   Sets the default offsets to be stored in the parameter file to zero.

**Traces \> Autocenter Channels**

:   Centres each trace on its mean value over the current time window.

**Traces \> Show Labels** (Ctrl+L)

:   Shows or hides the labels (channel IDs and gains).

**Traces \> Auto-advance to end of recording** (Ctrl+Shift+Space)

:   Pages forward automatically, one time window at a time, until the end of the recording.

**Traces \> Accelerate** (Ctrl+Up)

:   Shortens the auto-advance interval by 125 ms.

**Traces \> Decelerate** (Ctrl+Down)

:   Lengthens the auto-advance interval by 125 ms (up to 1000 ms).

## The Displays Menu {#menu-displays}

**Displays \> New Display** (Ctrl+N)

:   Creates a new display, copied from the active one.

**Displays \> Rename Active Display** (Ctrl+R)

:   Brings a dialog enabling the user to enter a new label for the currently active display.

**Displays \> Close Active Display** (Ctrl+W)

:   Closes the currently active display; when there is only one display, closes all data files as well (will prompt for saving if necessary).

## The Settings Menu {#menu-settings}

**Settings \> Show Main Toolbar**

:   Shows or hides the main toolbar.

**Settings \> Show Tools**

:   Shows or hides the tools toolbar.

**Settings \> Show StatusBar**

:   Shows or hides the Status Bar.

**Settings \> Display Calibration**

:   Shows or hides a calibration bar for channel 0 (voltage and gain are indicated).

**Settings \> Preferences**

:   Opens the [Preferences](./03-settings.md#settings) dialog, including the keyboard and mouse bindings (Input page).

## The Help Menu {#menu-help}

**Help \> Handbook** (F1)

:   Opens this user manual.

**Help \> Keyboard Shortcuts...**

:   Opens a list of all keyboard shortcuts and mouse bindings and the commands they trigger. See [Keyboard and Mouse Shortcuts](./02-using-neuroscope.md#shortcuts).

**Help \> About**

:   Shows information about the authors of and contributors to NeuroScope.

