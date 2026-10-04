#ifndef TEMPLATEVIEW_H
#define TEMPLATEVIEW_H

// TemplateView — the template-library display (DisplayType TEMPLATE_LIBRARY).
//
// A marked unit's waveform is stored by fiber-template as a LINKED SERIES: a
// median per drift chunk and a median per spike-energy bin, written as a .wtf
// (a headerless .spk-layout int16 stack) with a companion .wti index naming
// each row (unit, link, bin, coordinates).  This view loads those for the open
// group+stage and lets the curator:
//   * see which units have templates (the .wti unit set),
//   * pick a unit and a link (drift|adapt) and SCRUB through its series with a
//     slider — the current bin's waveform drawn solid over the whole series as
//     faint ghosts, so drift / adaptation is visible as the shape migrates,
//   * select two or more template units and PIN them as the oblique basis (the
//     template axes of the Shift+O projection), via the active cluster view.
//
// Self-contained (a QWidget, like TemplateMatrixView — NOT a ViewWidget), so it
// carries none of the spike-selection plumbing; it reads the files on disk for
// the open document and renders them.  Reading uses the shared library
// (neurofileio readWti/readSpk + the wti* view-model helpers).

#include <QWidget>
#include <QString>
#include <QList>
#include <QColor>
#include <vector>
#include <cstdint>

#include "neurosuite/core/neurofileio.h"   // WtiIndex / SpkFile / wti* helpers
#include "templateclassstore.h"            // EAP template-class state model (.eap/.tcl)

class KlustersDoc;
class KlustersView;
class QListWidget;
class QComboBox;
class QSlider;
class QLabel;
class QPushButton;
class QStatusBar;

// ── waveform panel ──────────────────────────────────────────────────────────
// A plain QWidget (no signals/slots, so no moc) that paints one template bin's
// waveform across channels, with the rest of the series as faint ghosts.  Data
// is pushed in by TemplateView; geometry is channel-stacked and auto-scaled.
class TemplateWavePanel : public QWidget {
public:
    explicit TemplateWavePanel(QWidget* parent = nullptr);

    int  nSamples  = 0;
    int  nChannels = 0;
    QColor accent  = QColor(80, 170, 255);
    QColor background = QColor(20, 20, 20);
    // Each entry is one bin's waveform (nSamples*nChannels int16, channel-fastest),
    // in scrub order; `current` is the bin drawn solid (others are ghosts).
    std::vector<std::vector<int16_t>> series;
    int current = -1;

    void setData(int nSamp, int nChan,
                 std::vector<std::vector<int16_t>> waves, int cur);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    double globalMaxAbs() const;   // across the whole series, for a shared scale
};

// ── the display ───────────────────────────────────────────────────────────
class TemplateView : public QWidget {
    Q_OBJECT
public:
    explicit TemplateView(KlustersDoc& doc, KlustersView& view,
                          const QColor& backgroundColor, QStatusBar* statusBar,
                          QWidget* parent = nullptr);

    // Re-read the .wti/.wtf library and the .eap/.tcl class store from disk and
    // rebuild the UI — same sequence the constructor runs.  Called after Klusters
    // (re)writes templates so an open display updates without being reopened.
    void reloadFromDisk();

private:
    void loadFromDisk();                 // resolve paths, read .wti + .wtf
    void rebuildUnitList();              // fill the unit list from the .wti
    void onUnitChanged();                // populate links + reset scrub for the shown unit
    void onLinkOrBinChanged();           // push the selected (unit,link,bin) to the panel
    void onPinClicked();                 // selected units -> oblique basis (via the view)
    int  shownUnit() const;              // the unit whose series is displayed (current row)

    // ── template classes (.eap/.tcl via TemplateClassStore) ───────────────────
    void loadClasses();                  // open the store for the resolved group+stage
    void rebuildClassList();             // fill the class list from active .tcl classes
    void updateClassButtons();           // enable/disable per selection + primary
    void persistClasses();               // save the store (.eap + .tcl) to disk
    void showStatus(const QString& msg); // status bar, null-safe
    int  selectedClassCol() const;       // .eap column of the selected class row, or -1
    int  firstSelectedUnit() const;      // lowest shown cluster id > 1, or -1 (provenance)
    std::vector<int64_t> selectionSpikeIndices() const;  // 0-based .spk ids of shown units
    void onNewClass();                   // new class from the current cluster selection
    void onUpdatePrimary();              // re-set the primary class to the current selection
    void onMergeIntoPrimary();           // fold the selected class into the primary
    void onDeleteClass();                // tombstone the selected class
    void onRenameClass();                // relabel the selected class
    void onSetPrimary();                 // make the selected class the primary
    void onRegenClicked();               // request a fiber-template waveform regen

    KlustersDoc&  doc;
    KlustersView& klView;
    QStatusBar*   statusBar = nullptr;

    // Resolved session coordinates (from the open document's loaded paths).
    QString base;        // absolute <...>/<sessionBase>
    QString variant;     // waveform method token (the open .spk variant)
    QString tag;         // stage tag ("" = untagged)
    int     group    = 0;
    int     nSamples = 0;
    int     nChannels = 0;

    neurofileio::WtiIndex   wti;         // the shared row index
    neurofileio::SpkFile    wtf;         // the open variant's waveform stack
    bool                    loaded = false;
    bool                    coordsResolved = false;  // base/group/tag/geometry known

    // template-class state (independent of the .wti library; see TemplateClassStore)
    TemplateClassStore      classStore;
    bool                    classesLoaded = false;

    // widgets
    QListWidget*      unitList = nullptr;   // template units (multi-select)
    QComboBox*        linkCombo = nullptr;  // drift | adapt (per shown unit)
    QSlider*          binSlider = nullptr;  // scrub over the current link's bins
    QLabel*           infoLabel = nullptr;  // bin coordinates + spike count
    QLabel*           headerLabel = nullptr;// session/method summary or "no templates"
    QPushButton*      pinButton = nullptr;  // pin selected units as the oblique basis
    TemplateWavePanel* panel = nullptr;

    // template-class panel
    QLabel*      classHeader       = nullptr;  // "Template classes" + primary summary
    QListWidget* classList         = nullptr;  // active classes (id, label, member count)
    QPushButton* newClassButton    = nullptr;
    QPushButton* updateClassButton = nullptr;
    QPushButton* mergeClassButton  = nullptr;
    QPushButton* deleteClassButton = nullptr;
    QPushButton* renameClassButton = nullptr;
    QPushButton* setPrimaryButton  = nullptr;
    QPushButton* regenButton       = nullptr;
};

#endif // TEMPLATEVIEW_H
