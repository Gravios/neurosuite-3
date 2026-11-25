#ifndef PREFMATRICES_H
#define PREFMATRICES_H

#include "prefmatriceslayout.h"

// Preferences ▸ Views ▸ Matrices: when the matrix displays are computed, the
// error matrix's compute options, the template matrix and the drift matrix.
class PrefMatrices : public PrefMatricesLayout
{
    Q_OBJECT
public:
    explicit PrefMatrices(QWidget* parent = nullptr);
    ~PrefMatrices() override = default;


public Q_SLOTS:
    // Grey the template matrix and drift controls when Templates mode
    // (Display page) is off; PrefDialog connects that switch here.
    void setTemplateControlsEnabled(bool on);
};

#endif
