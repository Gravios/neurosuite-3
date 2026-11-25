#ifndef PREFSPLITTINGLAYOUT_H
#define PREFSPLITTINGLAYOUT_H

#include "ui_prefsplittinglayout.h"

class PrefSplittingLayout : public QWidget, public Ui_PrefSplittingLayout
{
    Q_OBJECT
public:
    explicit PrefSplittingLayout(QWidget* parent = nullptr);
};

#endif
