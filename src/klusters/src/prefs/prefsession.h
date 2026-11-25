#ifndef PREFSESSION_H
#define PREFSESSION_H

#include "prefsessionlayout.h"

class PrefSession : public PrefSessionLayout
{
    Q_OBJECT
public:
    explicit PrefSession(QWidget* parent = nullptr);
    ~PrefSession() override = default;



    // Saving and the curation log.

private slots:
    void updateCrashRecoveryTimeInterval(int state);
};

#endif
