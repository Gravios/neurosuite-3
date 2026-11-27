/***************************************************************************
    klusters_reload.cpp  -  the displays and selections kept across a reload

    Adding rows (Add Residual Spikes, decollide), sorting them by time or a
    plugin rewriting the files changes every row-indexed structure the open
    document holds, so the document is closed and opened again.  That used to
    leave the curator on a fresh Overview with one cluster selected.  What the
    curator arranged is captured before the close and put back after the open:
    the displays (type, added views, clusters, feature dimensions, order, the
    current one), the feature space shown, the child palette and the child
    selection, the matrices' child scope.  Ids the reopened clustering no
    longer has are left out.  The undo history is not kept: every step in it
    names rows of the files that were closed.

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klusters.h"
#include "klustersdoc.h"
#include "klustersview.h"
#include "clusterpalette.h"

#include <qextendtabwidget.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QSet>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>

KlustersApp::GuiState KlustersApp::captureGuiState() const
{
    GuiState s;
    if (!doc || doc->url().isEmpty() || !tabsParent) return s;
    for (int i = 0; i < tabsParent->count(); ++i) {
        auto* view = qobject_cast<KlustersView*>(tabsParent->widget(i));
        if (!view) continue;                                  // a process (recluster) tab
        if (tabsParent->widget(i) == tabsParent->currentWidget()) s.current = static_cast<int>(s.displays.size());
        GuiState::Display d;
        d.label = tabsParent->tabText(i);
        d.createdAs = view->createdAs();
        d.added = view->addedViews();
        d.clusters = view->clusters();
        d.dimX = view->abscissaDimension();
        d.dimY = view->ordinateDimension();
        s.displays << d;
    }
    s.featureSpace = doc->activeFeatureMethod();
    if (clusterPalette) s.parentSelection = clusterPalette->selectedClusters();
    s.childPaletteShown = childPanel && childPanel->isVisible();
    s.childView = doc->isChildClusteringActive();
    if (s.childView && childPalette) s.childSelection = childPalette->selectedClusters();
    s.matrixScope = doc->matrixScopeEnabled();
    s.valid = !s.displays.isEmpty();
    return s;
}

namespace {

/** @p ids less those @p have does not hold, in their order. */
QList<int> kept(const QList<int>& ids, const QSet<int>& have)
{
    QList<int> out;
    for (const int id : ids) if (have.contains(id)) out << id;
    return out;
}

} // namespace

void KlustersApp::restoreGuiState(const GuiState& s)
{
    if (!s.valid || !doc || doc->url().isEmpty() || !tabsParent || !mainDock) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // ── the space shown (before the displays: they draw its features) ──
    if (!s.featureSpace.isEmpty() && s.featureSpace != doc->activeFeatureMethod())
        showFeatureSpace(s.featureSpace, /*syncFirst*/ false);

    QSet<int> parents;
    for (const dataType id : doc->data().clusterIds()) parents.insert(static_cast<int>(id));
    if (doc->isChildClusteringActive()) doc->setActiveClustering(false);

    // ── the displays: the ones the open made are re-used when of the type wanted ──
    QList<KlustersView*> open;
    for (int i = 0; i < tabsParent->count(); ++i)
        if (auto* v = qobject_cast<KlustersView*>(tabsParent->widget(i))) open << v;
    QList<KlustersView*> placed;
    for (const GuiState::Display& d : s.displays) {
        KlustersView* view = nullptr;
        for (KlustersView* v : open)
            if (!placed.contains(v) && v->createdAs() == d.createdAs) { view = v; break; }
        if (!view) {
            switch (d.createdAs) {
            case KlustersView::CLUSTERS:                slotWindowNewClusterDisplay(); break;
            case KlustersView::WAVEFORMS:               slotWindowNewWaveformDisplay(); break;
            case KlustersView::CORRELATIONS:            slotWindowNewCrosscorrelationDisplay(); break;
            case KlustersView::OVERVIEW:                slotWindowNewOverViewDisplay(); break;
            case KlustersView::GROUPING_ASSISTANT_VIEW: slotWindowNewGroupingAssistantDisplay(); break;
            case KlustersView::TRACES:                  slotNewTraceDisplay(); break;
            case KlustersView::TEMPLATE_LIBRARY:        slotWindowNewTemplateLibraryDisplay(); break;
            default:                                    createDisplay(d.createdAs); break;
            }
            view = qobject_cast<KlustersView*>(tabsParent->widget(tabsParent->count() - 1));
            if (!view || placed.contains(view)) continue;
        }
        placed << view;
        tabsParent->setCurrentWidget(view);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        bool matrices = false;
        for (const KlustersView::DisplayType t : d.added) {
            const bool there = (t == KlustersView::CLUSTERS && view->containsClusterView())
                || (t == KlustersView::WAVEFORMS && view->containsWaveformView())
                || (t == KlustersView::CORRELATIONS && view->containsCorrelationView())
                || (t == KlustersView::ERROR_MATRIX && view->containsErrorMatrixView())
                || (t == KlustersView::TEMPLATE_MATRIX && view->containsTemplateMatrixView())
                || (t == KlustersView::RESIDUAL_MATRIX && view->containsResidualMatrixView())
                || (t == KlustersView::DRIFT_MATRIX && view->containsDriftMatrixView())
                || (t == KlustersView::TRACES && view->containsTraceView());
            if (there) continue;
            widgetAddToDisplay(t);
            matrices = matrices || t == KlustersView::ERROR_MATRIX || t == KlustersView::TEMPLATE_MATRIX
                       || t == KlustersView::RESIDUAL_MATRIX || t == KlustersView::DRIFT_MATRIX;
        }
        if (matrices) slotUpdateErrorMatrix();             // computes every open matrix
        // as a click in the palette sets it (the palette's own deferred redraw then agrees)
        const QList<int> ids = kept(d.clusters, parents);
        if (!ids.isEmpty()) {
            if (clusterPalette) clusterPalette->selectItems(ids);
            doc->shownClustersUpdate(ids, *view);
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
        if (view->containsClusterView() && d.dimX >= 1 && d.dimY >= 1
            && d.dimX <= doc->nbDimensions() && d.dimY <= doc->nbDimensions()) {
            dimensionX->setValue(d.dimX);
            dimensionY->setValue(d.dimY);
        }
        tabsParent->setTabText(tabsParent->indexOf(view), d.label);
    }
    // the order they were in; a display the open made that was not there before goes
    for (int k = 0; k < placed.size(); ++k) {
        const int at = tabsParent->indexOf(placed[k]);
        if (at >= 0 && at != k) tabsParent->tabBar()->moveTab(at, k);
    }
    for (KlustersView* v : open) {
        if (placed.contains(v) || tabsParent->count() <= 1) continue;
        tabsParent->setCurrentWidget(v);
        slotDisplayClose();
    }
    if (s.current >= 0 && s.current < placed.size()) tabsParent->setCurrentWidget(placed[s.current]);
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);   // slotTabChange: the palette follows

    // ── the child view: the parents whose children are listed, then the children ──
    if (s.childView && !doc->hasChildClustering()) {
        QString err;
        doc->loadChildClustering(err);                          // (not there any more: the parent view stays)
    }
    if (s.childView && doc->hasChildClustering() && clusterPalette && activeView()) {
        const QList<int> ids = kept(s.parentSelection, parents);
        if (!ids.isEmpty()) {
            clusterPalette->selectItems(ids);
            doc->shownClustersUpdate(ids, *activeView());
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
    if (s.childPaletteShown && mShowChildPalette && !mShowChildPalette->isChecked()) {
        mShowChildPalette->setChecked(true);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    if (s.childView && childPalette && activeView()) {
        const QList<int> listed = childPalette->listedClusters();
        const QList<int> kids = kept(s.childSelection, QSet<int>(listed.begin(), listed.end()));
        if (!kids.isEmpty() && childPalette->clickItem(kids.first())) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (kids.size() > 1 && doc->isChildClusteringActive()) {
                childPalette->selectItems(kids);
                doc->shownClustersUpdate(kids, *activeView());
            }
        }
    }
    if (s.matrixScope != doc->matrixScopeEnabled() && doc->hasChildClustering())
        doc->setMatrixScopeEnabled(s.matrixScope);
    QApplication::restoreOverrideCursor();
}

void KlustersApp::restoreGuiStateLater(const GuiState& s)
{
    if (!s.valid) return;
    QTimer::singleShot(0, this, [this, s]() {
        if (mainDock && doc && !doc->url().isEmpty()) restoreGuiState(s);
    });
}

bool KlustersApp::reopenKeepingState(const QString& url, const QString& space)
{
    GuiState state = captureGuiState();
    if (!space.isEmpty()) state.featureSpace = space;
    slotFileClose();
    if (mainDock) return false;                               // the close did not complete
    openDocumentFile(url);
    if (mainDock && doc && !doc->url().isEmpty()) restoreGuiStateLater(state);
    return true;
}
