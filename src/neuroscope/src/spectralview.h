#ifndef NEUROSCOPE_SPECTRALVIEW_H
#define NEUROSCOPE_SPECTRALVIEW_H

// Spectral counterpart of TraceView. Shares the recording's TracesProvider,
// requests the visible time window, feeds the multitaper compute engine, and
// renders the resulting spectral image as a colour heatmap. Toggled in place
// with the waveform view; the inspector below the channels drives the setters.

#include "baseframe.h"
#include "tracesprovider.h"

#include "spectral/spectralengine.h"
#include "spectral/spectralrender.h"
#include "spectral/colormap.h"
#include "spectral/spectralwindow.h"

#include <QImage>
#include <QList>

class QTimer;
template <typename T> class QFutureWatcher;

class SpectralView : public BaseFrame {
    Q_OBJECT

public:
    SpectralView(TracesProvider& tracesProvider,
                 const QList<int>& channelsToDisplay,
                 long start, long timeFrameWidth,
                 QWidget* parent = nullptr,
                 const QString& name = QString(),
                 const QColor& backgroundColor = Qt::black);
    ~SpectralView() override;

    /// Show the window [start, start+timeFrameWidth) (milliseconds).
    void displayTimeFrame(long start, long timeFrameWidth);

    /// Channels (0-based ids) to include, in display order.
    void setChannels(const QList<int>& channels);

    // --- inspector-driven parameters; each recomputes and repaints ---------
    void setSpectralMode(neuroscope::spectral::SpectralMode mode);
    void setWindowSamples(int n);
    void setNfft(int n);
    void setStep(int s);
    void setTimeBandwidth(double nw);
    void setNumTapers(int k);
    void setFrequencyRange(double lowHz, double highHz);

    /// Set the mode-B integration sub-band [lowHz, highHz] (Hz). This re-sums
    /// the retained cube and repaints immediately, without recomputing.
    void setBand(double lowHz, double highHz);
    void setSingleChannelRow(int row);
    void setWhitening(bool on);
    void setDecimate(bool on);
    void setColormap(neuroscope::spectral::Colormap cm);
    void setDynamicRangeDb(double db);

    /**If true, the colour scale spans the full observed power range of the
    * current image; if false, it uses the fixed dynamic range (setDynamicRangeDb).*/
    void setAutoScale(bool on);

    void setBackend(neuroscope::spectral::SpectralBackend backend);

    /**If true, the spectral view uses the trace window unchanged. If false, it
    * uses its own span (setSpan) but stays centred on the trace window centre.*/
    void setLockToTrace(bool on);

    /**Sets the spectral view's own window width (ms), used when not locked to
    * the trace window. The view re-centres on the current trace centre.*/
    void setSpan(long ms);

    /**Applies any pending parameter/window change now (bound to the "u" key).
    * With the default manual update mode this is the only thing that triggers a
    * recompute after an inspector change; with a debounce delay set it also
    * flushes early.*/
    void commitNow();

    /**Debounce delay (ms) before a parameter change is applied. 0 (the default)
    * means manual: changes are held until the user presses "u" (commitNow).*/
    void setUpdateDelay(int ms) { recomputeDelayMs = ms < 0 ? 0 : ms; }

    const neuroscope::spectral::SpectralParams& spectralParams() const { return params; }

    // Display-state accessors (not part of SpectralParams) so a shared
    // inspector can reload its controls when it is retargeted to this view.
    neuroscope::spectral::Colormap colormapValue() const { return colormap; }
    double dynamicRange() const { return dynamicRangeDb; }
    bool   isAutoScale() const { return autoScale; }
    bool   isLockedToTrace() const { return lockToTrace; }
    long   spanMs() const { return span; }

public Q_SLOTS:
    void dataAvailable(Array<dataType>& data, QObject* initiator);

Q_SIGNALS:
    /// Emitted when the displayed frequency range changes, so the band slider
    /// can update its extent to match.
    void frequencyRangeChanged(double lowHz, double highHz);
    /// Emitted when the spectral mode changes (e.g. to show the band slider
    /// only in channel mode).
    void spectralModeChanged(neuroscope::spectral::SpectralMode mode);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void requestCurrentWindow();
    void applyWindow();        // derive the spectral window from the trace window
    void recompute();          // run the engine on the current window, rebuild image
    void rebuildImage();       // SpectralImage -> QImage
    void drawAxes(QPainter& painter, const QRect& plot);

    // Debounced application of inspector changes: setters mark what is dirty and
    // (re)start a short timer; flushPending() applies once when it fires or when
    // the user presses "u" (commitNow). Avoids recomputing on every keystroke.
    void scheduleParamUpdate();
    void scheduleWindowUpdate();
    void flushPending();

    // While the time window is moving, recompute() renders a fast preview
    // (single taper on decimated data); settleNow(), fired once the window
    // stops, renders the full multitaper estimate. previewParams() builds the
    // fast variant of the current params.
    void settleNow();
    neuroscope::spectral::SpectralParams previewParams() const;
    // Flatten the current window into sample-major doubles + valid channel list.
    bool buildInput(std::vector<double>& sampleMajor, std::vector<int>& chans,
                    int& nSamples, int& nCh) const;
    // Applies a background full-multitaper result if it is still current.
    void onSettleComputed();

    TracesProvider& tracesProvider;
    QList<int> channels;

    long startTime = 0;
    long endTime = 0;
    long timeFrameWidth = 0;
    long startTimeInRecordingUnits = 0;

    // Trace window last received, and the view's own centring options.
    long traceStart = 0;
    long traceWidth = 0;
    bool lockToTrace = true;
    long span = 0;             // own width (ms) when unlocked; <=0 follows trace
    long recordingLength = 0;

    Array<dataType> data;
    bool dataReady = false;

    neuroscope::spectral::SpectralEngine engine;
    neuroscope::spectral::SpectralParams params;
    neuroscope::spectral::SpectralImage lastImage;
    neuroscope::spectral::Colormap colormap = neuroscope::spectral::Colormap::Viridis;
    double dynamicRangeDb = 60.0;
    bool autoScale = true;

    QImage image;

    QTimer* recomputeTimer = nullptr;
    bool paramsDirty = false;  // a parameter change awaits recompute
    bool windowDirty = false;  // a span/lock change awaits a window re-derive
    int  recomputeDelayMs = 0;   // 0 = manual: changes apply only on "u"

    QTimer* settleTimer = nullptr; // fires when the window stops moving
    bool movePreview = false;      // true while scrolling: render the fast preview

    // Auto-scale continuity: previews reuse the value range of the last full
    // (multitaper) estimate so the colour scale does not jump on settle.
    double fullScaleMin = 0.0;
    double fullScaleMax = 0.0;
    bool   haveFullScale = false;

    // Background full-multitaper pass that replaces the settle render when ready.
    QFutureWatcher<neuroscope::spectral::SpectralImage>* mtWatcher = nullptr;
    unsigned long settleGen = 0;        // bumped on each move; discards stale results
    unsigned long pendingSettleGen = 0; // generation of the in-flight background pass
    std::uint64_t pendingWindowId = 0;
};

#endif // NEUROSCOPE_SPECTRALVIEW_H
