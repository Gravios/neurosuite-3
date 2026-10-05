#ifndef TEMPLATEPALETTE_H
#define TEMPLATEPALETTE_H

// TemplatePalette — the left-column template-class palette (plan
// claude/template-curation-plan.md §11.5).  A compact, quick-access view of the
// active template classes (.eap/.tcl via its own read-mostly TemplateClassStore),
// sitting under the children palette so the curator never leaves the feature
// scatter: select a class to make it the overlay's active/primary class, create
// one from the shown clusters, delete one, and press `s` to pin the selected
// class(es) as the oblique basis.  The fuller class CRUD (merge/rename/update/
// regenerate) stays in the Template Library tab, which reloads from the same
// files; this palette emits classesChanged after its own edits so they stay in
// sync.  Thin wiring: KlustersApp supplies the selection spikes for a New and
// routes the oblique / active-class signals.

#include <QWidget>
#include <QList>

#include "templateclassstore.h"

#include <cstdint>
#include <string>
#include <vector>

class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QCheckBox;

class TemplatePalette : public QWidget
{
    Q_OBJECT
public:
    explicit TemplatePalette(QWidget* parent = nullptr);
    ~TemplatePalette() override = default;

    // (Re)load the class layer for the open group+stage and rebuild the list.
    void reload(const std::string& base, int group, const std::string& stage, int64_t nSpikes);
    // Re-read the .eap/.tcl from disk (e.g. after the tab changed them) and rebuild.
    void refreshFromDisk();
    void clearClasses();

    bool hasClasses() const { return loaded_; }
    int  selectedClass() const;                 ///< the selected .eap column, or -1
    int  primaryClass()  const { return loaded_ ? classStore_.primary() : -1; }
    int  provCluOf(int col) const { return loaded_ ? classStore_.provClu(col) : -1; }

    // Create an ACTIVE class from `spikes` (made primary) and persist; KlustersApp
    // supplies the spikes + provenance from the shown clusters.  Rebuilds + emits
    // classesChanged.  Returns the new column, or -1.
    int  createClassFromSpikes(const std::vector<int64_t>& spikes, int provClu, const std::string& label);

    void setFocusToList();                       ///< focus the list (Tab ring entry point)

    // The stable per-class overlay hue (same formula as ClusterView::lineageClassColor),
    // so the palette swatches match the overlay nodes.
    static QColor classColor(int classId);

Q_SIGNALS:
    void classSelected(int col);                 ///< list row changed -> this class is active/primary
    void obliqueRequested(const QList<int>& cols);///< `s` on >=1 selected class(es) -> pin oblique basis
    void newClassRequested();                    ///< "New" clicked -> KlustersApp gathers the selection
    void classDeleted(int col);                  ///< a class was tombstoned -> the overlay drops its nodes
    void classesChanged();                       ///< a create/delete here -> the tab should reload
    void scaleAbsoluteToggled(bool absolute);    ///< waveform preview scale: best-fit <-> absolute

protected:
    void keyPressEvent(QKeyEvent* e) override;

private Q_SLOTS:
    void onRowChanged();
    void onItemClicked(QListWidgetItem* item);   ///< toggle the PRIMARY (★) on click
    void onNewClicked();
    void onDeleteClicked();

private:
    void rebuild();
    void updateButtons();

    QLabel*      header_ = nullptr;
    QListWidget* list_   = nullptr;
    QPushButton* newBtn_ = nullptr;
    QPushButton* delBtn_ = nullptr;
    QCheckBox*   scaleAbs_ = nullptr;

    TemplateClassStore classStore_;
    bool        loaded_  = false;
    std::string base_, stage_;
    int         group_   = 0;
    int64_t     nSpikes_ = 0;
};

#endif // TEMPLATEPALETTE_H
