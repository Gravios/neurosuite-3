#ifndef PROBELAYOUT_H
#define PROBELAYOUT_H

#include "ui_probelayout.h"

class ProbeLayout : public QWidget, public Ui_ProbeLayout
{
    Q_OBJECT
public:
    explicit ProbeLayout(QWidget* parent = nullptr);
};

#endif // PROBELAYOUT_H
