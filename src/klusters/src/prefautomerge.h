#ifndef PREFAUTOMERGE_H
#define PREFAUTOMERGE_H

#include "prefautomergelayout.h"

// Placeholder class — settings UI added in patch 0068,
// action+algorithm wired in patch 0069.
class PrefAutoMerge : public PrefAutoMergeLayout
{
    Q_OBJECT
public:
    explicit PrefAutoMerge(QWidget* parent = nullptr);
    ~PrefAutoMerge() override = default;
};

#endif
