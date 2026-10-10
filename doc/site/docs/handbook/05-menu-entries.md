# Menu Entries

## The File Menu {#menu-file}

[File \> Open]{.menuchoice}

:   Opens data files

[File \> Open Recent]{.menuchoice}

:   Opens data files selected from a list of recently edited data files.

[File \> Load Cluster File(s)...]{.menuchoice}

:   Loads multiple cluster files.

[File \> Load Event File(s)...]{.menuchoice}

:   Loads multiple event files.

[File \> Create Event File...]{.menuchoice}

:   Creates an event file.

[File \> Load Position File...]{.menuchoice}

:   Loads a single position file.

[File \> Save]{.menuchoice}

:   Saves the session file.

[File \> Save As]{.menuchoice}

:   Saves the session file in a new location.

[File \> Print]{.menuchoice}

:   Prints (or exports as PostScript or PDF) each view of the current display in a separate page.

[File \> Properties]{.menuchoice}

:   Opens a dialog displaying the properties of the currently opened file. These properties can be edited.

[File \> Close]{.menuchoice}

:   Closes the session (will prompt for saving if necessary).

[File \> Close Cluster File]{.menuchoice}

:   Closes the cluster file currently selected in the Units Palette.

[File \> Close Event File]{.menuchoice}

:   Closes the event file currently selected in the Events Palette.

[File \> Close Position File]{.menuchoice}

:   Closes the position file.

[File \> Quit]{.menuchoice}

:   Quits NeuroScope (will prompt for saving if necessary).

## The Edit Menu {#menu-edit}

[Edit \> Undo]{.menuchoice}

:   Undoes the last change (for event edition only).

[Edit \> Redo]{.menuchoice}

:   Redoes the last undone change (for event edition only).

[Edit \> Select All]{.menuchoice}

:   Selects all the items in the current palette (channels, clusters or events).

[Edit \> Select All except 0 and 1]{.menuchoice}

:   Selects all clusters except artefacts (cluster 0) and noise (cluster 1).

[Edit \> Deselect All]{.menuchoice}

:   Deselects all the items in the current palette (channels, clusters or events).

[Edit \> Edit Mode]{.menuchoice}

:   Swiches between edit and non edit mode.

## The Tools Menu {#menu-tools}

[Tools \> Zoom]{.menuchoice}

:   Allows you to zoom on a point (left click) or area (click and drag); double-click to reset the zoom level to the initial state.

[Tools \> Draw Time Line]{.menuchoice}

:   Allows you to temporarily draw a vertical line where you left click.

[Tools \> Select Channels]{.menuchoice}

:   Allows you to select channels in order to act on them (move them to compensate for an offset, change the amplitude...)

[Tools \> Measure]{.menuchoice}

:   Allows you to measure the time and the voltage between two points.

[Tools \> Select Time]{.menuchoice}

:   Allows you to refine the time frame used to display the data.

[Tools \> Select Events]{.menuchoice}

:   Allows you to select one event in order to move it or delete it.

[Tools \> Add Event]{.menuchoice}

:   Allows you to add a new event (select the type of event from the submenu).

## The Channels Menu {#menu-Channels}

[Channels \> Show Channels]{.menuchoice}

:   Shows the selected channels.

[Channels \> Hide Channels]{.menuchoice}

:   Hides the selected channels.

[Channels \> Move Channels to New Group]{.menuchoice}

:   Moves the selected channels to a newly created group (either an anatomical group or a spike group).

[Channels \> Remove Channels from Group]{.menuchoice}

:   Removes the selected channels from their current group and puts them in the Undefined Group (the "?" group). This does not actually remove the channels from the data file.

[Channels \> Discard Channels]{.menuchoice}

:   Removes the selected channels from their current group and puts them in the Discarded Group (simultaneously acts on the anatomical and spike groups). This does not actually remove the channels from the data file.

[Channels \> Synchronize Groups]{.menuchoice}

:   Synchronizes the anatomical and spike groups. The existing spike groups are discarded and replaced with the anatomical groups.

[Channels \> Color by Anatomical Groups]{.menuchoice}

:   Colors the channels using the anatomical group colors.

[Channels \> Color by Spike Groups]{.menuchoice}

:   Colors the channels using the spike group colors.

## The Units Menu {#menu-clusters}

[Units \> Vertical Lines]{.menuchoice}

:   Draws spikes for selected clusters as long vertical lines.

[Units \> Raster]{.menuchoice}

:   Draws spikes for selected clusters as rasters below the local field potential traces.

[Units \> Waveforms]{.menuchoice}

:   Draws spike waveforms for selected clusters directly on the local field potential traces.

[Units \> Next Spike]{.menuchoice}

:   Moves the time window to the next spike fired by one of the currently activated clusters.

[Units \> Previous Spike]{.menuchoice}

:   Moves the time window to the previous spike fired by one of the currently activated clusters.

## The Events Menu {#menu-events}

[Events \> Next Event]{.menuchoice}

:   Moves the time window to the next activated event.

[Events \> Previous Event]{.menuchoice}

:   Moves the time window to the previous activated event.

[Events \> Remove Event]{.menuchoice}

:   Removes the currently selected event.

## The Positions Menu {#menu-positions}

[Positions \> Show Position View]{.menuchoice}

:   Shows or hides the position view.

## The Traces Menu {#menu-traces}

[Traces \> Multiple Columns]{.menuchoice}

:   Toggles between single and multiple column mode. In mutiple column mode, each anatomical group is represented on a separate column.

[Traces \> Grey-Scale]{.menuchoice}

:   Displays the channels in gray-scale.

[Traces \> Increase All Channel Amplitudes]{.menuchoice}

:   Increases the amplitudes of all the channels (both visible and hidden).

[Traces \> Decrease All Channel Amplitudes]{.menuchoice}

:   Decreases the amplitudes of all the channels (both visible and hidden).

[Traces \> Increase Selected Channel Amplitudes]{.menuchoice}

:   Increases the amplitudes of the selected channels.

[Traces \> Decrease Selected Channel Amplitudes]{.menuchoice}

:   Decreases the amplitudes of the selected channels.

[Traces \> Reset Selected Channel Amplitude]{.menuchoice}

:   Resets the amplitudes of the selected channels.

[Traces \> Reset Selected Channel Offsets]{.menuchoice}

:   Resets the offsets of the selected channels to the default offsets.

[Traces \> Set Current Offsets as Defaults]{.menuchoice}

:   Sets the current offsets as the default offsets to be stored in the parameter file.

[Traces \> Set Default Offsets to Zero]{.menuchoice}

:   Sets the default offsets to be stored in the parameter file to zero.

[Traces \> Show Labels]{.menuchoice}

:   Shows or hides the labels (channel IDs and gains).

## The Displays Menu {#menu-displays}

[Displays \> New Display]{.menuchoice}

:   Creates a new Display.

[Displays \> Rename Active Display]{.menuchoice}

:   Brings a dialog enabling the user to enter a new label for the currently active display.

[Displays \> Close Active Display]{.menuchoice}

:   Closes the currently active display; when there is only one display, closes all data files as well (will prompt for saving if necessary).

## The Settings Menu {#menu-settings}

[Settings \> Show Toolbar]{.menuchoice}

:   Shows or hides the Toolbar.

[Settings \> Show Tools]{.menuchoice}

:   Shows or hides the Tools.

[Settings \> Show Parameters]{.menuchoice}

:   Shows or hides the Parameter Bar.

[Settings \> Show Status Bar]{.menuchoice}

:   Shows or hides the Status Bar.

[Settings \> Display Calibration]{.menuchoice}

:   Shows or hides a calibration bar for channel 0 (voltage and gain are indicated).

[Settings \> Configure Shortcuts]{.menuchoice}

:   Lets you configure the shortcuts.

[Settings \> Configure neuroscope]{.menuchoice}

:   Lets you configure a number of settings including default behaviors.

## The Help Menu {#menu-help}

[Help \> neuroscope Handbook]{.menuchoice}

:   Opens this user manual in the Help Center.

[Help \> Keyboard Shortcuts...]{.menuchoice}

:   Opens a list of all keyboard shortcuts and mouse bindings and the commands they trigger. See [Keyboard and Mouse Shortcuts](./02-using-neuroscope.md#shortcuts).

[Help \> What's this?]{.menuchoice}

:   Activates the context help tool.

[Help \> Report Bug]{.menuchoice}

:   Helps you compose an e-mail message to report a bug to the author of NeuroScope.

[Help \> About neuroscope]{.menuchoice}

:   Shows information about the authors of and contributors to NeuroScope.

[Help \> About KDE]{.menuchoice}

:   Shows information about the authors of and contributors to KDE.

