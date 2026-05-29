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
    void setNbUndo(int n);

    bool isCrashRecovery() const;
    int  crashRecoveryIntervalIndex() const;
    int  getNbUndo() const;

private slots:
    void updateCrashRecoveryTimeInterval(int state);
};

#endif
