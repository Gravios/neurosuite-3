/*
Copyright (C) 2012 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
*/

#include "qcolorbutton.h"

#include <QColor>
#include <QColorDialog>

class QColorButtonPrivate
{
public:
    QColorButtonPrivate(QColorButton *qq)
        :q(qq)
    {
    }
    void slotSelectColor();
    void updateBackgroundColor();
    QColor color;
    QColorButton *q;
};


void QColorButtonPrivate::updateBackgroundColor()
{
    QPalette pal = q->palette();
    pal.setColor(QPalette::Button, color);
    q->setPalette(pal);
    q->update();
}


QColorButton::QColorButton(QWidget *parent)
    :QPushButton(parent),d(new QColorButtonPrivate(this))
{
    connect(this, &QPushButton::clicked, this, &QColorButton::slotSelectColor);
}

QColorButton::~QColorButton()
{
    delete d;
}


void QColorButton::setColor(const QColor&col)
{
    if(col != d->color) {
        d->color = col;
        d->updateBackgroundColor();
        Q_EMIT colorChanged(col);
    }
}

QColor QColorButton::color() const
{
    return d->color;
}

void QColorButton::slotSelectColor()
{
    const QColor col = QColorDialog::getColor ( d->color, this );
    if(col.isValid()) {
        setColor(col);
    }
}
