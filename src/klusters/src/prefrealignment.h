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
    // Post-alignment mode: 0 = off, 1 = PCA refine, 2 = RMS recenter
    void   setRealignMode(int m);
    int    getRealignMode()       const;

    void   setSupersampleMode(int m);       // 0 off, 2, 4
    int    getSupersampleMode() const;

    // Auto-run spike alignment after each interactive merge

};

#endif
