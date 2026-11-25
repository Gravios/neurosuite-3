#ifndef PREFMATRICESLAYOUT_H
#define PREFMATRICESLAYOUT_H

#include "ui_prefmatriceslayout.h"

class PrefMatricesLayout : public QWidget, public Ui_PrefMatricesLayout
{
    Q_OBJECT
public:
    explicit PrefMatricesLayout(QWidget* parent = nullptr);
};

#endif
