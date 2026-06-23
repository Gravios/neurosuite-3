#ifndef NEUROSCOPE_FREQBANDSLIDER_H
#define NEUROSCOPE_FREQBANDSLIDER_H

// A thin vertical range slider shown to the left of the spectral view in
// channel (band-power) mode. Its full extent is the displayed frequency range
// [rangeLo, rangeHi]; the user drags the two handles (or the band between them)
// to pick the integration sub-band [bandLo, bandHi], which is what the spectral
// power is integrated over for each channel. High frequencies are at the top.

#include <QWidget>

class FreqBandSlider : public QWidget {
    Q_OBJECT
public:
    explicit FreqBandSlider(QWidget* parent = nullptr);

    double bandLow() const { return bandLo; }
    double bandHigh() const { return bandHi; }

public Q_SLOTS:
    /// Set the slider's full extent (Hz). The current band is clamped into it.
    void setRange(double lowHz, double highHz);
    /// Set the selected band (Hz) without emitting bandChanged.
    void setBand(double lowHz, double highHz);

Q_SIGNALS:
    /// Emitted while the user drags a handle or the band.
    void bandChanged(double lowHz, double highHz);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    enum class Drag { None, Low, High, Band };

    QRect  trackRect() const;          // the vertical track area
    double hzForY(int y) const;        // pixel -> Hz (clamped to [rangeLo,rangeHi])
    int    yForHz(double hz) const;    // Hz -> pixel
    void   clampBand();
    void   emitBand();

    double rangeLo = 0.0;
    double rangeHi = 100.0;
    double bandLo  = 0.0;
    double bandHi  = 100.0;

    Drag   drag = Drag::None;
    double dragGrabOffset = 0.0;       // Hz offset for Band drags
};

#endif // NEUROSCOPE_FREQBANDSLIDER_H
