#ifndef PREFSORTINGLAYOUT_H
#define PREFSORTINGLAYOUT_H

#include "ui_prefsortinglayout.h"

class PrefSortingLayout : public QWidget, public Ui_PrefSortingLayout
{
    Q_OBJECT
public:
    explicit PrefSortingLayout(QWidget* parent = nullptr);
};

#endif
