#ifndef KLUSTERRUBBERBAND_H
#define KLUSTERRUBBERBAND_H

#include <QRubberBand>
#include "libklustersshared_export.h"

class KLUSTERSSHARED_EXPORT KlusterRubberBand : public QRubberBand
{
    Q_OBJECT
public:
    explicit KlusterRubberBand(QRubberBand::Shape s, QWidget *w=nullptr);
    ~KlusterRubberBand();

protected:
    void paintEvent( QPaintEvent *p );
};

#endif // KLUSTERRUBBERBAND_H
