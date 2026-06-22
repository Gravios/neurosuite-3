#include "spectralview.h"

#include <QPainter>
#include <QPaintEvent>

#include <cstdint>
#include <vector>

using neuroscope::spectral::SpectralMode;
using neuroscope::spectral::SpectralParams;
using neuroscope::spectral::SpectralImage;
using neuroscope::spectral::Colormap;
using neuroscope::spectral::SpectralBackend;

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

    requestCurrentWindow();
}

SpectralView::~SpectralView() = default;

void SpectralView::requestCurrentWindow()
{
    dataReady = false;
    tracesProvider.requestData(startTime, endTime, this, startTimeInRecordingUnits);
}

void SpectralView::displayTimeFrame(long start, long width)
{
    startTime = start;
    endTime = start + width;
    timeFrameWidth = width;
    requestCurrentWindow();
}

void SpectralView::setChannels(const QList<int>& chans)
{
    channels = chans;
    engine.invalidate();
    if (dataReady) recompute();
    update();
}

void SpectralView::dataAvailable(Array<dataType>& incoming, QObject* initiator)
{
    if (initiator != this) return;       // not our request
    if (incoming.nbOfRows() == 0) return; // I/O error upstream

    data = incoming;
    dataReady = true;
    recompute();
    update();
}

void SpectralView::recompute()
{
    const int nSamples = static_cast<int>(data.nbOfRows());
    const int nCh = static_cast<int>(data.nbOfColumns());
    if (nSamples <= 0 || nCh <= 0) { image = QImage(); return; }

    // Array is 1-based (sample i, channel j); flatten to sample-major double.
    std::vector<double> sampleMajor(static_cast<std::size_t>(nSamples) * nCh);
    for (int s = 0; s < nSamples; ++s)
        for (int c = 0; c < nCh; ++c)
            sampleMajor[static_cast<std::size_t>(s) * nCh + c] =
                static_cast<double>(data(s + 1, c + 1));

    std::vector<int> chans;
    chans.reserve(channels.size());
    for (int c : channels)
        if (c >= 0 && c < nCh) chans.push_back(c);
    if (chans.empty()) { image = QImage(); return; }

    params.samplingRate = tracesProvider.getSamplingRate();
    const std::uint64_t windowId =
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(startTime)) << 32)
        ^ static_cast<std::uint32_t>(timeFrameWidth);

    lastImage = engine.compute(sampleMajor.data(), nSamples, nCh, chans, params, windowId);
    rebuildImage();
}

void SpectralView::rebuildImage()
{
    image = neuroscope::spectral::spectralImageToQImage(lastImage, dynamicRangeDb, colormap);
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

    // Title: mode and key parameters.
    const bool modeB = params.mode == SpectralMode::TimeFrequencySingleChannel;
    QString title = modeB ? tr("time \xC3\x97 frequency") : tr("band power \xC3\x97 channel");
    title += QString(" \xE2\x80\x94 NW=%1 K=%2 win=%3")
                 .arg(params.nw).arg(params.nTapers).arg(params.windowSamples);
    painter.drawText(plot.left(), plot.top() - 5, title);

    // X axis: time in ms across the window.
    painter.drawText(plot.left(), plot.bottom() + 16, QString::number(startTime));
    painter.drawText(plot.center().x() - 10, plot.bottom() + 16,
                     QString::number(startTime + timeFrameWidth / 2));
    painter.drawText(plot.right() - 30, plot.bottom() + 16, QString::number(endTime));

    // Y axis: frequency (mode B) or channel id (mode A); low at bottom / top.
    if (modeB && !lastImage.freqs.empty()) {
        const double fLo = lastImage.freqs.front();
        const double fHi = lastImage.freqs.back();
        painter.drawText(4, plot.top() + 8, QString::number(fHi, 'f', 0));     // top = high
        painter.drawText(4, plot.bottom(), QString::number(fLo, 'f', 0));      // bottom = low
        painter.drawText(2, plot.center().y(), tr("Hz"));
    } else if (!modeB && !lastImage.rowChannels.empty()) {
        painter.drawText(4, plot.top() + 8, QString::number(lastImage.rowChannels.front()));
        painter.drawText(4, plot.bottom(), QString::number(lastImage.rowChannels.back()));
        painter.drawText(2, plot.center().y(), tr("ch"));
    }
}

// --- inspector-driven setters ----------------------------------------------

void SpectralView::setSpectralMode(SpectralMode mode)
{
    params.mode = mode;
    engine.invalidate();
    if (dataReady) recompute();
    update();
}

void SpectralView::setWindowSamples(int n)
{
    if (n < 2) n = 2;
    params.windowSamples = n;
    if (params.nfft < n) params.nfft = n;
    engine.invalidate();
    if (dataReady) recompute();
    update();
}

void SpectralView::setNfft(int n)
{
    if (n < params.windowSamples) n = params.windowSamples;
    params.nfft = n;
    engine.invalidate();
    if (dataReady) recompute();
    update();
}

void SpectralView::setStep(int s)
{
    if (s < 1) s = 1;
    params.stepSamples = s;
    engine.invalidate();
    if (dataReady) recompute();
    update();
}

void SpectralView::setTimeBandwidth(double nw)
{
    params.nw = nw;
    engine.invalidate();
    if (dataReady) recompute();
    update();
}

void SpectralView::setNumTapers(int k)
{
    if (k < 1) k = 1;
    params.nTapers = k;
    engine.invalidate();
    if (dataReady) recompute();
    update();
}

void SpectralView::setFrequencyRange(double lowHz, double highHz)
{
    params.freqLow = lowHz;
    params.freqHigh = highHz;
    engine.invalidate();
    if (dataReady) recompute();
    update();
}

void SpectralView::setSingleChannelRow(int row)
{
    params.singleChannel = row < 0 ? 0 : row;
    engine.invalidate();
    if (dataReady) recompute();
    update();
}

void SpectralView::setWhitening(bool on)
{
    params.whiten = on;
    engine.invalidate();
    if (dataReady) recompute();
    update();
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

void SpectralView::setBackend(SpectralBackend backend)
{
    params.backend = backend;
    engine.invalidate();
    if (dataReady) recompute();
    update();
}
