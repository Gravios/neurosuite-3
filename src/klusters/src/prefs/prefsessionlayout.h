#ifndef PREFSESSIONLAYOUT_H
#define PREFSESSIONLAYOUT_H

#include "ui_prefsessionlayout.h"

class PrefSessionLayout : public QWidget, public Ui_PrefSessionLayout
{
    Q_OBJECT
public:
    explicit PrefSessionLayout(QWidget* parent = nullptr);
};

#endif
