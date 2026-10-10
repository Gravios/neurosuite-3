# Settings

Selecting [Settings \> Configure neuroscope]{.menuchoice} will open the Settings Dialog. This lets you configure several default behaviors of NeuroScope.

<a id="settings-general"></a>General

:   In this page, you can configure the following:

    -   <a id="settings-background"></a>Background color. This setting affects all views.

    -   Display labels in the palette headers. This shows or hides the labels ('Anatomy','Spikes', 'Units' and 'Events') next to the icons in the palette headers. Hiding them saves some space.

    -   Align events.... This setting is used when you browse events. For instance, each time you move to the next event, the time window is shifted forward in such a way that the next event appears at a fixed position in the display. This setting determines where the events are aligned in the display.

        Align units.... Same as above, for spikes.

<a id="defaults"></a>Defaults

:   In this page, you can configure the default values for the items contained in the following categories.

    Channels

    -   Number of channels. Default number of channels.

    -   Sampling rate (Hz). Default sampling rate of the `.dat` files.

    -   Resolution (bits). Default resolution of the recording system.

    -   Initial offset. Default offset in the recording system.

    -   Voltage range (V). Default total voltage range of the recording system, e.g. 10V for ±5V.

    -   Amplification. Default gain of the amplifiers.

    -   Screen gain (mV/cm). Default screen gain (see [this paragraph](./02-using-neuroscope.md#screen-gain) for details).

    -   Sampling rate for EEG (Hz). Default sampling rate of the local field potentials (`.eeg`) file.

    -   Background image. Background image for the Trace View. You can use any image format supported by Qt: JPEG, PNG, BMP, XBM, XPM, PNM, MNG and GIF. See your Qt documentation for details (e.g. the QImageIO class description).

    Units

    -   Number of samples per waveform. Default number of samples per waveform, used when highlighting waveforms on the traces.

    -   Index of the peak sample. Default peak sample in the waveforms, used when highlighting waveforms on the traces.

    Positions

    -   Sampling rate (Hz). Default sampling rate of the position file.

    -   Width (pixels). Default horizontal resolution of the video camera.

    -   Height (pixels). Default vertical resolution of the video camera.

    -   Background image. Background image for the Position View. You can use any image format supported by Qt: JPEG, PNG, BMP, XBM, XPM, PNM, MNG and GIF. See your Qt documentation for details (e.g. the QImageIO class description).

    -   Draw trajectory. Activate this to draw over the background (be it a black background or an image) an outline of the trajectory of the animal throughout the experiment.

    -   Rotate CCW (degrees). This allows you to rotate (counterclockwise) both the position data and the background image.

    -   flip. This allows you to flip both the position data and the background image.

<a id="settings-input"></a>Input

:   In this page, you can view and change the keyboard shortcuts and mouse bindings used throughout NeuroScope, and keep named [keymap layouts](./02-using-neuroscope.md#shortcuts-layouts). See [Keyboard and Mouse Shortcuts](./02-using-neuroscope.md#shortcuts) for details.

