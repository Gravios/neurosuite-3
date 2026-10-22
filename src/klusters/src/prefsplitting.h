#ifndef PREFSPLITTING_H
#define PREFSPLITTING_H

#include "prefsplittinglayout.h"

// Preferences ▸ Curation ▸ Splitting: DipSplit and the KNN voting split.
class PrefSplitting : public PrefSplittingLayout
{
    Q_OBJECT
public:
    explicit PrefSplitting(QWidget* parent = nullptr);
    ~PrefSplitting() override = default;

    void   setDipSplitMinSize(int n);
    void   setDipSplitBloatFactor(double v);
    void   setDipSplitValleyThresh(double v);
    int    getDipSplitMinSize()       const;
    double getDipSplitBloatFactor()   const;
    double getDipSplitValleyThresh()  const;
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
