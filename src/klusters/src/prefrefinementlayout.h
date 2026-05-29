#ifndef PREFREFINEMENTLAYOUT_H
#define PREFREFINEMENTLAYOUT_H

#include "ui_prefrefinementlayout.h"

class PrefRefinementLayout : public QWidget, public Ui_PrefRefinementLayout
{
    Q_OBJECT
public:
    explicit PrefRefinementLayout(QWidget* parent = nullptr);
};

#endif
