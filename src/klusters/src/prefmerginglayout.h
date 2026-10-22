#ifndef PREFMERGINGLAYOUT_H
#define PREFMERGINGLAYOUT_H

#include "ui_prefmerginglayout.h"

class PrefMergingLayout : public QWidget, public Ui_PrefMergingLayout
{
    Q_OBJECT
public:
    explicit PrefMergingLayout(QWidget* parent = nullptr);
};

#endif
