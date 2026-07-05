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
    if(!doc){ if(mHierarchicalView) mHierarchicalView->setChecked(false); return; }
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
        repopulateChildPalette(clusterPalette->selectedClusters());
    } else {
        childPanel->hide();
        childPaletteA->reset();
        parentSlotA = -1;
        childPalette = childPaletteA;
        doc->setActiveClustering(false);                 // views back to the parent
        if(activeView())
            doc->shownClustersUpdate(clusterPalette->selectedClusters(), *activeView());
    }
    const bool editable = on && doc->hasChildClustering();
    if(mMergeFibers)    mMergeFibers->setEnabled(editable);
    if(mPromoteChild)   mPromoteChild->setEnabled(editable);
    if(mMoveChild)      mMoveChild->setEnabled(editable);
    if(mGroupChildren)  mGroupChildren->setEnabled(editable);
    if(mDissolveFiber)  mDissolveFiber->setEnabled(editable);
    if(mDropChildNoise) mDropChildNoise->setEnabled(editable);
    if(mRefiberize)     mRefiberize->setEnabled(editable);
    if(mMergeChildren)  mMergeChildren->setEnabled(editable);
    if(mUndoChildEdit)  mUndoChildEdit->setEnabled(editable && doc->childUndoCount() > 0);
    if(mRedoChildEdit)  mRedoChildEdit->setEnabled(editable && doc->childRedoCount() > 0);
}

void KlustersApp::slotMergeChildren(){
    if(!doc || !activeView() || !childPanel || !childPanel->isVisible()) return;
    const QList<int> kids = childPalette->selectedClusters();
    if(kids.size() < 2){
        statusBar()->showMessage(tr("Select two or more children of the same fiber to merge."), 4000);
        return;
    }
    if(doc->mergeChildren(kids, *activeView()) < 0)
        statusBar()->showMessage(tr("Children must belong to the same fiber to merge."), 4000);
    if(mUndoChildEdit) mUndoChildEdit->setEnabled(doc->childUndoCount() > 0);
    if(mRedoChildEdit) mRedoChildEdit->setEnabled(doc->childRedoCount() > 0);
}

void KlustersApp::slotUndoChildEdit(){
    if(!doc || !activeView()) return;
    if(doc->childUndoCount() == 0){
        statusBar()->showMessage(tr("No child-layer (atom) edit to undo."), 4000);
        return;
    }
    doc->undoChildEditDispatch();
    if(mUndoChildEdit) mUndoChildEdit->setEnabled(doc->childUndoCount() > 0);
    if(mRedoChildEdit) mRedoChildEdit->setEnabled(doc->childRedoCount() > 0);
}

void KlustersApp::slotRedoChildEdit(){
    if(!doc || !activeView()) return;
    doc->redoChildEditDispatch();
    if(mUndoChildEdit) mUndoChildEdit->setEnabled(doc->childUndoCount() > 0);
    if(mRedoChildEdit) mRedoChildEdit->setEnabled(doc->childRedoCount() > 0);
}

void KlustersApp::slotGroupChildrenIntoFiber(){
    if(!doc || !activeView() || !childPanel || !childPanel->isVisible()) return;
    const QList<int> kids = childPalette->selectedClusters();
    if(kids.isEmpty()){
        statusBar()->showMessage(tr("Select the children to group into a new fiber."), 4000);
        return;
    }
    doc->groupChildrenIntoFiber(kids, *activeView());
}

void KlustersApp::slotDissolveFiber(){
    if(!doc || !activeView()) return;
    const QList<int> sel = clusterPalette->selectedClusters();
    if(sel.size() != 1){
        statusBar()->showMessage(tr("Select exactly one fiber to dissolve into its children."), 4000);
        return;
    }
    doc->dissolveFiber(sel.first(), *activeView());
}

void KlustersApp::slotRefiberize(){
    if(!doc || !activeView()) return;
    doc->refiberize();   // re-cut straddling atoms + rebuild the child<->fiber maps (.clp on Save)
    statusBar()->showMessage(tr("Refiberized: atoms re-cut onto the current fibers."), 4000);
}

void KlustersApp::slotDropChildToNoise(){
    if(!doc || !activeView() || !childPanel || !childPanel->isVisible()) return;
    const QList<int> kids = childPalette->selectedClusters();
    if(kids.isEmpty()){
        statusBar()->showMessage(tr("Select the child(ren) to drop to noise."), 4000);
        return;
    }
    for(int c : kids)
        doc->dropChildToNoise(c, *activeView());
}

void KlustersApp::slotMergeFibers(){
    if(!doc || !activeView()) return;
    const QList<int> sel = clusterPalette->selectedClusters();
    if(sel.size() < 2){
        statusBar()->showMessage(tr("Select two or more fibers in the main palette to merge."), 4000);
        return;
    }
    doc->mergeParentFibers(sel, *activeView());
}

void KlustersApp::slotPromoteChildren(){
    if(!doc || !activeView() || !childPanel || !childPanel->isVisible()) return;
    const QList<int> kids = childPalette->selectedClusters();
    if(kids.isEmpty()){
        statusBar()->showMessage(tr("Select one or more children to promote."), 4000);
        return;
    }
    QList<int> newParents;
    for(int c : kids){
        const int p = doc->promoteChild(c, *activeView());   // each is one undo step
        if(p > 0) newParents.append(p);
    }
    // The new fibers inherit focus: select them in the main palette and give it
    // list focus so the curator continues from the just-promoted parents.
    if(!newParents.isEmpty() && clusterPalette){
        clusterPalette->selectItems(newParents);
        clusterPalette->setFocusToList();
    }
}

void KlustersApp::slotMoveChildrenToFiber(){
    if(!doc || !activeView() || !childPanel || !childPanel->isVisible()) return;
    const QList<int> kids = childPalette->selectedClusters();
    const QList<int> target = clusterPalette->selectedClusters();
    if(kids.isEmpty() || target.size() != 1){
        statusBar()->showMessage(
            tr("Select child(ren) in the child palette and exactly one target fiber in the main palette."), 5000);
        return;
    }
    for(int c : kids)
        doc->moveChild(c, target.first(), *activeView());
}

void KlustersApp::assignChildSlot(ClusterPalette* pal, int parentId){
    if(!pal || !doc) return;
    if(parentId < 0){ pal->clearPaletteScope(); pal->reset(); return; }
    const QList<int> kids = doc->childrenOf(QList<int>{parentId});
    // Build the palette from the child clustering's colours, scoped to this
    // parent's children; restore parent-active so the rest of the app keeps
    // seeing the parent clustering.
    doc->setActiveClustering(true);
    pal->setPaletteScope(kids);
    pal->createClusterList(doc);
    doc->setActiveClustering(false);
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
//   Ctrl+Up ............... new fiber from selected children (or all children of 2+ parents)
//   Ctrl+Down ............. group selected parents
//   Ctrl+Shift+Down ....... dissolve the selected parent
// (The Ctrl+Left/Right A<->B spike-custody transfer was retired with the second
//  child palette.)
bool KlustersApp::dispatchHierarchyKey(int key, Qt::KeyboardModifiers mods){
    if(!doc || !activeView() || !childPanel || !childPanel->isVisible()) return false;
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
                statusBar()->showMessage(tr("Children must belong to the same fiber to merge."), 4000);
        } else if(kidsAB.size() == 1){
            statusBar()->showMessage(tr("Select 2+ children to merge, or none to merge a whole fiber's children."), 4000);
        } else if(parents.size() == 1){
            doc->mergeChildren(doc->childrenOf(parents), *activeView());   // collapse one fiber's children
        } else if(parents.size() >= 2){
            doc->mergeParentFibers(parents, *activeView());                // fold parents into one
        } else {
            return false;
        }
        refreshChildUndoActions();
        return true;
    }

    // Arrow operations require the Ctrl modifier and a palette in focus.
    if(!ctrl || !paletteHasFocus()) return false;

    if(key == Qt::Key_Up && !shift){                       // new fiber from children
        QList<int> kids = kidsAB;
        if(kids.isEmpty() && parents.size() >= 2) kids = doc->childrenOf(parents);
        if(kids.isEmpty()){
            statusBar()->showMessage(tr("Select children (or 2+ parents) to form a new fiber."), 4000);
            return true;
        }
        doc->groupChildrenIntoFiber(kids, *activeView());
        return true;
    }
    if(key == Qt::Key_Down && shift){                      // dissolve parent
        if(parents.size() != 1){
            statusBar()->showMessage(tr("Select exactly one fiber to dissolve into its children."), 4000);
            return true;
        }
        doc->dissolveFiber(parents.first(), *activeView());
        return true;
    }
    if(key == Qt::Key_Down && !shift){                     // group parents
        if(parents.size() < 2){
            statusBar()->showMessage(tr("Select two or more fibers to group."), 4000);
            return true;
        }
        doc->mergeParentFibers(parents, *activeView());
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
    if(!doc || !childPanel || !childPanel->isVisible()) return;
    // The first selected parent populates the child palette; further parents are
    // ignored (the child view shows one parent's children at a time).
    parentSlotA = parents.size() >= 1 ? parents[0] : -1;
    assignChildSlot(childPaletteA, parentSlotA);
    if(focusedChildPalette() == nullptr) childPalette = childPaletteA;
}

void KlustersApp::slotChildSelectionChanged(const QList<int>&){
    if(!doc || !activeView()) return;
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
