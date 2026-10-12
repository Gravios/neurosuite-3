#include "spectralview.h"

#include "input/bindingregistry.h"
#include "input/inputdispatcher.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QRect>
#include <QtConcurrentRun>
#include <QFutureWatcher>
#include <QSettings>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using neuroscope::spectral::SpectralMode;
using neuroscope::spectral::SpectralParams;
using neuroscope::spectral::SpectralImage;
using neuroscope::spectral::Colormap;
using neuroscope::spectral::SpectralBackend;

namespace {
// A "nice" axis step (1/2/5 x 10^k) giving roughly targetTicks divisions.
double niceStep(double range, int targetTicks)
{
    if (range <= 0.0 || targetTicks < 1) return 1.0;
    const double raw = range / targetTicks;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    double nice;
    if (norm < 1.5) nice = 1.0;
    else if (norm < 3.0) nice = 2.0;
    else if (norm < 7.0) nice = 5.0;
    else nice = 10.0;
    return nice * mag;
}
} // namespace

SpectralView::SpectralView(TracesProvider& tracesProvider,
                           const QList<int>& channelsToDisplay,
                           long start, long timeFrameWidth,
                           QWidget* parent, const QString& name,
                           const QColor& backgroundColor)
    : BaseFrame(10, 0, parent, name, backgroundColor),
      tracesProvider(tracesProvider),
      channels(channelsToDisplay),
      startTime(start),
      endTime(start + timeFrameWidth),
      timeFrameWidth(timeFrameWidth)
{
    // Default estimation parameters; sampling rate from the recording.
    params.samplingRate  = tracesProvider.getSamplingRate();
    params.mode          = SpectralMode::TimeFrequencySingleChannel;
    params.windowSamples = 256;
    params.nfft          = 256;
    params.stepSamples   = 128;
    params.nw            = 3.0;
    params.nTapers       = 5;
    params.freqLow       = 0.0;
    params.freqHigh      = 0.0;   // Nyquist
    params.singleChannel = 0;
    params.whiten        = false;
    params.car           = false;
    params.refRegress    = false;
    params.refChannel    = 0;

    connect(&tracesProvider, &TracesProvider::dataReady, this,
            static_cast<void (SpectralView::*)(Array<dataType>&, QObject*)>(&SpectralView::dataAvailable));

    recomputeTimer = new QTimer(this);
    recomputeTimer->setSingleShot(true);
    connect(recomputeTimer, &QTimer::timeout, this, &SpectralView::flushPending);

    // Fires once the time window stops moving, to replace the fast scroll
    // preview with the full multitaper estimate.
    settleTimer = new QTimer(this);
    settleTimer->setSingleShot(true);
    connect(settleTimer, &QTimer::timeout, this, &SpectralView::settleNow);

    mtWatcher = new QFutureWatcher<neuroscope::spectral::SpectralImage>(this);
    connect(mtWatcher, &QFutureWatcher<neuroscope::spectral::SpectralImage>::finished,
            this, &SpectralView::onSettleComputed);

    loadSettings();   // restore inspector/render settings over the defaults above
    pending = params;
    pendingLockToTrace = lockToTrace;
    pendingSpan = span;

    traceStart = startTime;
    traceWidth = timeFrameWidth;
    recordingLength = tracesProvider.recordingLength();
    applyWindow();
}

SpectralView::~SpectralView() { saveSettings(); }

namespace {
constexpr auto kSpectralGroup = "spectral";
}

void SpectralView::loadSettings()
{
    QSettings s;
    s.beginGroup(kSpectralGroup);
    if (!s.contains("mode")) { s.endGroup(); return; }  // nothing saved yet

    params.mode = static_cast<neuroscope::spectral::SpectralMode>(
        s.value("mode", static_cast<int>(params.mode)).toInt());
    params.singleChannel = s.value("singleChannel", params.singleChannel).toInt();
    params.nw            = s.value("nw", params.nw).toDouble();
    params.nTapers       = s.value("nTapers", params.nTapers).toInt();
    params.windowSamples = s.value("windowSamples", params.windowSamples).toInt();
    params.nfft          = s.value("nfft", params.nfft).toInt();
    params.stepSamples   = s.value("stepSamples", params.stepSamples).toInt();
    params.freqLow       = s.value("freqLow", params.freqLow).toDouble();
    params.freqHigh      = s.value("freqHigh", params.freqHigh).toDouble();
    params.whiten        = s.value("whiten", params.whiten).toBool();
    params.car           = s.value("car", params.car).toBool();
    params.refRegress    = s.value("refRegress", params.refRegress).toBool();
    params.refChannel    = s.value("refChannel", params.refChannel).toInt();
    params.decimate      = s.value("decimate", params.decimate).toBool();
    params.bandLo        = s.value("bandLo", params.bandLo).toDouble();
    params.bandHi        = s.value("bandHi", params.bandHi).toDouble();
    colormap = static_cast<neuroscope::spectral::Colormap>(
        s.value("colormap", static_cast<int>(colormap)).toInt());
    autoScale      = s.value("autoScale", autoScale).toBool();
    dynamicRangeDb = s.value("dynamicRangeDb", dynamicRangeDb).toDouble();
    lockToTrace    = s.value("lockToTrace", lockToTrace).toBool();
    span           = s.value("span", static_cast<qlonglong>(span)).toLongLong();
    s.endGroup();
}

void SpectralView::saveSettings() const
{
    // The staged values: what the inspector shows is what the next session opens with.
    QSettings s;
    s.beginGroup(kSpectralGroup);
    s.setValue("mode", static_cast<int>(pending.mode));
    s.setValue("singleChannel", pending.singleChannel);
    s.setValue("nw", pending.nw);
    s.setValue("nTapers", pending.nTapers);
    s.setValue("windowSamples", pending.windowSamples);
    s.setValue("nfft", pending.nfft);
    s.setValue("stepSamples", pending.stepSamples);
    s.setValue("freqLow", pending.freqLow);
    s.setValue("freqHigh", pending.freqHigh);
    s.setValue("whiten", pending.whiten);
    s.setValue("car", pending.car);
    s.setValue("refRegress", pending.refRegress);
    s.setValue("refChannel", pending.refChannel);
    s.setValue("decimate", pending.decimate);
    s.setValue("bandLo", pending.bandLo);
    s.setValue("bandHi", pending.bandHi);
    s.setValue("colormap", static_cast<int>(colormap));
    s.setValue("autoScale", autoScale);
    s.setValue("dynamicRangeDb", dynamicRangeDb);
    s.setValue("lockToTrace", pendingLockToTrace);
    s.setValue("span", static_cast<qlonglong>(pendingSpan));
    s.endGroup();
}

void SpectralView::requestCurrentWindow()
{
    dataReady = false;
    tracesProvider.requestData(startTime, endTime, this, startTimeInRecordingUnits);
}

void SpectralView::displayTimeFrame(long start, long width)
{
    traceStart = start;
    traceWidth = width;
    // The file can grow (NeuroScope's auto-forward paging re-reads its length),
    // and the unlocked window is clamped to it.
    recordingLength = tracesProvider.recordingLength();
    // A moving window renders the fast preview; the settle timer restarts on
    // every move and triggers the full multitaper once it stops (~200 ms idle).
    // FrequencyAcrossChannels needs no settle - its single compute is the full
    // estimate - so it is not marked as a preview at all (a stale "preview" flag
    // left over from that mode used to pin the next mode's image at one taper).
    movePreview = params.mode != neuroscope::spectral::SpectralMode::FrequencyAcrossChannels;
    ++settleGen;          // invalidates any in-flight background multitaper pass
    if (movePreview)
        settleTimer->start(200);
    applyWindow();
}

void SpectralView::renderMoving()
{
    movePreview = params.mode != neuroscope::spectral::SpectralMode::FrequencyAcrossChannels;
    ++settleGen;
    if (movePreview)
        settleTimer->start(200);
    if (dataReady) { recompute(); update(); }
}

void SpectralView::applyWindow()
{
    // The spectral view stays centred on the trace window's centre, using the
    // trace width when locked or its own span otherwise.
    const neuroscope::spectral::TimeWindow w =
        neuroscope::spectral::spectralWindow(traceStart, traceWidth,
                                             lockToTrace, span, recordingLength);
    startTime = w.start;
    timeFrameWidth = w.width;
    endTime = w.start + w.width;
    requestCurrentWindow();
}

// Setters only stage a change (into pending / pendingLockToTrace / pendingSpan);
// nothing on screen or in the caches is touched until flushPending() commits it.
// Previously the setters wrote the live parameters and only deferred the
// recompute, so the next scroll, settle or band drag applied a half-edited set
// while "u" was still outstanding, and the title showed values the image was not
// computed with.
void SpectralView::scheduleParamUpdate()
{
    paramsDirty = true;
    update();      // the title flags the uncommitted change
    if (recomputeDelayMs > 0) recomputeTimer->start(recomputeDelayMs);
    // else: manual mode — the change waits for commitNow() ("u").
}

void SpectralView::scheduleWindowUpdate()
{
    windowDirty = true;
    update();
    if (recomputeDelayMs > 0) recomputeTimer->start(recomputeDelayMs);
    // else: manual mode — the change waits for commitNow() ("u").
}

void SpectralView::flushPending()
{
    recomputeTimer->stop();
    if (!paramsDirty && !windowDirty) return;

    const bool modeChanged  = pending.mode != params.mode;
    const bool rangeChanged = pending.freqLow != params.freqLow
                           || pending.freqHigh != params.freqHigh;
    const bool windowChanged = windowDirty
                            || pendingLockToTrace != lockToTrace || pendingSpan != span;
    pending.bandLo = params.bandLo;     // the band is live (setBand), never staged
    pending.bandHi = params.bandHi;
    params = pending;
    lockToTrace = pendingLockToTrace;
    span = pendingSpan;
    paramsDirty = false;
    windowDirty = false;

    engine.invalidate();
    recent_.clear();       // cached estimates are for the old parameters / window
    ++settleGen;           // drop any in-flight pass computed with them

    if (modeChanged || rangeChanged) resetRowView();   // the vertical axis changed
    if (rangeChanged) emit frequencyRangeChanged(params.freqLow, params.freqHigh);
    if (modeChanged)  emit spectralModeChanged(params.mode);

    // Render as a preview and settle in the background, as a scroll does, rather
    // than running the full multitaper on the UI thread.
    if (windowChanged) {
        movePreview = params.mode != neuroscope::spectral::SpectralMode::FrequencyAcrossChannels;
        if (movePreview) settleTimer->start(200);
        applyWindow();              // re-derives the window; recompute on dataReady
    } else {
        renderMoving();
    }
}

void SpectralView::commitNow()
{
    flushPending();
}

void SpectralView::setChannels(const QList<int>& chans)
{
    // The displayed channels are not an inspector parameter: follow them at once,
    // as the trace view does.  (They used to wait for "u" -- or leak in on the next
    // scroll.)  TraceWidget calls this on every show, so an unchanged list must not
    // throw the cached estimates away; a hidden view only records the list, and
    // displayTimeFrame() renders it when the view is shown.
    if (chans == channels) return;
    channels = chans;
    recent_.clear();
    if (isVisible()) renderMoving();
    else ++settleGen;
}

void SpectralView::dataAvailable(Array<dataType>& incoming, QObject* initiator)
{
    if (initiator != this) return;       // not our request
    if (incoming.nbOfRows() == 0) {       // I/O error upstream
        if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
            qDebug() << "[spectral] dataAvailable: 0 rows (no data) - keeping current frame";
        return;
    }

    data = incoming;
    dataReady = true;
    if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
        qDebug() << "[spectral] dataAvailable rows=" << data.nbOfRows()
                 << "cols=" << data.nbOfColumns() << "movePreview=" << movePreview;
    recompute();
    update();
    // A staged (uncommitted) change stays staged: this render used the committed
    // parameters, not the staged ones.
}

bool SpectralView::buildInput(std::vector<double>& sampleMajor, std::vector<int>& chans,
                              int& nSamples, int& nCh) const
{
    nSamples = static_cast<int>(data.nbOfRows());
    nCh = static_cast<int>(data.nbOfColumns());
    if (nSamples <= 0 || nCh <= 0) return false;

    // Array is 1-based (sample i, channel j); flatten to sample-major double.
    sampleMajor.assign(static_cast<std::size_t>(nSamples) * nCh, 0.0);
    for (int s = 0; s < nSamples; ++s)
        for (int c = 0; c < nCh; ++c)
            sampleMajor[static_cast<std::size_t>(s) * nCh + c] =
                static_cast<double>(data(s + 1, c + 1));

    chans.clear();
    chans.reserve(channels.size());
    for (int c : channels)
        if (c >= 0 && c < nCh) chans.push_back(c);
    return !chans.empty();
}

void SpectralView::recompute()
{
    params.samplingRate = tracesProvider.getSamplingRate();
    const std::uint64_t windowId =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(startTime)) << 32)
        ^ static_cast<std::uint32_t>(timeFrameWidth);

    // Revisiting a window we already settled: render its full estimate at once,
    // re-applying the current band, and skip the preview/settle cycle.
    if (movePreview && recentGet(windowId, lastImage)) {
        neuroscope::spectral::integrateBand(lastImage, params.bandLo, params.bandHi);
        fullScaleMin = lastImage.valueMin; fullScaleMax = lastImage.valueMax;
        haveFullScale = true;
        movePreview = false;
        lastIsPreview = false;
        settleTimer->stop();
        rebuildImage();
        return;
    }

    std::vector<double> sampleMajor;
    std::vector<int> chans;
    int nSamples = 0, nCh = 0;
    if (!buildInput(sampleMajor, chans, nSamples, nCh)) {
        if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
            qDebug() << "[spectral] recompute: buildInput failed (no valid channels/data)"
                     << "channels=" << channels.size() << "nCh=" << nCh;
        return;   // keep the current frame rather than blanking to "computing..."
    }

    // FrequencyAcrossChannels (band-pass + RMS) is already cheap and has no
    // preview tier, so treat it as a full estimate; other modes preview while
    // moving and settle to the full multitaper.
    const bool preview = movePreview &&
        params.mode != neuroscope::spectral::SpectralMode::FrequencyAcrossChannels;
    const neuroscope::spectral::SpectralParams& p = preview ? previewParams() : params;
    lastImage = engine.compute(sampleMajor.data(), nSamples, nCh, chans, p, windowId);
    lastIsPreview = preview && lastImage.valid();
    // A preview is only ever an interim image: make sure a settle pass follows it.
    if (preview && !settleTimer->isActive()) settleTimer->start(200);
    if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
        qDebug() << "[spectral] compute" << (preview ? "preview" : "full")
                 << "backend=" << static_cast<int>(p.backend)
                 << "valid=" << lastImage.valid()
                 << "rows=" << lastImage.rows << "cols=" << lastImage.cols;
    if (preview && autoScale && haveFullScale) {
        // Show the preview at the last full estimate's scale to avoid a flash.
        lastImage.valueMin = fullScaleMin;
        lastImage.valueMax = fullScaleMax;
    } else if (!preview) {
        fullScaleMin = lastImage.valueMin; fullScaleMax = lastImage.valueMax;
        haveFullScale = true;
        recentPut(windowId, lastImage);   // a synchronous full estimate is cacheable
    }
    rebuildImage();
}

bool SpectralView::recentGet(std::uint64_t windowId,
                             neuroscope::spectral::SpectralImage& out)
{
    for (auto it = recent_.begin(); it != recent_.end(); ++it) {
        if (it->first == windowId) {
            out = it->second;                       // copy out
            recent_.splice(recent_.begin(), recent_, it); // move to front (LRU)
            return true;
        }
    }
    return false;
}

void SpectralView::recentPut(std::uint64_t windowId,
                             const neuroscope::spectral::SpectralImage& img)
{
    for (auto it = recent_.begin(); it != recent_.end(); ++it)
        if (it->first == windowId) { recent_.erase(it); break; }
    recent_.emplace_front(windowId, img);
    while (recent_.size() > recentCap) recent_.pop_back();
}

neuroscope::spectral::SpectralParams SpectralView::previewParams() const
{
    // FrequencyAcrossChannels is band-pass + RMS - already cheap, so it has no
    // separate preview tier (preview == full, served straight from the cache).
    if (params.mode == neuroscope::spectral::SpectralMode::FrequencyAcrossChannels)
        return params;
    // Single-channel time-frequency: a one-taper preview at the full rate keeps
    // the frequency axis put while scrolling; the full multitaper follows.
    neuroscope::spectral::SpectralParams p = params;
    p.nTapers = 1;
    p.decimate = false;
    return p;
}

void SpectralView::settleNow()
{
    if (!movePreview) return;
    movePreview = false;
    if (!dataReady) return;

    // Launch the full multitaper on a worker thread; the preview stays on screen
    // until it returns. Stale results (a newer move bumped settleGen) are dropped
    // in onSettleComputed().
    std::vector<double> sampleMajor;
    std::vector<int> chans;
    int nSamples = 0, nCh = 0;
    if (!buildInput(sampleMajor, chans, nSamples, nCh)) return;

    neuroscope::spectral::SpectralParams full = params;
    full.samplingRate = tracesProvider.getSamplingRate();
    // The settle runs on a worker thread. GPU (cuFFT) work must stay on the UI
    // thread where the CUDA context is current, so force the worker to the CPU
    // backend; the GPU path is still used for the main-thread preview/compute.
    full.backend = neuroscope::spectral::SpectralBackend::Cpu;
    const std::uint64_t windowId =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(startTime)) << 32)
        ^ static_cast<std::uint32_t>(timeFrameWidth);

    pendingSettleGen = settleGen;
    pendingWindowId = windowId;
    if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
        qDebug() << "[spectral] settleNow launch gen=" << settleGen
                 << "nSamples=" << nSamples << "nCh=" << nCh;

    // Compute on a private engine (the member engine stays with the UI thread).
    // FFTW plan creation is mutex-guarded, so this is safe alongside the preview.
    auto future = QtConcurrent::run(
        [sm = std::move(sampleMajor), chans = std::move(chans), nSamples, nCh, full, windowId]() {
            neuroscope::spectral::SpectralEngine local;
            return local.compute(sm.data(), nSamples, nCh, chans, full, windowId);
        });
    mtWatcher->setFuture(future);
}

void SpectralView::onSettleComputed()
{
    if (!mtWatcher->future().isValid() || mtWatcher->future().resultCount() == 0)
        return;
    // Discard if a newer move started, or the window moved, since the launch.
    const std::uint64_t windowId =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(startTime)) << 32)
        ^ static_cast<std::uint32_t>(timeFrameWidth);
    const bool current = (pendingSettleGen == settleGen)
                      && (pendingWindowId == windowId) && !movePreview;
    neuroscope::spectral::SpectralImage res = mtWatcher->result();
    if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
        qDebug() << "[spectral] settle done current=" << current
                 << "result.valid=" << res.valid()
                 << "rows=" << res.rows << "cols=" << res.cols;
    if (!current) return;
    if (!res.valid()) return;   // a failed settle must not blank the shown preview

    lastImage = std::move(res);
    // Re-apply the current band selection to the freshly computed cube.
    neuroscope::spectral::integrateBand(lastImage, params.bandLo, params.bandHi);
    fullScaleMin = lastImage.valueMin; fullScaleMax = lastImage.valueMax;
    haveFullScale = true;
    recentPut(pendingWindowId, lastImage);   // settled full estimate is cacheable
    lastIsPreview = false;
    rebuildImage();
    update();
}

void SpectralView::rebuildImage()
{
    QImage img = neuroscope::spectral::spectralImageToQImage(lastImage, dynamicRangeDb,
                                                             colormap, autoScale);
    // Never replace a good frame with an empty render; if nothing valid has been
    // produced yet, image stays null and the view shows "computing...".
    if (!img.isNull() || image.isNull())
        image = img;
}

void SpectralView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    const QRect plot = plotRect();

    if (!image.isNull()) {
        // Only the rows inside the vertical zoom [yLo, yHi] are shown.
        const QRectF src(0.0, yLo * image.height(),
                         image.width(), (yHi - yLo) * image.height());
        painter.drawImage(QRectF(plot), image, src);
    } else {
        painter.setPen(Qt::gray);
        painter.drawText(plot, Qt::AlignCenter, tr("computing…"));
    }

    drawAxes(painter, plot);
}

QRect SpectralView::plotRect() const
{
    const int left = 52, right = 12, top = 18, bottom = 26;
    return QRect(left, top, std::max(1, width() - left - right),
                 std::max(1, height() - top - bottom));
}

void SpectralView::drawAxes(QPainter& painter, const QRect& plot)
{
    painter.setPen(QColor(180, 180, 180));
    painter.drawRect(plot);

    QFont f = painter.font();
    f.setPointSizeF(8.0);
    painter.setFont(f);

    // Title/axis type follow the image actually displayed (lastImage), not the
    // pending params, so a debounced or external repaint mid-change stays
    // consistent with the heatmap on screen.
    const bool modeB = lastImage.mode == SpectralMode::TimeFrequencySingleChannel;
    QString title = modeB ? tr("time \xC3\x97 frequency") : tr("band power \xC3\x97 channel");
    title += QString(" \xE2\x80\x94 NW=%1 K=%2 win=%3")
                 .arg(params.nw).arg(lastIsPreview ? 1 : params.nTapers).arg(params.windowSamples);
    if (lastIsPreview) title += tr("  (preview)");
    if (paramsDirty || windowDirty) title += tr("  \xE2\x80\x94 uncommitted changes");
    painter.drawText(plot.left(), plot.top() - 5, title);

    // X axis: time in ms across the window.
    painter.drawText(plot.left(), plot.bottom() + 16, QString::number(startTime));
    painter.drawText(plot.center().x() - 10, plot.bottom() + 16,
                     QString::number(startTime + timeFrameWidth / 2));
    painter.drawText(plot.right() - 30, plot.bottom() + 16, QString::number(endTime));

    // Y axis ticks + labels on the left edge: frequency (mode B) or channel
    // id (mode A). Low frequency at the bottom, channel 0 at the top.
    if (modeB && lastImage.freqs.size() >= 2) {
        // The visible part of the computed range (image row 0 is the top, fHi).
        const double fullLo = lastImage.freqs.front();
        const double fullHi = lastImage.freqs.back();
        const double fHi = fullHi - yLo * (fullHi - fullLo);
        const double fLo = fullHi - yHi * (fullHi - fullLo);
        if (fHi > fLo) {
            const double step = niceStep(fHi - fLo, 6);
            const int decimals = step < 1.0 ? 1 : 0;
            const double first = std::ceil(fLo / step) * step;
            for (double f = first; f <= fHi + step * 1e-6; f += step) {
                const int y = plot.top()
                    + static_cast<int>((fHi - f) / (fHi - fLo) * plot.height());
                painter.drawLine(plot.left() - 4, y, plot.left(), y);
                painter.drawText(QRect(0, y - 7, plot.left() - 6, 14),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(f, 'f', decimals));
            }
        }
        painter.drawText(2, plot.top() - 5, tr("Hz"));
    } else if (!modeB && !lastImage.rowChannels.empty()) {
        const int nch = static_cast<int>(lastImage.rowChannels.size());
        // Rows inside the vertical zoom, in image-row units.
        const double rTop = yLo * nch, rBottom = yHi * nch;
        const int stride = std::max(1, static_cast<int>((rBottom - rTop) / 16.0));
        for (int r = 0; r < nch; r += stride) {
            const double centre = r + 0.5;
            if (centre < rTop || centre > rBottom) continue;
            const int y = plot.top()
                + static_cast<int>((centre - rTop) / (rBottom - rTop) * plot.height());
            painter.drawLine(plot.left() - 4, y, plot.left(), y);
            painter.drawText(QRect(0, y - 7, plot.left() - 6, 14),
                             Qt::AlignRight | Qt::AlignVCenter,
                             QString::number(lastImage.rowChannels[r]));
        }
        painter.drawText(2, plot.top() - 5, tr("ch"));
    }
}

// --- inspector-driven setters ----------------------------------------------

void SpectralView::setSpectralMode(SpectralMode mode)
{
    pending.mode = mode;     // spectralModeChanged is emitted when it is committed
    scheduleParamUpdate();
}

void SpectralView::setWindowSamples(int n)
{
    if (n < 2) n = 2;
    pending.windowSamples = n;
    if (pending.nfft < n) pending.nfft = n;
    scheduleParamUpdate();
}

void SpectralView::setNfft(int n)
{
    if (n < pending.windowSamples) n = pending.windowSamples;
    pending.nfft = n;
    scheduleParamUpdate();
}

void SpectralView::setStep(int s)
{
    if (s < 1) s = 1;
    pending.stepSamples = s;
    scheduleParamUpdate();
}

void SpectralView::setTimeBandwidth(double nw)
{
    pending.nw = nw;
    scheduleParamUpdate();
}

void SpectralView::setNumTapers(int k)
{
    if (k < 1) k = 1;
    pending.nTapers = k;
    scheduleParamUpdate();
}

void SpectralView::setFrequencyRange(double lowHz, double highHz)
{
    pending.freqLow = lowHz;     // frequencyRangeChanged is emitted when committed
    pending.freqHigh = highHz;
    scheduleParamUpdate();
}

void SpectralView::setBand(double lowHz, double highHz)
{
    // Live, not staged: the slider gives immediate feedback on the shown image.
    params.bandLo = pending.bandLo = lowHz;
    params.bandHi = pending.bandHi = highHz;
    // The band is now the band-pass filter's pass-band, so re-filter rather than
    // re-integrate a cube. Filtering is cheap, so recompute immediately for live
    // slider feedback; cached estimates are band-specific, so drop them.
    if (lastImage.mode == neuroscope::spectral::SpectralMode::FrequencyAcrossChannels) {
        recent_.clear();
        if (dataReady) { recompute(); update(); }
    }
}

void SpectralView::setSingleChannelRow(int row)
{
    pending.singleChannel = row < 0 ? 0 : row;
    scheduleParamUpdate();
}

void SpectralView::setWhitening(bool on)
{
    pending.whiten = on;
    scheduleParamUpdate();
}

void SpectralView::setCar(bool on)
{
    pending.car = on;
    scheduleParamUpdate();
}

void SpectralView::setRefRegress(bool on)
{
    pending.refRegress = on;
    scheduleParamUpdate();
}

void SpectralView::setRefChannel(int row)
{
    pending.refChannel = row < 0 ? 0 : row;
    scheduleParamUpdate();
}

void SpectralView::setDecimate(bool on)
{
    pending.decimate = on;
    scheduleParamUpdate();
}

void SpectralView::setColormap(Colormap cm)
{
    colormap = cm;
    rebuildImage();   // colormap only affects rendering, not the computation
    update();
}

void SpectralView::setDynamicRangeDb(double db)
{
    dynamicRangeDb = db;
    rebuildImage();   // dB range only affects rendering
    update();
}

void SpectralView::setAutoScale(bool on)
{
    autoScale = on;
    rebuildImage();   // scaling only affects rendering
    update();
}

void SpectralView::setBackend(SpectralBackend backend)
{
    pending.backend = backend;
    scheduleParamUpdate();
}

void SpectralView::setLockToTrace(bool on)
{
    pendingLockToTrace = on;
    scheduleWindowUpdate();   // re-derive the window after the debounce / on "u"
}

void SpectralView::setSpan(long ms)
{
    pendingSpan = ms;
    if (!pendingLockToTrace)
        scheduleWindowUpdate();
}

// --- mouse navigation -------------------------------------------------------
//
// The same gestures as the Klusters trace view: Ctrl+Left drag pans, Ctrl+wheel
// zooms toward the cursor.  The plain wheel zooms time only.  The horizontal axis
// is the shared trace window, so a time pan or zoom moves it (through
// timeWindowRequested), the trace view and scroll bar follow, and the spectral
// image recomputes through the usual preview/settle path.  The vertical axis --
// frequency, or channel rows -- is a crop of the computed image and costs nothing.

void SpectralView::registerInput(input::BindingRegistry& reg)
{
    reg.addScope({ QStringLiteral("view.spectral"), input::Layer::ViewType,
        [](const input::Ctx& c){ return qobject_cast<SpectralView*>(c.view) != nullptr; } });

    auto view = [](const input::Ctx& c){ return qobject_cast<SpectralView*>(c.view); };

    input::Command pan;
    pan.id       = QStringLiteral("spectral.pan");
    pan.scopeId  = QStringLiteral("view.spectral");
    pan.label    = tr("Pan the spectral view");
    pan.category = tr("Spectral view");
    pan.kind     = input::Kind::Gesture;
    pan.defaultChord = input::Chord::button(Qt::LeftButton, Qt::ControlModifier,
                                            input::Phase::Press, input::ModMatch::AtLeast);
    pan.invoke   = [view](const input::Ctx& c){
        if (auto* sv = view(c); sv && c.event)
            sv->beginPan(static_cast<QMouseEvent*>(c.event)->position().toPoint());
    };
    reg.addCommand(pan);

    // Ctrl+wheel (AtLeast, so registered before the plain-wheel pair, which is
    // Exact and could not match a Ctrl event anyway).
    auto addZoom = [&](const QString& id, const QString& label, int dir,
                       Qt::KeyboardModifiers mods, input::ModMatch mm, bool vertical){
        input::Command c;
        c.id       = id;
        c.scopeId  = QStringLiteral("view.spectral");
        c.label    = label;
        c.category = tr("Spectral view");
        c.kind     = input::Kind::Action;
        c.defaultChord = input::Chord::wheel(dir, mods, mm);
        c.invoke   = [view, dir, vertical](const input::Ctx& ctx){
            if (auto* sv = view(ctx); sv && ctx.event)
                sv->zoomAtCursor(dir > 0,
                                 static_cast<QWheelEvent*>(ctx.event)->position().toPoint(),
                                 vertical);
        };
        reg.addCommand(c);
    };
    addZoom(QStringLiteral("spectral.zoomIn"),  tr("Zoom in toward cursor"), +1,
            Qt::ControlModifier, input::ModMatch::AtLeast, true);
    addZoom(QStringLiteral("spectral.zoomOut"), tr("Zoom out from cursor"),  -1,
            Qt::ControlModifier, input::ModMatch::AtLeast, true);
    addZoom(QStringLiteral("spectral.timeZoomIn"),  tr("Zoom in on time"),  +1,
            Qt::NoModifier, input::ModMatch::Exact, false);
    addZoom(QStringLiteral("spectral.timeZoomOut"), tr("Zoom out on time"), -1,
            Qt::NoModifier, input::ModMatch::Exact, false);
}

void SpectralView::wheelEvent(QWheelEvent* event)
{
    if (dispatchInput(event)) return;
    BaseFrame::wheelEvent(event);
}

void SpectralView::beginPan(const QPoint& pos)
{
    // Arm the pan; mouseMoveEvent crosses the drag threshold and moves the view,
    // mouseReleaseEvent disarms.  Everything is measured from the press, so the
    // point grabbed stays under the cursor.
    panArmed           = true;
    panning            = false;
    panAnchorPx        = pos;
    panPressTraceStart = traceStart;
    panPressWidth      = timeFrameWidth;
    panPressYLo        = yLo;
    panPressYHi        = yHi;
    setCursor(Qt::ClosedHandCursor);
}

void SpectralView::mouseMoveEvent(QMouseEvent* event)
{
    if (panArmed && (event->buttons() & Qt::LeftButton)) {
        const QPoint d = event->position().toPoint() - panAnchorPx;
        if (!panning && std::abs(d.x()) + std::abs(d.y()) >= panDragThreshold)
            panning = true;
        if (panning) {
            const QRect plot = plotRect();
            // Vertical: slide the crop, keeping its height.
            const double h = panPressYHi - panPressYLo;
            if (h < 1.0) {
                double lo = panPressYLo - static_cast<double>(d.y()) / plot.height() * h;
                lo = std::clamp(lo, 0.0, 1.0 - h);
                if (lo != yLo) { yLo = lo; yHi = lo + h; update(); }
            }
            // Horizontal: move the shared time window by the dragged fraction of
            // the window shown at the press.
            const double dtMs = static_cast<double>(d.x()) / plot.width() * panPressWidth;
            requestTraceStart(panPressTraceStart - std::lround(dtMs), traceWidth);
        }
        event->accept();
        return;
    }
    BaseFrame::mouseMoveEvent(event);
}

void SpectralView::mouseReleaseEvent(QMouseEvent* event)
{
    if (panArmed && event->button() == Qt::LeftButton) {
        panArmed = false;
        panning  = false;
        unsetCursor();
        event->accept();
        return;
    }
    BaseFrame::mouseReleaseEvent(event);
}

void SpectralView::zoomAtCursor(bool zoomIn, const QPoint& pos, bool vertical)
{
    const QRect plot = plotRect();
    const double factor = zoomIn ? wheelZoomStep : 1.0 / wheelZoomStep;
    const double fx = std::clamp(static_cast<double>(pos.x() - plot.left()) / plot.width(), 0.0, 1.0);
    const double fy = std::clamp(static_cast<double>(pos.y() - plot.top()) / plot.height(), 0.0, 1.0);
    if (vertical) zoomRows(factor, fy);
    zoomTime(factor, fx);
}

void SpectralView::zoomRows(double factor, double fy)
{
    // Keep the row under the cursor at the same screen height.
    const double h  = yHi - yLo;
    const double at = yLo + fy * h;
    const double nh = std::clamp(h / factor, 1.0 / 64.0, 1.0);
    if (nh == h) return;
    const double lo = std::clamp(at - fy * nh, 0.0, 1.0 - nh);
    yLo = lo;
    yHi = lo + nh;
    update();
}

long SpectralView::minimumSpanMs() const
{
    // Narrower than two analysis windows leaves no spectrogram to show.
    const double fs = tracesProvider.getSamplingRate();
    if (fs <= 0.0) return 1;
    const double ms = 2.0 * params.windowSamples * 1000.0 / fs;
    return std::max(1L, static_cast<long>(std::ceil(ms)));
}

void SpectralView::zoomTime(double factor, double fx)
{
    // Keep the time under the cursor at the same screen fraction.
    if (timeFrameWidth <= 0) return;
    const long maxW = recordingLength > 0 ? recordingLength : timeFrameWidth;
    const long w = std::clamp(std::lround(timeFrameWidth / factor),
                              std::min(minimumSpanMs(), maxW), maxW);
    if (w == timeFrameWidth) return;
    const double at = startTime + fx * timeFrameWidth;
    long start = std::lround(at - fx * w);
    start = std::clamp(start, 0L, std::max(0L, maxW - w));

    if (lockToTrace) {
        requestTraceStart(start, w);          // the trace window is the spectral window
    } else {
        // Unlocked: the view has its own span, centred on the trace window.  Set
        // the span directly (a mouse zoom is immediate, not staged) and recentre
        // the trace window on the new spectral centre.
        span = pendingSpan = w;
        const long centre = start + w / 2;
        if (traceStart + traceWidth / 2 == centre) displayTimeFrame(traceStart, traceWidth);
        else requestTraceStart(centre - traceWidth / 2, traceWidth);
    }
}

void SpectralView::requestTraceStart(long start, long duration)
{
    const long len = recordingLength > 0 ? recordingLength : start + duration;
    start = std::clamp(start, 0L, std::max(0L, len - duration));
    if (start == traceStart && duration == traceWidth) return;
    emit timeWindowRequested(start, duration);
}
