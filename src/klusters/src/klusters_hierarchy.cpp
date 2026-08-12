/* klusters_hierarchy.cpp -- KlustersApp hierarchical (.clc child) view + child-palette slots.
 * Split out of klusters.cpp (one KlustersApp class, many .cpp files; the
 * Q_OBJECT lives in klusters.h so moc is unaffected).  Same include set as
 * klusters.cpp; static members are defined once there. */

#include <algorithm>
#include <cmath>
#include <memory>     // std::make_shared — used by Shift+S stale-matrix wait
/***************************************************************************
                          klusters.cpp  -  description
                             -------------------
    begin                : Mon Sep  8 12:06:21 EDT 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                : lynn.hazan@myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "config-klusters.h"
// application specific includes
#include "klusters.h"
#include "clusterview.h"
#include "klustersdoc.h"
#include <neurosuite/core/custody.hpp>   // shared chain-of-custody type policy (clu/clc/...)
#include "clusterPalette.h"
#include "autoMerge.h"      // patch 0069
#include "savethread.h"
#include "prefdialog.h"
#include "configuration.h"  // class Configuration
#include "processwidget.h"
#include "realignworker.h"
#include "serialjobqueue.h"
#include "realignjob.h"
#include "qhelpviewer.h"
// For the Shift+S reorder action: needs the public accessors + signals
// added to these view classes.
#include "errormatrixview.h"
#include "templatematrixview.h"
#include "residualmatrixview.h"
#include "array.h"



// include files for QT
#include <QDir>
#include <QTabBar>

#include <QToolTip>
#include <QToolButton>
#include <QString>
#include <QImage>
#include <QSet>
#include <QHash>
#include <QIcon>  
#include <QCursor>
#include <QFileInfo> 
#include <QApplication>
#include <QInputDialog>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QProgressBar>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSlider>
#include <QGridLayout>
#include <QImage>
#include <QPixmap>
#include <QFrame>
#include <QMenu>
#include <QAction>
#include "plugindialog.h"
#include <QMessageBox>
#include <QLabel>
#include <QPrinter>
#include <QSplitter>
#include <qrecentfileaction.h>
#include <qextendtabwidget.h>
#include <dockarea.h>
#include <QPrintDialog>

#include <QLabel>
#include <QPixmap>
#include <QList>
#include <QEvent>
#include <QKeyEvent>
#include <QAbstractSpinBox>
#include <QPointer>   // QPointer guard for the Shift+S stale-matrix wait
#include "spinbox.h"

#include <QDebug>
#include <QStatusBar>
#include <QProcess>
#include <QMenuBar>
#include <QMessageBox>
#include <QToolBar>
#include <QKeySequence>
#include <QShortcut>  // (kept for potential future use; J/K/X removed 2026-04)
#include <QFileDialog>
#include <QSignalBlocker>
#include <QTime>
#include <QSettings>

extern int nbUndo;

void KlustersApp::slotHierarchicalViewToggled(bool on){
    if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
        qDebug().noquote() << "[rebuild] slotHierarchicalViewToggled";   // focus-trace marker
    if(on){
        if(!doc->isHierarchicalSession()){
            QMessageBox::information(this,tr("Hierarchical view"),
                tr("A hierarchical session needs both a .clc child file and a .clp\n"
                   "parent-map sibling next to this clustering."));
            mHierarchicalView->setChecked(false);
            return;
        }
        QString err;
        if(!doc->loadChildClustering(err)){
            QMessageBox::critical(this,tr("Hierarchical view"),
                tr("Could not load the child clustering:\n%1").arg(err));
            mHierarchicalView->setChecked(false);
            return;
        }
        childPanel->show();
        // The recommend panel is opt-out: its ranking sweep compares every pair of
        // clusters, so a curator who is not using it should not pay for it at all.
        // (Preferences > Sorting > Recommended Merges.)
        if(recommendPanel && configuration().getShowMergeRecommendPanel())
            recommendPanel->show();
        repopulateChildPalette(clusterPalette->selectedClusters());
        scheduleRefreshMergeRecommendations();
    } else {
        childPanel->hide();
        if(recommendPanel) recommendPanel->hide();
        childPaletteA->reset();
        parentSlotA = -1;
        childPalette = childPaletteA;
        doc->setActiveClustering(false);                 // views back to the parent
        if(activeView())
            doc->shownClustersUpdate(clusterPalette->selectedClusters(), *activeView());
    }
    const bool editable = on && doc->hasChildClustering();
    if(mMergeParents)    mMergeParents->setEnabled(editable);
    if(mPromoteChild)   mPromoteChild->setEnabled(editable);
    if(mGroupChildren)  mGroupChildren->setEnabled(editable);
    if(mDissolveParent)  mDissolveParent->setEnabled(editable);
    if(mDropChildNoise) mDropChildNoise->setEnabled(editable);
    if(mRepairNesting)     mRepairNesting->setEnabled(editable);
    if(mMergeChildren)  mMergeChildren->setEnabled(editable);
    if(mMergeAllChildren) mMergeAllChildren->setEnabled(editable);
    if(mUndoChildEdit)  mUndoChildEdit->setEnabled(editable && doc->childUndoCount() > 0);
    if(mRedoChildEdit)  mRedoChildEdit->setEnabled(editable && doc->childRedoCount() > 0);
}

void KlustersApp::slotMergeChildren(){
    if(!activeView() || !childPanel || !childPanel->isVisible()) return;
    const QList<int> kids = childPalette->selectedClusters();
    if(kids.size() < 2){
        statusBar()->showMessage(tr("Select two or more children of the same parent to merge."), 4000);
        return;
    }
    if(doc->mergeChildren(kids, *activeView()) < 0)
        statusBar()->showMessage(tr("Children must belong to the same parent to merge."), 4000);
    if(mUndoChildEdit) mUndoChildEdit->setEnabled(doc->childUndoCount() > 0);
    if(mRedoChildEdit) mRedoChildEdit->setEnabled(doc->childRedoCount() > 0);
}

void KlustersApp::slotFlattenHierarchyToClu(){
    if(!activeView() || !doc->hasChildClustering()) return;
    // Destructive across the whole session -- the entire sub-mode layer goes away
    // -- so confirm before doing it rather than after.  Undo exists (one step),
    // but a mis-click on 8000+ parents is not something to discover by surprise.
    const QMessageBox::StandardButton go = QMessageBox::question(this,
        tr("Merge all children"),
        tr("Merge every parent's child atoms into a single self child\n"
           "(atom id == parent id)?\n\n"
           "This discards all sub-mode structure in the atom layer for the\n"
           "whole session.  It can be undone in one step with Ctrl+Shift+Z,\n"
           "and is written to disk only when you save."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if(go != QMessageBox::Yes) return;

    const int removed = doc->flattenHierarchyToClu(*activeView());
    if(removed < 0)
        statusBar()->showMessage(tr("Could not flatten: the parent and atom layers "
                                    "disagree in length."), 6000);
    else if(removed == 0)
        statusBar()->showMessage(tr("Nothing to do: every parent already has a single "
                                    "self child."), 4000);
    else
        statusBar()->showMessage(tr("Merged all children: %1 atom(s) removed; each parent "
                                    "is now its own self child.").arg(removed), 6000);
    repopulateChildPalette(clusterPalette->selectedClusters());
    if(mUndoChildEdit) mUndoChildEdit->setEnabled(doc->childUndoCount() > 0);
    if(mRedoChildEdit) mRedoChildEdit->setEnabled(doc->childRedoCount() > 0);
}

void KlustersApp::slotUndoChildEdit(){
    if(!activeView()) return;
    if(doc->childUndoCount() == 0){
        statusBar()->showMessage(tr("No child-layer (atom) edit to undo."), 4000);
        return;
    }
    doc->undoChildEditDispatch();
    if(mUndoChildEdit) mUndoChildEdit->setEnabled(doc->childUndoCount() > 0);
    if(mRedoChildEdit) mRedoChildEdit->setEnabled(doc->childRedoCount() > 0);
}

void KlustersApp::slotRedoChildEdit(){
    if(!activeView()) return;
    doc->redoChildEditDispatch();
    if(mUndoChildEdit) mUndoChildEdit->setEnabled(doc->childUndoCount() > 0);
    if(mRedoChildEdit) mRedoChildEdit->setEnabled(doc->childRedoCount() > 0);
}

void KlustersApp::slotGroupChildrenIntoParent(){
    if(!activeView() || !childPanel || !childPanel->isVisible()) return;
    const QList<int> kids = childPalette->selectedClusters();
    if(kids.isEmpty()){
        statusBar()->showMessage(tr("Select the children to group into a new parent."), 4000);
        return;
    }
    doc->promoteChildren(kids, *activeView());
}

void KlustersApp::slotDissolveParent(){
    if(!activeView()) return;
    const QList<int> sel = clusterPalette->selectedClusters();
    if(sel.size() != 1){
        statusBar()->showMessage(tr("Select exactly one parent to dissolve into its children."), 4000);
        return;
    }
    doc->dissolveParent(sel.first(), *activeView());
}

void KlustersApp::slotRepairNesting(){
    if(!activeView()) return;
    doc->repairNesting();   // re-cut straddling atoms + rebuild the child<->parent maps (.clp on Save)
    statusBar()->showMessage(tr("Nesting repaired: atoms re-cut onto the current parents."), 4000);
}

void KlustersApp::slotDropChildToNoise(){
    if(!activeView() || !childPanel || !childPanel->isVisible()) return;
    const QList<int> kids = childPalette->selectedClusters();
    if(kids.isEmpty()){
        statusBar()->showMessage(tr("Select the child(ren) to drop to noise."), 4000);
        return;
    }
    for(int c : kids)
        doc->dropChildToNoise(c, *activeView());
}

void KlustersApp::slotMergeParents(){
    if(!activeView()) return;
    const QList<int> sel = clusterPalette->selectedClusters();
    if(sel.size() < 2){
        statusBar()->showMessage(tr("Select two or more parents in the main palette to merge."), 4000);
        return;
    }
    doc->mergeParents(sel, *activeView());
}

void KlustersApp::slotPromoteChildren(){
    if(!activeView() || !childPanel || !childPanel->isVisible()) return;
    const QList<int> kids = childPalette->selectedClusters();
    if(kids.isEmpty()){
        statusBar()->showMessage(tr("Select one or more children to promote."), 4000);
        return;
    }
    // One new parent PER CHILD -- each promoted child becomes its own parent, and
    // each call is one undo step.  Pooling them into a single parent is the other
    // operation, reached by passing them together.
    QList<int> newParents;
    for(int c : kids){
        const int p = doc->promoteChildren(QList<int>{c}, *activeView());
        if(p > 0) newParents.append(p);
    }
    // The new parents inherit focus: select them in the main palette and give it
    // list focus so the curator continues from the just-promoted parents.
    if(!newParents.isEmpty() && clusterPalette){
        clusterPalette->selectItems(newParents);
        clusterPalette->setFocusToList();
    }
}


// True while a child palette is being rebuilt.  See slotChildSelectionChanged.
bool KlustersApp::childPaletteRebuilding = false;

void KlustersApp::assignChildSlot(ClusterPalette* pal, int parentId){
    if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
        qDebug().noquote() << "[rebuild] assignChildSlot";   // focus-trace marker
    // A rebuild empties the list before refilling it, and an empty child palette
    // makes slotChildSelectionChanged() drop the clustering scope back to the
    // parent.  That rule is meant for the user deselecting every child; applied to
    // the transient emptiness of a rebuild it turns the flag false in the middle of
    // a child edit, and every reader of it then does the wrong thing -- the
    // automatic renumber selected ATOM ids in the FIBER palette, and
    // refreshActivePalette() routed a child edit's landing there for the same
    // reason.  Both were fixed at the reader; this fixes the flag.
    ChildRebuildGuard guard;
    if(!pal) return;
    if(parentId < 0){ pal->clearPaletteScope(); pal->reset(); return; }
    const QList<int> kids = doc->childrenOf(QList<int>{parentId});
    // Build the palette from the child clustering's colours, scoped to this
    // parent's children, then RESTORE the scope this was called with.
    //
    // It used to end with setActiveClustering(false) unconditionally, described as
    // "restore parent-active" -- but that hardcodes the value it assumes was there
    // instead of restoring the one that was.  When the rebuild is driven by a child
    // edit the scope was TRUE on entry, so the rebuild silently turned child scope
    // off as a side effect of redrawing a list.
    //
    // Everything downstream keys off that flag.  isChildClusteringActive() is what
    // the parent-palette focus guards test, so they disarmed and pulled focus to the
    // parent; refreshActivePalette() routes on it, so later refreshes went to the
    // wrong palette; and slotChildSelectionChanged() sets it false again when the
    // rebuilt palette comes up with nothing selected, which closed the loop.  The
    // focus trace showed the whole thing: child -> (none) as createClusterList tore
    // the items down, then child -> parent once the guards had gone quiet.
    const bool wasChildActive = doc->isChildClusteringActive();
    doc->setActiveClustering(true);
    pal->setPaletteScope(kids);
    pal->createClusterList(doc);
    doc->setActiveClustering(wasChildActive);
}

ClusterPalette* KlustersApp::curationPalette() const {
    if (ClusterPalette* cp = focusedChildPalette()) return cp;
    if (doc && doc->matrixScopeActive() && childPanel && childPanel->isVisible())
        return childPalette;
    return nullptr;
}

ClusterPalette* KlustersApp::focusedChildPalette() const {
    QWidget* f = QApplication::focusWidget();
    while(f){
        if(f == childPaletteA) return childPaletteA;
        f = f->parentWidget();
    }
    return nullptr;
}

QList<int> KlustersApp::selectedChildrenAB() const {
    QList<int> kids;
    if(childPaletteA) kids += childPaletteA->selectedClusters();
    return kids;
}

void KlustersApp::refreshChildUndoActions(){
    if(mUndoChildEdit) mUndoChildEdit->setEnabled(doc && doc->childUndoCount() > 0);
    if(mRedoChildEdit) mRedoChildEdit->setEnabled(doc && doc->childRedoCount() > 0);
}

void KlustersApp::cycleHierarchyFocus(bool forward){
    // Cycle focus across the full ring: cluster palette, the child palette
    // (when shown), then the toolbar spinboxes/line-edits.  Mirrors the focus
    // logic formerly bound to Tab.
    buildFocusZones();
    if(focusZones.isEmpty()) return;
    QWidget* focused = QApplication::focusWidget();
    int cur = -1;
    if(focused){
        for(int z = 0; z < focusZones.size() && cur < 0; ++z){
            for(QObject* w = focused; w; w = w->parent())
                if(w == focusZones[z]){ cur = z; break; }
        }
    }
    const int n = focusZones.size();
    const int nextI = (cur < 0) ? (forward ? 0 : n - 1)
                                : (forward ? (cur + 1) % n : (cur - 1 + n) % n);
    focusZones[nextI]->setFocus(Qt::TabFocusReason);
}

// Ctrl-arrow / M hierarchy operations, dispatched by selection state while the
// child view is visible.  All operations reuse the existing doc primitives
// (the same ones the &Hierarchy menu drives).  Returns true when consumed.
//   G (no mod) ............ adaptive merge (see below)
//   Ctrl+Up ............... new parent from selected children (or all children of 2+ parents)
//   Ctrl+Down ............. group selected parents
//   Ctrl+Shift+Down ....... dissolve the selected parent
// (The Ctrl+Left/Right A<->B spike-custody transfer was retired with the second
//  child palette.)
bool KlustersApp::dispatchHierarchyKey(int key, Qt::KeyboardModifiers mods){
    if(!activeView() || !childPanel || !childPanel->isVisible()) return false;
    const bool ctrl  = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const QList<int> parents = clusterPalette->selectedClusters();
    const QList<int> kidsAB  = selectedChildrenAB();

    // G -- adaptive merge.  Only when a palette holds focus (otherwise G is the
    // flat group-clusters action).  M was the old binding; it now stays free for
    // the mean-presentation display toggle.
    if(key == Qt::Key_G && mods == Qt::NoModifier){
        if(!paletteHasFocus()) return false;
        if(kidsAB.size() >= 2){
            if(doc->mergeChildren(kidsAB, *activeView()) < 0)
                statusBar()->showMessage(tr("Children must belong to the same parent to merge."), 4000);
        } else if(kidsAB.size() == 1){
            statusBar()->showMessage(tr("Select 2+ children to merge, or none to merge a whole parent's children."), 4000);
        } else if(parents.size() == 1){
            doc->mergeChildren(doc->childrenOf(parents), *activeView());   // collapse one parent's children
        } else if(parents.size() >= 2){
            doc->mergeParents(parents, *activeView());                // fold parents into one
        } else {
            return false;
        }
        refreshChildUndoActions();
        return true;
    }

    // Arrow operations require the Ctrl modifier and a palette in focus.
    if(!ctrl || !paletteHasFocus()) return false;

    if(key == Qt::Key_Up && !shift){                       // new parent from children
        QList<int> kids = kidsAB;
        if(kids.isEmpty() && parents.size() >= 2) kids = doc->childrenOf(parents);
        if(kids.isEmpty()){
            statusBar()->showMessage(tr("Select children (or 2+ parents) to form a new parent."), 4000);
            return true;
        }
        doc->promoteChildren(kids, *activeView());
        return true;
    }
    if(key == Qt::Key_Down && shift){                      // dissolve parent
        if(parents.size() != 1){
            statusBar()->showMessage(tr("Select exactly one parent to dissolve into its children."), 4000);
            return true;
        }
        doc->dissolveParent(parents.first(), *activeView());
        return true;
    }
    if(key == Qt::Key_Down && !shift){                     // group parents
        if(parents.size() < 2){
            statusBar()->showMessage(tr("Select two or more parents to group."), 4000);
            return true;
        }
        doc->mergeParents(parents, *activeView());
        return true;
    }
    if((key == Qt::Key_Left || key == Qt::Key_Right) && !shift){
        // The Left/Right A<->B spike-custody transfer needed two child panes; with
        // a single child palette there is no second pane to transfer to/from, so
        // the gesture is retired.  Let the key fall through to its default handling.
        return false;
    }
    return false;
}

void KlustersApp::repopulateChildPalette(const QList<int>& parents){
    if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
        qDebug().noquote() << "[rebuild] repopulateChildPalette";   // focus-trace marker
    if(!childPanel || !childPanel->isVisible()) return;

    // The parent comes from the DOC, not from the caller's list.
    //
    // Four call sites reach here as a refresh and pass
    // clusterPalette->selectedClusters() straight through, so deriving the parent
    // from that argument made the displayed parent a function of the parent
    // selection at whatever instant a rebuild happened to run -- and any
    // incidental change to that selection moved the user to another parent in the
    // middle of curating.  Merging two children and landing on a different parent
    // was this.
    //
    // Now the doc owns it and changes it in exactly two situations: the user
    // selects a parent, or the parent ceases to exist.  A caller that merely wants
    // the list redrawn cannot move it, which is the property that makes the mode
    // usable -- every operation lands on its own output under the same parent.
    Q_UNUSED(parents);
    parentSlotA = doc ? doc->curatedParent() : -1;

    const bool hadChildFocus = (focusedChildPalette() != nullptr);
    bool landedFromOperation = false;   // a parked landing was applied below
    QList<int> priorSelection;
    if(childPaletteA) priorSelection = childPaletteA->selectedClusters();

    assignChildSlot(childPaletteA, parentSlotA);
    if(focusedChildPalette() == nullptr) childPalette = childPaletteA;

    // Selection and focus are preserved across the rebuild: the rebuild is a
    // redraw, not an edit, and the deferred refreshes that follow an operation
    // carry no landing of their own.
    if(childPaletteA && parentSlotA >= 0){
        const QList<int> live = doc->childrenOf(QList<int>{parentSlotA});

        // A parked landing wins over the prior selection: an operation asked to
        // end up on its output, and that outranks whatever was selected before it
        // ran.  Drained here rather than applied at the operation, so it survives
        // however many deferred rebuilds follow.
        QList<int> want = doc->takePendingChildSelection();
        const bool fromLanding = !want.isEmpty();
        if(want.isEmpty()) want = priorSelection;

        QList<int> restore;
        for(int id : want)
            if(live.contains(id)) restore.append(id);
        if(!restore.isEmpty()){
            childPaletteA->selectItems(restore);
            // An operation that parked a landing asked to END UP there -- "return
            // focus to the output clusters or merged clusters within the children
            // palette".  Focus follows it unconditionally, NOT gated on
            // hadChildFocus: that flag asks whether a child palette held Qt focus
            // when the rebuild started, and after picking a pair from a matrix cell
            // the answer is no, the matrix does.  Gating on it means the merged
            // child is selected and nothing has focus, which is the reported
            // symptom.
            if(fromLanding) landedFromOperation = true;
        }
    }
    if((hadChildFocus || landedFromOperation) && childPaletteA && parentSlotA >= 0)
        childPaletteA->setFocusToList();
}

void KlustersApp::slotChildSelectionChanged(const QList<int>&){
    if(!activeView()) return;
    // Ignore the empty selection a rebuild passes through on its way to refilling
    // the list: that is not the user saying "no children", it is the list being
    // between states.  Acting on it drops the clustering scope mid-edit.
    if(childPaletteRebuilding) return;
    // The views reflect the children selected in the child palette; with no child
    // selected they fall back to the parent unit(s).
    QList<int> kids;
    if(childPaletteA) kids += childPaletteA->selectedClusters();
    if(kids.isEmpty()){
        doc->setActiveClustering(false);                 // no child selected -> parent view
        doc->shownClustersUpdate(clusterPalette->selectedClusters(), *activeView());
    } else {
        doc->setActiveClustering(true);                  // show the selected children's spikes
        doc->shownClustersUpdate(kids, *activeView());
    }
}
