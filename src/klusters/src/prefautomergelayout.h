#ifndef PREFAUTOMERGELAYOUT_H
#define PREFAUTOMERGELAYOUT_H

#include "ui_prefautomergelayout.h"

class PrefAutoMergeLayout : public QWidget, public Ui_PrefAutoMergeLayout
{
    Q_OBJECT
public:
    explicit PrefAutoMergeLayout(QWidget* parent = nullptr);
};

#endif
