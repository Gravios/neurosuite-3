#ifndef PREFREALIGNMENT_H
#define PREFREALIGNMENT_H

#include "prefrealignmentlayout.h"

class PrefRealignment : public PrefRealignmentLayout
{
    Q_OBJECT
public:
    explicit PrefRealignment(QWidget* parent = nullptr);
    ~PrefRealignment() override = default;

    // Realignment (built-in xcorr)
    void   setRealignThreshold(double v);
    void   setRealignIterations(int n);
    void   setRealignMaxShift(int n);
    double getRealignThreshold()  const;
    int    getRealignIterations() const;
    int    getRealignMaxShift()   const;
    // Post-alignment mode: 0 = off, 1 = PCA refine, 2 = RMS recenter
    void   setRealignMode(int m);
    int    getRealignMode()       const;

    void   setRealignVerbose(bool b);
    bool   getRealignVerbose()    const;
    void   setSyncFeatureSpaceOnSwitch(bool b);
    bool   getSyncFeatureSpaceOnSwitch() const;
    void   setRealignFromAnySpace(bool b);
    bool   getRealignFromAnySpace() const;

    // Auto-run spike alignment after each interactive merge
    void   setAutoRealignAfterMerge(bool b);
    bool   getAutoRealignAfterMerge() const;

};

#endif
