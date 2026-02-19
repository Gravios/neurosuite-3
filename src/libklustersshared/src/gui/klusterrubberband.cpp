#include "klusterrubberband.h"
#include <QPainter>
#include <QPaintEvent>

KlusterRubberBand::KlusterRubberBand(QRubberBand::Shape s, QWidget *w)
    : QRubberBand(s, w)
{
}

KlusterRubberBand::~KlusterRubberBand()
{

}

void KlusterRubberBand::paintEvent( QPaintEvent *p )
{
    QPainter painter( this );
    QRect rect = p->rect();

    QPen pen( Qt::white, 1 ); // blue solid line, 1 pixels wide
    painter.setPen( pen );
    painter.setOpacity( 0.9 );
    painter.drawRect( rect.adjusted(0,0,-1,-1) );
}
