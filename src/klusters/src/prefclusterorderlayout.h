#ifndef PREFCLUSTERORDERLAYOUT_H
#define PREFCLUSTERORDERLAYOUT_H

#include "ui_prefclusterorderlayout.h"

class PrefClusterOrderLayout : public QWidget, public Ui_PrefClusterOrderLayout
{
    Q_OBJECT
public:
    explicit PrefClusterOrderLayout(QWidget* parent = nullptr);
};

#endif
