#ifndef PREFSORTING_H
#define PREFSORTING_H

#include "prefsortinglayout.h"

// Cluster-sorting preferences (Reorder Clusters by Similarity, Shift+S).
// Split out of PrefRefinement so sorting has its own Preferences tab.
class PrefSorting : public PrefSortingLayout
{
    Q_OBJECT
public:
    explicit PrefSorting(QWidget* parent = nullptr);
    ~PrefSorting() override = default;

    void setReorderMethod(int m);
    int  getReorderMethod()      const;
    void setReorderDisplayOnly(bool b);
    bool getReorderDisplayOnly() const;

    // Merge recommendations (the panel under the child palette).
    void   setMergeRecommendMax(int n);
    int    getMergeRecommendMax()          const;
    void   setMergeRecommendErrorFloor(double v);
    double getMergeRecommendErrorFloor()   const;
    void   setMergeRecommendQualityFloor(double v);
    double getMergeRecommendQualityFloor() const;
};

#endif
