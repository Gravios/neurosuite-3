#ifndef PREFRECLUSTERING_H
#define PREFRECLUSTERING_H

#include "prefreclusteringlayout.h"

class PrefReclustering : public PrefReclusteringLayout
{
    Q_OBJECT
public:
    explicit PrefReclustering(QWidget* parent = nullptr);
    ~PrefReclustering() override = default;

    void setReclusteringExecutable(const QString& executable);
    void setReclusteringArguments(const QString& arguments);
    void setAutoSelectFeatures(bool checked);
    void setAutoSelectNFeatures(int n);
    void setReclusterMeanSubtractedSubdim(bool checked);
    void setReclusterChannelVariance(bool checked);
    void setReclusterMedianWaveformResidual(bool checked);

    QString getReclusteringExecutable() const;
    QString getReclusteringArguments() const;
    bool    getAutoSelectFeatures() const;
    int     getAutoSelectNFeatures() const;
    bool    getReclusterMeanSubtractedSubdim() const;
    bool    getReclusterChannelVariance() const;
    bool    getReclusterMedianWaveformResidual() const;

private slots:
    void updateReclusteringExecutable();
};

#endif
