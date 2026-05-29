#ifndef PREFDISPLAYLAYOUT_H
#define PREFDISPLAYLAYOUT_H

#include "ui_prefdisplaylayout.h"

class PrefDisplayLayout : public QWidget, public Ui_PrefDisplayLayout
{
    Q_OBJECT
public:
    explicit PrefDisplayLayout(QWidget* parent = nullptr);
};

#endif
