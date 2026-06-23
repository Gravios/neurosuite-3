#include "spectralview.h"

#include <QPainter>
#include <QPaintEvent>
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
    QSettings s;
    s.beginGroup(kSpectralGroup);
    s.setValue("mode", static_cast<int>(params.mode));
    s.setValue("singleChannel", params.singleChannel);
    s.setValue("nw", params.nw);
    s.setValue("nTapers", params.nTapers);
    s.setValue("windowSamples", params.windowSamples);
    s.setValue("nfft", params.nfft);
    s.setValue("stepSamples", params.stepSamples);
    s.setValue("freqLow", params.freqLow);
    s.setValue("freqHigh", params.freqHigh);
    s.setValue("whiten", params.whiten);
    s.setValue("decimate", params.decimate);
    s.setValue("bandLo", params.bandLo);
    s.setValue("bandHi", params.bandHi);
    s.setValue("colormap", static_cast<int>(colormap));
    s.setValue("autoScale", autoScale);
    s.setValue("dynamicRangeDb", dynamicRangeDb);
    s.setValue("lockToTrace", lockToTrace);
    s.setValue("span", static_cast<qlonglong>(span));
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
    // A moving window renders the fast preview; the settle timer restarts on
    // every move and triggers the full multitaper once it stops (~200 ms idle).
    // FrequencyAcrossChannels needs no settle - its single compute is the full
    // estimate - so skip the redundant background pass there.
    movePreview = true;
    ++settleGen;          // invalidates any in-flight background multitaper pass
    if (params.mode != neuroscope::spectral::SpectralMode::FrequencyAcrossChannels)
        settleTimer->start(200);
    applyWindow();
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

void SpectralView::scheduleParamUpdate()
{
    engine.invalidate();
    paramsDirty = true;
    ++settleGen;   // a parameter change supersedes any in-flight background pass
    recent_.clear();   // cached estimates are for the old parameters
    if (recomputeDelayMs > 0) recomputeTimer->start(recomputeDelayMs);
    // else: manual mode — the change waits for commitNow() ("u").
}

void SpectralView::scheduleWindowUpdate()
{
    windowDirty = true;
    ++settleGen;
    recent_.clear();   // window width may change, invalidating cached window ids
    if (recomputeDelayMs > 0) recomputeTimer->start(recomputeDelayMs);
    // else: manual mode — the change waits for commitNow() ("u").
}

void SpectralView::flushPending()
{
    recomputeTimer->stop();
    if (windowDirty) {
        windowDirty = false;
        paramsDirty = false;
        applyWindow();              // re-derives the window; recompute on dataReady
    } else if (paramsDirty) {
        paramsDirty = false;
        if (dataReady) { recompute(); update(); }
    }
}

void SpectralView::commitNow()
{
    flushPending();
}

void SpectralView::setChannels(const QList<int>& chans)
{
    channels = chans;
    scheduleParamUpdate();
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

    // The fresh render already used the current parameters, so cancel any
    // pending debounced recompute.
    paramsDirty = false;
    windowDirty = false;
    if (recomputeTimer) recomputeTimer->stop();
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

    const int left = 52, right = 12, top = 18, bottom = 26;
    QRect plot(left, top, std::max(1, width() - left - right),
               std::max(1, height() - top - bottom));

    if (!image.isNull()) {
        painter.drawImage(plot, image);
    } else {
        painter.setPen(Qt::gray);
        painter.drawText(plot, Qt::AlignCenter, tr("computing..."));
    }

    drawAxes(painter, plot);
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
                 .arg(params.nw).arg(params.nTapers).arg(params.windowSamples);
    painter.drawText(plot.left(), plot.top() - 5, title);

    // X axis: time in ms across the window.
    painter.drawText(plot.left(), plot.bottom() + 16, QString::number(startTime));
    painter.drawText(plot.center().x() - 10, plot.bottom() + 16,
                     QString::number(startTime + timeFrameWidth / 2));
    painter.drawText(plot.right() - 30, plot.bottom() + 16, QString::number(endTime));

    // Y axis ticks + labels on the left edge: frequency (mode B) or channel
    // id (mode A). Low frequency at the bottom, channel 0 at the top.
    if (modeB && lastImage.freqs.size() >= 2) {
        const double fLo = lastImage.freqs.front();
        const double fHi = lastImage.freqs.back();
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
        const int stride = std::max(1, nch / 16);   // cap label count
        for (int r = 0; r < nch; r += stride) {
            const int y = plot.top()
                + static_cast<int>((r + 0.5) / nch * plot.height());
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
    params.mode = mode;
    emit spectralModeChanged(mode);
    scheduleParamUpdate();
}

void SpectralView::setWindowSamples(int n)
{
    if (n < 2) n = 2;
    params.windowSamples = n;
    if (params.nfft < n) params.nfft = n;
    scheduleParamUpdate();
}

void SpectralView::setNfft(int n)
{
    if (n < params.windowSamples) n = params.windowSamples;
    params.nfft = n;
    scheduleParamUpdate();
}

void SpectralView::setStep(int s)
{
    if (s < 1) s = 1;
    params.stepSamples = s;
    scheduleParamUpdate();
}

void SpectralView::setTimeBandwidth(double nw)
{
    params.nw = nw;
    scheduleParamUpdate();
}

void SpectralView::setNumTapers(int k)
{
    if (k < 1) k = 1;
    params.nTapers = k;
    scheduleParamUpdate();
}

void SpectralView::setFrequencyRange(double lowHz, double highHz)
{
    params.freqLow = lowHz;
    params.freqHigh = highHz;
    emit frequencyRangeChanged(lowHz, highHz);
    scheduleParamUpdate();
}

void SpectralView::setBand(double lowHz, double highHz)
{
    params.bandLo = lowHz;
    params.bandHi = highHz;
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
    params.singleChannel = row < 0 ? 0 : row;
    scheduleParamUpdate();
}

void SpectralView::setWhitening(bool on)
{
    params.whiten = on;
    scheduleParamUpdate();
}

void SpectralView::setDecimate(bool on)
{
    params.decimate = on;
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
    params.backend = backend;
    scheduleParamUpdate();
}

void SpectralView::setLockToTrace(bool on)
{
    lockToTrace = on;
    scheduleWindowUpdate();   // re-derive the window after the debounce / on "u"
}

void SpectralView::setSpan(long ms)
{
    span = ms;
    if (!lockToTrace)
        scheduleWindowUpdate();
}
