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

#include <QImage>
#include <QList>

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
    void setSingleChannelRow(int row);
    void setWhitening(bool on);
    void setColormap(neuroscope::spectral::Colormap cm);
    void setDynamicRangeDb(double db);
    void setBackend(neuroscope::spectral::SpectralBackend backend);

    const neuroscope::spectral::SpectralParams& spectralParams() const { return params; }

public Q_SLOTS:
    void dataAvailable(Array<dataType>& data, QObject* initiator);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void requestCurrentWindow();
    void recompute();          // run the engine on the current window, rebuild image
    void rebuildImage();       // SpectralImage -> QImage
    void drawAxes(QPainter& painter, const QRect& plot);

    TracesProvider& tracesProvider;
    QList<int> channels;

    long startTime = 0;
    long endTime = 0;
    long timeFrameWidth = 0;
    long startTimeInRecordingUnits = 0;

    Array<dataType> data;
    bool dataReady = false;

    neuroscope::spectral::SpectralEngine engine;
    neuroscope::spectral::SpectralParams params;
    neuroscope::spectral::SpectralImage lastImage;
    neuroscope::spectral::Colormap colormap = neuroscope::spectral::Colormap::Viridis;
    double dynamicRangeDb = 60.0;

    QImage image;
};

#endif // NEUROSCOPE_SPECTRALVIEW_H
