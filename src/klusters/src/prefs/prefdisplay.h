#ifndef PREFDISPLAY_H
#define PREFDISPLAY_H

#include "prefdisplaylayout.h"
#include <QColor>

class PrefDisplay : public PrefDisplayLayout
{
    Q_OBJECT
public:
    explicit PrefDisplay(QWidget* parent = nullptr);
    ~PrefDisplay() override = default;








private:
    // Grey the Template Library auto-show when Templates mode is off (the
    // template matrix and drift controls on the Matrices page follow the same
    // switch, through PrefDialog).
    void setTemplateControlsEnabled(bool on);
};

#endif
