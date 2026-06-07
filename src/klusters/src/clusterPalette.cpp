/***************************************************************************
                          clusterPalette.cpp  -  description
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



// application specific includes
#include "klustersdoc.h"
#include "clusterPalette.h"
#include "itemcolors.h"
#include "clusterinformationdialog.h"

// include files for Qt
#include <QVariant>
#include <QPainter>
#include <QLayout>
#include <QToolTip>
#include <QMouseEvent>
#include <QDebug>
#include <climits>

#include <QPixmap>
#include <QBitmap>
#include <QColorDialog>
#include <QColor>
#include <QFrame>
#include <QList>
#include <QHash>
#include <QStatusBar>
#include <QDebug>


ClusterPaletteWidget::ClusterPaletteWidget(QWidget *parent)
    : QListWidget(parent)
{
    setViewMode(QListView::IconMode);
    setDragDropMode(QAbstractItemView::NoDragDrop);
}

void ClusterPaletteWidget::mousePressEvent ( QMouseEvent * event )
{
    if(event->button() == Qt::MiddleButton) {
        QListWidgetItem *item = itemAt(event->position().toPoint());
        if(item)
            Q_EMIT changeColor(item);
    }
    QListWidget::mousePressEvent(event);
}

void ClusterPaletteWidget::mouseMoveEvent ( QMouseEvent * event )
{
    QListWidgetItem *item = itemAt(event->position().toPoint());
    if(item)
        Q_EMIT onItem(item);
    QListWidget::mouseMoveEvent(event);
}

void ClusterPaletteWidget::focusInEvent(QFocusEvent *event)
{
    QListWidget::focusInEvent(event);
    // Ensure a current item exists so arrow keys have a starting point.
    QListWidgetItem* cur = currentItem();
    if (!cur && count() > 0) {
        setCurrentRow(0);
        cur = currentItem();
    }
    if (cur)
        scrollToItem(cur, QAbstractItemView::EnsureVisible);

    // Ask klusters to show the Overview Display tab (it returns focus to us
    // after switching, so we emit after the scroll above is done).
    Q_EMIT paletteGainedFocus();
}

void ClusterPaletteWidget::keyPressEvent(QKeyEvent *event)
{
    const bool hasShiftPressed = event->modifiers() & Qt::ShiftModifier;

    // Left/Right navigate sequentially (previous/next item in list order).
    // Up/Down navigate by row within the same column using visual geometry.
    // Ctrl+Left/Right is reserved for tab cycling at the app level.
    // Let it bubble up without handling it here.
    if((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) &&
       (event->modifiers() & Qt::ControlModifier)){
        QListWidget::keyPressEvent(event);
        return;
    }

    const bool goNext     = (event->key() == Qt::Key_Right);
    const bool goPrev     = (event->key() == Qt::Key_Left);
    const bool goDown     = (event->key() == Qt::Key_Down);
    const bool goUp       = (event->key() == Qt::Key_Up);

    auto applyMove = [&](int targetRow) {
        if (targetRow < 0 || targetRow >= count()) return;
        QListWidgetItem *target = item(targetRow);
        if (hasShiftPressed) {
            QListWidgetItem *c = currentItem();
            if (c) {
                if (target->isSelected())
                    c->setSelected(false);
                else {
                    c->setSelected(true);
                    target->setSelected(true);
                }
            }
            setCurrentItem(target);
        } else {
            // Move to targetRow. clearSelection() + setCurrentRow() ensures
            // a clean single-select visual state for the cursor. Then restore
            // S-pinned highlights so S-pinned clusters stay visually
            // selected across arrow navigation.  selectedClusters() unions
            // visual + S-pinned so every itemSelectionChanged fired here
            // returns the correct display set.
            //
            // Pinned ids are looked up against each item's CLUSTER_ID
            // data role (set in updateClusterList), not by row index, so
            // the highlight is correct regardless of how the iconView was
            // (re)ordered after the pin was made.
            clearSelection();
            setCurrentRow(targetRow);
            if (!sPinnedIds.isEmpty()) {
                for (int k = 0; k < count(); ++k) {
                    QListWidgetItem* it = item(k);
                    if (!it) continue;
                    const int cid = it->data(ClusterPalette::CLUSTER_ID).toInt();
                    if (sPinnedIds.contains(cid))
                        it->setSelected(true);
                }
            }
        }
        scrollToItem(currentItem(), QAbstractItemView::EnsureVisible);
    };

    if (goNext || goPrev) {
        QListWidgetItem *c = currentItem();
        if (!c && count() > 0) {
            // No selection: Right starts at the end, Left starts at the beginning.
            applyMove(goNext ? count() - 1 : 0);
        } else if (c) {
            const int i = row(c);
            if      (goNext && i < count() - 1) applyMove(i + 1);
            else if (goNext)                    applyMove(0);            // wrap: last → first
            else if (goPrev && i > 0)           applyMove(i - 1);
            else if (goPrev)                    applyMove(count() - 1); // wrap: first → last
        }
    } else if (goDown || goUp) {
        QListWidgetItem *c = currentItem();
        if (!c && count() > 0) {
            // No selection: Down starts at the end, Up starts at the beginning.
            applyMove(goDown ? count() - 1 : 0);
        } else if (c) {
            // Find the item directly above or below by comparing visual
            // geometry.  We avoid all column-count arithmetic because the
            // palette reflows freely when the window is resized, making any
            // cached count wrong.
            const QRect cur = visualRect(indexFromItem(c));
            const int   cx  = cur.center().x();
            const int   cy  = cur.center().y();

            // ROW-FIRST navigation.  The previous implementation chose the
            // qualifying item with the smallest horizontal distance to our
            // column, breaking ties by vertical distance.  Palette items are
            // cluster-number labels of differing widths ("7" vs "1024") laid
            // out on a fixed grid, so their visual-rect centres do NOT line up
            // into tidy columns: an item several rows away can have a centre-x
            // closer to ours than the item in the immediately adjacent row.
            // Column-first selection therefore skipped over whole rows of
            // clusters — the "jumps over many units" symptom.
            //
            // Fix: pick the *adjacent row* first (minimum vertical distance in
            // the requested direction), then, within that row only, pick the
            // item whose centre-x is nearest ours.  Up/Down now always moves
            // exactly one row regardless of label-width-induced misalignment.

            // Pass 1 — vertical distance to the nearest qualifying row.
            int nearestDy = INT_MAX;
            for (int k = 0; k < count(); ++k) {
                const int ky = visualRect(model()->index(k, 0)).center().y();
                if (goDown ? (ky <= cy) : (ky >= cy)) continue;
                nearestDy = qMin(nearestDy, qAbs(ky - cy));
            }

            int bestRow = -1;
            if (nearestDy != INT_MAX) {
                // Items within half an item height of that nearest distance are
                // "the next row".  Half the current item height sits well below
                // the row pitch, so items two or more rows away are excluded
                // even when row centres jitter slightly.
                const int rowTol = qMax(cur.height() / 2, 4);

                // Pass 2 — among the adjacent-row items, the nearest column.
                int bestDx = INT_MAX;
                for (int k = 0; k < count(); ++k) {
                    const QRect r  = visualRect(model()->index(k, 0));
                    const int   ky = r.center().y();
                    if (goDown ? (ky <= cy) : (ky >= cy)) continue;
                    if (qAbs(ky - cy) - nearestDy > rowTol) continue;
                    const int dx = qAbs(r.center().x() - cx);
                    if (dx < bestDx) { bestDx = dx; bestRow = k; }
                }
            }

            if (bestRow >= 0) {
                applyMove(bestRow);
            } else {
                // No item found in the target direction — wrap to the opposite
                // end of the same column.
                // Find the row boundary: for goDown (wrap to top) the target y
                // is the minimum centre-y across all items; for goUp (wrap to
                // bottom) it is the maximum.
                int edgeY = goDown ? INT_MAX : INT_MIN;
                for (int k = 0; k < count(); ++k) {
                    const int ky = visualRect(model()->index(k, 0)).center().y();
                    if (goDown && ky < edgeY) edgeY = ky;
                    if (goUp   && ky > edgeY) edgeY = ky;
                }

                // Among all items on that edge row, pick the one with the
                // smallest horizontal distance to our column.
                int wrapRow = -1;
                int wrapDx  = INT_MAX;
                for (int k = 0; k < count(); ++k) {
                    const QRect r  = visualRect(model()->index(k, 0));
                    if (r.center().y() != edgeY) continue;
                    const int   dx = qAbs(r.center().x() - cx);
                    if (dx < wrapDx) { wrapDx = dx; wrapRow = k; }
                }
                if (wrapRow >= 0) applyMove(wrapRow);
            }
        }
    } else if (event->key() == Qt::Key_S) {
        QListWidgetItem *cur = currentItem();
        if (!cur) { QListWidget::keyPressEvent(event); return; }

        // Pin/unpin by cluster id (stable across renumbers) rather than
        // by iconView row (changes whenever the palette is rebuilt).
        const int curId = cur->data(ClusterPalette::CLUSTER_ID).toInt();

        if (!sPinnedIds.contains(curId)) {
            // Not S-pinned yet → add it.
            cur->setSelected(true);
            sPinnedIds.insert(curId);
            lastSPressItem = cur;
        } else if (cur == lastSPressItem) {
            // S pressed a second time on the same item → isolate: clear all
            // other S-pins and deselect them visually, keep only this one.
            sPinnedIds.clear();
            for (int k = 0; k < count(); ++k)
                if (item(k) != cur) item(k)->setSelected(false);
            // Current item stays selected (it was already); sPinnedIds is
            // now empty so next arrow navigation resumes single-select
            // behaviour — matches the original "S-twice = un-pin everything,
            // keep visual selection on this cluster only" semantics.
            lastSPressItem = nullptr;
        } else {
            // S on a different already-S-pinned item → remove it.
            cur->setSelected(false);
            sPinnedIds.remove(curId);
            lastSPressItem = cur;
        }
        emit selectionToggled();
    } else {
        QListWidget::keyPressEvent(event);
    }
}



ClusterPalette::ClusterPalette(const QColor& backgroundColor,QWidget* parent,QStatusBar * statusBar, const char* name )
    : QWidget( parent ),
      doc(0L),
      mode(IMMEDIATE),
      isInSelectItems(false),
      isUpToDate(true),
      backgroundColor(backgroundColor),
      statusBar(statusBar),
      isInUserClusterInfoMode(false)
{
    setObjectName(name);
    QVBoxLayout *layout = new QVBoxLayout;
    layout->setContentsMargins(0,0,0,0);
    //Set the palette color
    setAutoFillBackground(true);

    QPalette palette;
    palette.setColor(backgroundRole(), backgroundColor);
    palette.setColor(foregroundRole(), Qt::white);
    setPalette(palette);

    iconView = new ClusterPaletteWidget(this);
    iconView->setObjectName("ClusterPalette");
    iconView->setPalette(palette);
    layout->addWidget(iconView);
    QFont font( "Helvetica",10);


    iconView->setFont(font);
    iconView->setFrameStyle(QFrame::NoFrame);
    iconView->setResizeMode(QListWidget::Adjust);

    palette = iconView->palette();
    palette.setColor(iconView->backgroundRole(), backgroundColor);

    iconView->setAutoFillBackground(true);
    iconView->viewport()->setAutoFillBackground(false);
    iconView->viewport()->setPalette(palette);
    iconView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    iconView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    iconView->setMouseTracking(true);
    int h;
    int s;
    int v;
    backgroundColor.getHsv(&h,&s,&v);
    QColor legendColor;
    if(s <= 80 && v >= 240 || (s <= 40 && v >= 220))
        legendColor =  Qt::black;
    else
        legendColor =  Qt::white;

    palette.setColor(QPalette::Text, legendColor);
    palette.setColor(QPalette::HighlightedText, legendColor);

    iconView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    iconView->setSpacing(4);

    iconView->setContextMenuPolicy(Qt::CustomContextMenu);

    //Deal with the sizes
    setSizePolicy(QSizePolicy((QSizePolicy::Policy)5,(QSizePolicy::Policy)5));

    iconView->setPalette(palette);
    //Set the legend in the good language
    languageChange();

    connect(iconView, &QListWidget::itemSelectionChanged, this, &ClusterPalette::slotClickRedraw);
    connect(iconView, &QWidget::customContextMenuRequested, this, &ClusterPalette::slotCustomContextMenuRequested);
    connect(iconView, &ClusterPaletteWidget::changeColor, this, &ClusterPalette::changeColor);
    connect(iconView,&ClusterPaletteWidget::onItem,this, &ClusterPalette::slotOnItem);
    connect(iconView,&ClusterPaletteWidget::paletteGainedFocus,this, &ClusterPalette::paletteGainedFocus);
    connect(iconView, &ClusterPaletteWidget::selectionToggled, this, &ClusterPalette::slotClickRedraw);

    // Redirect focus straight to the inner list so Tab-navigation and
    // arrow-key cluster browsing work the moment the panel is entered.
    setFocusProxy(iconView);

    setLayout(layout);
}


ClusterPalette::~ClusterPalette()
{
    // no need to delete child widgets, Qt does it all for us
}

void ClusterPalette::createClusterList(KlustersDoc* document){
    //Assign the document to the doc member for future use
    doc = document;

    updateClusterList();
}

void ClusterPalette::updateClusterList(){
    if(!doc)
        return;
    iconView->clear();

    // iconView->clear() destroys every QListWidgetItem; lastSPressItem
    // now dangles, so reset it before building the fresh icons.  The
    // pinned-id set itself (sPinnedIds) is independent of these
    // pointers and survives across the rebuild — that's the whole
    // point of storing pins by id rather than by row or item pointer.
    iconView->lastSPressItem = nullptr;


    //Get the list of clusters with their color
    ItemColors& clusterColors = doc->clusterColors();
    int nbClusters = clusterColors.numberOfItems();

    //Construct one icon for each cluster.  The icon is a solid 12x12 colour
    //swatch; cache it per colour so a session with thousands of clusters (which
    //cycle a small fixed colour palette) builds a handful of pixmaps instead of
    //a QPixmap + QPainter per cluster.  (The old code filled with backgroundColor
    //then immediately overpainted the whole 12x12 with the cluster colour, so the
    //swatch was always a solid cluster colour.)
    QHash<QRgb, QPixmap> swatchCache;

    for(int i = 0; i<nbClusters; ++i){
        const QColor swatchColor = clusterColors.color(i,ItemColors::BY_INDEX);
        const QRgb   swatchKey   = swatchColor.rgb();
        QPixmap pix;
        const auto cached = swatchCache.constFind(swatchKey);
        if(cached != swatchCache.constEnd()){
            pix = *cached;
        } else {
            pix = QPixmap(12,12);
            pix.fill(swatchColor);
            swatchCache.insert(swatchKey, pix);
        }
        const int curId = clusterColors.itemId(i);
        QString clusterText = QString::fromLatin1("%1").arg(curId);

        if(isInUserClusterInfoMode){
            if(curId == 0){
                clusterText.append(" - ").append("artefact");
            } else if(curId == 1){
                clusterText.append(" - ").append("noise");
            } else{
                QList<QString> clusterInformation;
                doc->data().getUserClusterInformation(curId,clusterInformation);

                if(!clusterInformation.at(0).isEmpty()){
                    clusterText.append(" - ").append(clusterInformation.at(0));
                }
                if(!clusterInformation.at(1).isEmpty()){
                    clusterText.append(", ").append(clusterInformation.at(1));
                }
                if(!clusterInformation.at(2).isEmpty()){
                    clusterText.append(", ").append(clusterInformation.at(2));
                }
                if(!clusterInformation.at(3).isEmpty()){
                    clusterText.append(", ").append(clusterInformation.at(3));
                }
                if(!clusterInformation.at(4).isEmpty()){
                    clusterText.append(", ").append(clusterInformation.at(4));
                }
            }
            QListWidgetItem * item  = new QListWidgetItem(pix, clusterText, iconView);
            item->setData(INDEX,i);
            item->setData(CLUSTER_ID, curId);
        }
        else{
            QListWidgetItem * item  = new QListWidgetItem(pix, clusterText, iconView);
            item->setData(INDEX,i);
            item->setData(CLUSTER_ID, curId);
        }
    }

    // Restore S-pinned visual selection across the rebuild.  Any
    // pinned cluster id whose cluster no longer exists (e.g. it was
    // merged into another via group) is silently dropped from the set
    // — pinning a cluster that's gone is meaningless.
    if (!iconView->sPinnedIds.isEmpty()) {
        QSet<int> stillPresent;
        for (int row = 0; row < iconView->count(); ++row) {
            QListWidgetItem* it = iconView->item(row);
            if (!it) continue;
            const int cid = it->data(CLUSTER_ID).toInt();
            if (iconView->sPinnedIds.contains(cid)) {
                it->setSelected(true);
                stillPresent.insert(cid);
            }
        }
        iconView->sPinnedIds = stillPresent;
    }
}

void ClusterPalette::slotCustomContextMenuRequested(const QPoint& pos) {
    QListWidgetItem *item = iconView->itemAt(pos);

    if ( !item ) return; // right pressed on viewport,pix
    else{
        ////If several options are available a poppupmenu can be added////

        int clusterNumber = doc->clusterColors().itemId(item->data(INDEX).toInt());

        //The dialog is not shown for the Noise and arterfact clusters (1 and 0).
        if(clusterNumber != 0 && clusterNumber != 1){
            ClusterInformationDialog *clusterInformationDialog = new ClusterInformationDialog();
            //initizialize the dialog with the previous information
            QList<QString> clusterInformation;
            doc->data().getUserClusterInformation(clusterNumber,clusterInformation);
            clusterInformationDialog->setStructure(clusterInformation.at(0));
            clusterInformationDialog->setType(clusterInformation.at(1));
            clusterInformationDialog->setId(clusterInformation.at(2));
            clusterInformationDialog->setQuality(clusterInformation.at(3));
            clusterInformationDialog->setNotes(clusterInformation.at(4));

            if(clusterInformationDialog->exec() == QDialog::Accepted)
            {
                //Update the cluster user information.
                doc->data().setUserClusterInformation(doc->clusterColors().itemId(item->data(INDEX).toInt()),clusterInformationDialog->getStructure(),clusterInformationDialog->getType(),clusterInformationDialog->getId(),clusterInformationDialog->getQuality(),clusterInformationDialog->getNotes());

                //update the text of the item
                if(isInUserClusterInfoMode){
                    item->setText(QString::fromLatin1("%1").arg(clusterNumber) + ": " + clusterInformationDialog->getStructure()+ ",  " + clusterInformationDialog->getType() + ", " + clusterInformationDialog->getId() + ", " + clusterInformationDialog->getQuality() + ", " + clusterInformationDialog->getNotes());

                    QString clusterText = QString::number(clusterNumber);
                    bool first = true;

                    if( !clusterInformationDialog->getStructure().isEmpty()){
                        clusterText.append(" - ").append( clusterInformationDialog->getStructure());
                        first = false;
                    }
                    if(!clusterInformationDialog->getType().isEmpty()){
                        if(!first){
                            clusterText.append(", ").append(clusterInformationDialog->getType());
                        }
                        else{
                            clusterText.append(" - ").append(clusterInformationDialog->getType());
                            first = false;
                        }
                    }
                    if(!clusterInformationDialog->getId().isEmpty()){
                        if(!first){
                            clusterText.append(", ").append(clusterInformationDialog->getId());
                        }
                        else{
                            clusterText.append(" - ").append(clusterInformationDialog->getId());
                        }
                    }
                    if(clusterInformationDialog->getQuality() != ""){
                        if(!first){
                            clusterText.append(", ").append(clusterInformationDialog->getQuality());
                        }
                        else{
                            clusterText.append(" - ").append(clusterInformationDialog->getQuality());
                            first = false;
                        }
                    }
                    if(clusterInformationDialog->getNotes() != ""){
                        if(!first){
                            clusterText.append(", ").append(clusterInformationDialog->getNotes());
                        }
                        else{
                            clusterText.append(" - ").append(clusterInformationDialog->getNotes());
                            first = false;
                        }
                    }

                    item->setText(clusterText);
                }

                emit clusterInformationModified();
            }
            delete clusterInformationDialog;
        }
    }
}

void ClusterPalette::slotOnItem(QListWidgetItem* item){

    if ( !item ) {
        return; // right pressed on viewport
    } else {

        int clusterNumber = doc->clusterColors().itemId(item->data(INDEX).toInt());

        //The information are not shown in the statusBar for the Noise and arterfact clusters (1 and 0).
        if(clusterNumber != 0 && clusterNumber != 1){
            //Update the statusbar with the cluster information
            QList<QString> clusterInformation;
            doc->data().getUserClusterInformation(clusterNumber,clusterInformation);

            QString clusterText;
            bool first = true;

            if(!clusterInformation.at(0).isEmpty()){
                first = false;
                clusterText.append(tr("Structure: ")).append(clusterInformation.at(0));
            }
            if(!clusterInformation.at(1).isEmpty()){
                if(first){
                    clusterText.append(tr("Type: ")).append(clusterInformation.at(1));
                    first = false;
                }
                else{
                    clusterText.append(tr(", Type: ")).append(clusterInformation.at(1));
                }

            }
            if(!clusterInformation.at(2).isEmpty()){
                if(first){
                    clusterText.append(tr("ID: ")).append(clusterInformation.at(2));
                    first = false;
                }
                else{
                    clusterText.append(tr(", ID: ")).append(clusterInformation.at(2));
                }

            }
            if(!clusterInformation.at(3).isEmpty()){
                if(first){
                    clusterText.append(tr("Quality: ")).append(clusterInformation.at(3));
                    first = false;
                }
                else{
                    clusterText.append(tr(", Quality: ")).append(clusterInformation.at(3));
                }
            }
            if(!clusterInformation.at(4).isEmpty()){
                if(first){
                    first = false;
                    clusterText.append(tr("Notes: ")).append(clusterInformation.at(4));
                }
                else{
                    clusterText.append(tr(", Notes: ")).append(clusterInformation.at(4));
                }
            }

            statusBar->showMessage(clusterText);

            //item->setToolTip(tr("Structure: %1, Type: %2 , ID: %3, Quality: %4, notes: %5").arg(clusterInformation[0]).arg(clusterInformation[1]).arg(clusterInformation[2]).arg(clusterInformation[3]).arg(clusterInformation[4]));
        }
        else{
            statusBar->clearMessage();
        }
    }
}

QList<int> ClusterPalette::selectedClusters() {
    //Get the list of clusters with their color
    if(!doc) {
        return QList<int>();
    }

    // Build the set of cluster ids to report: the visually selected
    // items UNION sPinnedIds (S-pinned clusters).  Using the union
    // means even the transient intermediate signals fired during arrow
    // navigation (before we restore S-pinned highlights) return the
    // correct cluster list.
    //
    // Both sets are cluster ids, so the union is a direct insert.
    // Visual-selection ids come from each item's CLUSTER_ID data role
    // (set in updateClusterList) — stable across renumbers, unlike
    // iconView rows.
    QSet<int> reportIds;
    for (int i = 0; i < iconView->count(); ++i) {
        QListWidgetItem* it = iconView->item(i);
        if (!it) continue;
        if (it->isSelected())
            reportIds.insert(it->data(CLUSTER_ID).toInt());
    }
    for (int id : iconView->getSPinnedIds())
        reportIds.insert(id);

    QList<int> selectedClusters;
    selectedClusters.reserve(reportIds.size());
    for (int id : reportIds)
        selectedClusters.append(id);

    //Selection has just changed
    isUpToDate = false;

    return selectedClusters;
}

void ClusterPalette::renumberPinnedIds(const QMap<int,int>& oldToNew)
{
    if (iconView->sPinnedIds.isEmpty() || oldToNew.isEmpty())
        return;
    // Walk the map and replace any pinned id that's been renamed.
    // Build the new set in a temporary so we don't iterate-and-modify.
    QSet<int> renumbered;
    renumbered.reserve(iconView->sPinnedIds.size());
    for (int id : iconView->sPinnedIds) {
        if (oldToNew.contains(id))
            renumbered.insert(oldToNew.value(id));
        else
            renumbered.insert(id);
    }
    iconView->sPinnedIds = renumbered;
}

void ClusterPalette::toggleCurrentSelection(){
    QListWidgetItem *cur = iconView->currentItem();
    if (!cur) return;

    // Pin/unpin by cluster id (stable across renumbers) rather than by
    // iconView row.  Mirrors the inner S-key handler in
    // ClusterPaletteWidget::keyPressEvent.
    const int curId = cur->data(CLUSTER_ID).toInt();

    if (!iconView->sPinnedIds.contains(curId)) {
        // Not S-pinned → add it.
        cur->setSelected(true);
        iconView->sPinnedIds.insert(curId);
        iconView->lastSPressItem = cur;
    } else if (cur == iconView->lastSPressItem) {
        // S twice on same item → isolate: clear all others, keep only this.
        iconView->sPinnedIds.clear();
        for (int k = 0; k < iconView->count(); ++k)
            if (iconView->item(k) != cur) iconView->item(k)->setSelected(false);
        iconView->lastSPressItem = nullptr;
    } else {
        // S on a different already-S-pinned item → deselect it.
        cur->setSelected(false);
        iconView->sPinnedIds.remove(curId);
        iconView->lastSPressItem = cur;
    }
    slotClickRedraw();
}

void ClusterPalette::slotClickRedraw (){
    //If mode == DELAY nothing is done before a call to update
    isUpToDate = false;

    if(mode == IMMEDIATE && !isInSelectItems){
        QList<int> selection = selectedClusters();
        emit updateShownClusters(selection);
        isUpToDate = true;
    }
}


void ClusterPalette::groupClusters(){
    QList<int> selected = selectedClusters();
    //To group clusters, there must be more then one cluster !!!
    if(selected.size()>1){
        emit groupClusters(selected);
        isUpToDate = true;
    }
}

void ClusterPalette::moveClustersToNoise(){
    QList<int> selected = selectedClusters();

    if(!selected.isEmpty()){
        emit moveClustersToNoise(selected);
        isUpToDate = true;
    }
}

void ClusterPalette::moveClustersToArtefact(){
    QList<int> selected = selectedClusters();
    if(!selected.isEmpty()){
        emit moveClustersToArtefact(selected);
        isUpToDate = true;
    }
}

void ClusterPalette::updateClusters(){
    if(!isUpToDate){
        emit updateShownClusters(selectedClusters());
        isUpToDate = true;
    }
}


void ClusterPalette::changeColor(QListWidgetItem* item) {
    if(!item) {
        return;
    }
    //Get the list of clusters with their color
    ItemColors& clusterColors = doc->clusterColors();

    const int index = item->data(INDEX).toInt();

    //Get the clusterColor associated with the item
    QColor color = clusterColors.color(index,ItemColors::BY_INDEX);

    QColor result = QColorDialog::getColor(color, 0);
    if (result.isValid()) {
        //Update the clusterColor
        clusterColors.setColor(index,result,ItemColors::BY_INDEX);

        if(mode == IMMEDIATE){
            //Update the icon
            QPixmap pixmap(12,12);
            QPainter painter;
            painter.begin(&pixmap);
            painter.fillRect(0,0,12,12,result);
            painter.end();
            item->setIcon(QIcon(pixmap));

            //As soon a color changes a signal is emitted
            emit singleChangeColor(clusterColors.itemId(index));
        }
        else{
            //If mode several selection before update (DELAY) => update the change status
            //The color change will be effective on the next call to either Update, Group or Delete
            clusterColors.setColorChanged(index,true,ItemColors::BY_INDEX);
            clusterColors.setColorChanged(true);

            //The view is no more up to date
            isUpToDate = false;
        }
    }
}

/*
 *  Sets the strings of the subwidgets using the current
 *  language.
 */
void ClusterPalette::languageChange()
{
    setWindowTitle( tr( "Cluster palette" ) );
}

void ClusterPalette::selectItems(const QList<int>& selectedClusters){
    //Set isInSelectItems to true to prevent the emission of signals due to selectionChange
    isInSelectItems = true;

    //unselect all the items first
    iconView->clearSelection();

    // Build cluster-id -> item map once (O(n)), then resolve each requested
    // cluster in O(1).  This replaces findItems(QString::number(id),
    // MatchStartsWith), which was O(n) per selected cluster (O(k*n) overall,
    // noticeable at thousands of clusters) and matched by label *prefix* — so
    // selecting id 10 could land on "100 - noise" / "1000 …".  We match the
    // exact CLUSTER_ID data role instead.
    QHash<int, QListWidgetItem*> itemById;
    itemById.reserve(iconView->count());
    for(int k = 0; k < iconView->count(); ++k){
        QListWidgetItem* it = iconView->item(k);
        itemById.insert(it->data(ClusterPalette::CLUSTER_ID).toInt(), it);
    }

    QListWidgetItem *item = nullptr;
    for(int clusterId : selectedClusters){
        const auto found = itemById.constFind(clusterId);
        if(found != itemById.constEnd()){
            (*found)->setSelected(true);
            item = *found;
        }
    }
    //Last item in selection gets focus if it exists
    if(item)
        iconView->setCurrentItem(item);

    //reset isInSelectItems to false to enable again the the emission of signals due to selectionChange
    isInSelectItems = false;

    isUpToDate = true;
    slotClickRedraw();
}

void ClusterPalette::reset(){
    iconView->clear();
    doc = 0L;
    mode = IMMEDIATE;
    isInSelectItems = false;
    isUpToDate = true;
}

void ClusterPalette::showUserClusterInformation(int electrodeGroupId){
    //update the flag
    isInUserClusterInfoMode = true;

    iconView->setViewMode(QListView::ListMode);
    iconView->setGridSize(QSize(2500,iconView->gridSize().height()));

    QMap<int,ClusterUserInformation> clusterUserInformationMap = QMap<int,ClusterUserInformation>();
    doc->data().getClusterUserInformation(electrodeGroupId,clusterUserInformationMap);

    ItemColors& clusterColors = doc->clusterColors();
    int clusterId;
    ClusterUserInformation currentClusterInformation;

    for(int i =0; i< iconView->count() ; ++i) {
        clusterId = clusterColors.itemId(i);

        QString clusterText = iconView->item(i)->text();

        if(clusterId == 0){
            clusterText.append(" - ").append("artefact");
        }
        else if(clusterId == 1){
            clusterText.append(" - ").append("noise");
        }
        else{
            currentClusterInformation = clusterUserInformationMap[clusterId];
            bool first = true;

            if(currentClusterInformation.getStructure() != ""){
                first = false;
                clusterText.append(" - ").append(currentClusterInformation.getStructure());
            }
            if(currentClusterInformation.getType() != ""){
                if(!first){
                    clusterText.append(", ").append(currentClusterInformation.getType());
                }
                else{
                    clusterText.append(" - ").append(currentClusterInformation.getType());
                    first = false;
                }
            }
            if(currentClusterInformation.getId() != ""){
                if(!first){
                    clusterText.append(", ").append(currentClusterInformation.getId());
                }
                else{
                    clusterText.append(" - ").append(currentClusterInformation.getId());
                    first = false;
                }
            }
            if(currentClusterInformation.getQuality() != ""){
                if(!first){
                    clusterText.append(", ").append(currentClusterInformation.getQuality());
                }
                else{
                    clusterText.append(" - ").append(currentClusterInformation.getQuality());
                    first = false;
                }
            }
            if(currentClusterInformation.getNotes() != ""){
                if(!first){
                    clusterText.append(", ").append(currentClusterInformation.getNotes());
                }
                else{
                    clusterText.append(" - ").append(currentClusterInformation.getNotes());
                    first = false;
                }
            }
        }
        iconView->item(i)->setText(clusterText);
    }
    iconView->setWordWrap(false);
}

void ClusterPalette::hideUserClusterInformation(){
    //update the flag
    isInUserClusterInfoMode = false;

    iconView->setViewMode(QListView::IconMode);
    //Let's go back to normal
    QFontInfo fontInfo = QFontInfo(QFont());
    iconView->setGridSize(QSize(fontInfo.pixelSize() * 2,15*2));
    //iconView->arrangeItemsInGrid();

    ItemColors& clusterColors = doc->clusterColors();
    int clusterId;

    for(int i = 0; i < iconView->count(); ++i) {
        clusterId = clusterColors.itemId(i);
        iconView->item(i)->setText(QString::number(clusterId));
    }

    iconView->setWordWrap(true);
    iconView->resize(this->width(),this->height());
}


void ClusterPalette::setFocusToList()
{
    if (iconView)
        iconView->setFocus(Qt::OtherFocusReason);
}

void ClusterPalette::changeBackgroundColor(const QColor& color){
    backgroundColor = color;
    int h;
    int s;
    int v;
    color.getHsv(&h,&s,&v);
    QPalette palette;
    QColor legendColor;
    if(s <= 80 && v >= 240 || (s <= 40 && v >= 220))
        legendColor = Qt::black;
    else
        legendColor = Qt::white;
    palette.setColor(QPalette::Text, legendColor);
    palette.setColor(QPalette::HighlightedText, legendColor);

    palette.setColor(iconView->backgroundRole(), color);

    iconView->setPalette(palette);

    //get the list of selected clusters
    QList<int> selected = selectedClusters();

    //Set isInSelectItems to true to prevent the emission of signals due to selectionChange
    isInSelectItems = true;

    //Redraw the icons
    updateClusterList();

    //reselect the clusters
    selectItems(selected);

    //reset isInSelectItems to false to enable again the the emission of signals due to selectionChange
    isInSelectItems = false;

    update();
}



