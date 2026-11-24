#ifndef PREFCLUSTERORDER_H
#define PREFCLUSTERORDER_H

#include "prefclusterorderlayout.h"

// Preferences ▸ Curation ▸ Cluster order: Reorder Clusters by Similarity
// (Shift+S), the waveform sorts, and renumbering after cluster edits.
class PrefClusterOrder : public PrefClusterOrderLayout
{
    Q_OBJECT
public:
    explicit PrefClusterOrder(QWidget* parent = nullptr);
    ~PrefClusterOrder() override = default;


};

#endif
