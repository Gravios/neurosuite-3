#ifndef PREFREFINEMENT_H
#define PREFREFINEMENT_H

#include "prefrefinementlayout.h"

class PrefRefinement : public PrefRefinementLayout
{
    Q_OBJECT
public:
    explicit PrefRefinement(QWidget* parent = nullptr);
    ~PrefRefinement() override = default;

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

    // Curation logging (per-action audit snapshots)
    void   setCurationLogging(bool b);
    bool   getCurationLogging()   const;
    void   setRealignVerbose(bool b);
    bool   getRealignVerbose()    const;
    void   setReextractSpikesOnSave(bool b);
    bool   getReextractSpikesOnSave() const;

    // Auto-run spike alignment after each interactive merge
    void   setAutoRealignAfterMerge(bool b);
    bool   getAutoRealignAfterMerge() const;
    void   setAutoRenumberAfterMerge(bool b);
    bool   getAutoRenumberAfterMerge() const;
    void   setAutoUpdateMatricesAfterMerge(bool b);
    bool   getAutoUpdateMatricesAfterMerge() const;
    void   setErrorMatrixIncremental(bool b);
    bool   getErrorMatrixIncremental() const;
    void   setErrorMatrixLowPrecision(bool b);
    bool   getErrorMatrixLowPrecision() const;

    // DipSplit
    void   setDipSplitMinSize(int n);
    void   setDipSplitBloatFactor(double v);
    void   setDipSplitValleyThresh(double v);
    int    getDipSplitMinSize()       const;
    double getDipSplitBloatFactor()   const;
    double getDipSplitValleyThresh()  const;

    // KNN-split
    void   setKnnK(int n);
    void   setKnnThreshold(double v);
    void   setKnnMinNew(int n);
    void   setKnnMinRef(int n);
    int    getKnnK()         const;
    double getKnnThreshold() const;
    int    getKnnMinNew()    const;
    int    getKnnMinRef()    const;
};

#endif
