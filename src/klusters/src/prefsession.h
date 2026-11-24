#ifndef PREFSESSION_H
#define PREFSESSION_H

#include "prefsessionlayout.h"

class PrefSession : public PrefSessionLayout
{
    Q_OBJECT
public:
    explicit PrefSession(QWidget* parent = nullptr);
    ~PrefSession() override = default;

    void setCrashRecovery(bool use);
    void setCrashRecoveryIndex(int idx);

    bool isCrashRecovery() const;
    int  crashRecoveryIntervalIndex() const;

    // Saving and the curation log.

private slots:
    void updateCrashRecoveryTimeInterval(int state);
};

#endif
