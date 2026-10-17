#ifndef NEUROSCOPE_SPECTRALVIEW_H
#define NEUROSCOPE_SPECTRALVIEW_H

// Spectral counterpart of TraceView. Shares the recording's TracesProvider,
// requests the visible time window, feeds the multitaper compute engine, and
// renders the resulting spectral image as a colour heatmap. Toggled in place
// with the waveform view; the inspector below the channels drives the setters.

#include "baseframe.h"
#include "tracesprovider.h"

#include <neurosuite/spectral/spectralengine.h>
#include "spectral/spectralrender.h"
#include <neurosuite/spectral/colormap.h>
#include <neurosuite/spectral/spectralwindow.h>

#include <QImage>
#include <QList>
#include <list>
#include <utility>

class QTimer;
class QWheelEvent;
namespace input { class BindingRegistry; }
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

    // --- inspector-driven parameters -----------------------------------------
    // Each setter stages its value; nothing the view shows changes until the change
    // is committed ("u", commitNow(), or the debounce delay when one is set).  The
    // displayed image always matches the committed parameters, so scrolling or a
    // channel change never applies a half-edited set.
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
    void setCar(bool on);
    void setRefRegress(bool on);
    void setRefChannel(int row);
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

    /**Commits the staged parameter/window changes now (bound to the "u" key).
    * With the default manual update mode this is the only thing that applies an
    * inspector change; with a debounce delay set it also flushes early.*/
    void commitNow();

    /**Debounce delay (ms) before a parameter change is applied. 0 (the default)
    * means manual: changes are held until the user presses "u" (commitNow).*/
    void setUpdateDelay(int ms) { recomputeDelayMs = ms < 0 ? 0 : ms; }

    /// The staged parameters -- what the inspector controls show.  The image on
    /// screen was computed with the committed set, which equals this one unless a
    /// change is waiting for commitNow().
    const neuroscope::spectral::SpectralParams& spectralParams() const { return pending; }

    // Display-state accessors (not part of SpectralParams) so a shared
    // inspector can reload its controls when it is retargeted to this view.
    neuroscope::spectral::Colormap colormapValue() const { return colormap; }
    double dynamicRange() const { return dynamicRangeDb; }
    bool   isAutoScale() const { return autoScale; }
    bool   isLockedToTrace() const { return pendingLockToTrace; }
    long   spanMs() const { return pendingSpan; }

    /**Registers the spectral view's mouse commands (view.spectral scope), the Klusters
    * trace view's gestures: Ctrl+Left drag pans the magnified image (paging the time
    * window at its edges), Ctrl+wheel magnifies both axes toward the cursor, the plain
    * wheel magnifies time only.  Call from NeuroscopeApp::registerInputBindings after
    * BaseFrame::registerInput, so view.spectral shadows view.frame.*/
    static void registerInput(input::BindingRegistry& reg);

public Q_SLOTS:
    void dataAvailable(Array<dataType>& data, QObject* initiator);

Q_SIGNALS:
    /// Emitted when the displayed frequency range changes, so the band slider
    /// can update its extent to match.
    void frequencyRangeChanged(double lowHz, double highHz);
    /// Emitted when the spectral mode changes (e.g. to show the band slider
    /// only in channel mode).
    void spectralModeChanged(neuroscope::spectral::SpectralMode mode);
    /// Asks the owning TraceWidget to show [start, start+duration) (ms): the
    /// horizontal pan and zoom move the shared time window, so the trace view and
    /// the scroll bar follow.
    void timeWindowRequested(long start, long duration);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void requestCurrentWindow();
    void applyWindow();        // derive the spectral window from the trace window
    void recompute();          // run the engine on the current window, rebuild image
    void rebuildImage();       // SpectralImage -> QImage
    void drawAxes(QPainter& painter, const QRect& plot);
    QRect plotRect() const;    // heatmap area inside the axis margins

    // Mouse navigation (see registerInput).  The view shows the part
    // [xLo, xHi] x [yLo, yHi] of the computed image, as fractions of its width and
    // height from the top-left; the zooms magnify within it, as the trace view's do.
    void beginPan(const QPoint& pos);
    void zoomAtCursor(bool zoomIn, const QPoint& pos, bool horizontalOnly);
    void requestTraceStart(long start, long duration);
    void resetView() { xLo = 0.0; xHi = 1.0; yLo = 0.0; yHi = 1.0; }
    QPointF imageFractionAt(const QPoint& pos) const;   // pos -> [0,1]^2 of the image

    // Renders the current window as a moving-window preview (single taper) and
    // arms the settle pass, or as the full estimate for modes with no preview.
    void renderMoving();
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

    // Small LRU of recently settled full estimates, keyed by window id, so that
    // scrolling back to a visited window renders the full image at once instead
    // of preview-then-settle. Cleared when the estimator parameters change.
    bool recentGet(std::uint64_t windowId, neuroscope::spectral::SpectralImage& out);
    void recentPut(std::uint64_t windowId, const neuroscope::spectral::SpectralImage& img);

    // Persist/restore inspector + render settings across sessions (QSettings).
    void loadSettings();
    void saveSettings() const;

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
    neuroscope::spectral::SpectralParams params;    // committed: what is computed
    neuroscope::spectral::SpectralParams pending;   // staged by the inspector
    bool pendingLockToTrace = true;
    long pendingSpan = 0;
    bool lastIsPreview = false;  // the image on screen is the single-taper preview
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

    // Most-recent-first list of (windowId, full estimate); front is newest.
    std::list<std::pair<std::uint64_t, neuroscope::spectral::SpectralImage>> recent_;
    std::size_t recentCap = 16;

    // Visible part of the image and the Ctrl+drag pan state.
    double xLo = 0.0;
    double xHi = 1.0;
    double yLo = 0.0;
    double yHi = 1.0;
    bool   panArmed = false;
    bool   panning = false;
    QPoint panAnchorPx;
    QPointF panGrab;               // image fraction grabbed at the press
    bool   keepViewOnNextWindow = false;   // a pan paged the window: keep the zoom
    static constexpr int    panDragThreshold = 3;      // px, as in the Klusters trace view
    static constexpr double wheelZoomStep = 1.25;      // per notch, as in Klusters
    static constexpr double maxZoom = 22.73;           // ZoomWindow's MAX_SCALE
};

#endif // NEUROSCOPE_SPECTRALVIEW_H
