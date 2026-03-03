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
            // m_sRows highlights so S-pinned clusters stay visually selected.
            // selectedClusters() now unions visual + m_sRows so every
            // itemSelectionChanged fired here returns the correct display set.
            clearSelection();
            setCurrentRow(targetRow);
            for (int k : std::as_const(m_sRows))
                if (k < count()) item(k)->setSelected(true);
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
            //
            // Strategy: among all items whose vertical centre is strictly
            // above (goUp) or below (goDown) the centre of the current item,
            // pick the one whose horizontal centre is closest to ours.  When
            // there are ties on the closest-x axis (two items equally near our
            // column), prefer the one whose top/bottom edge is nearest — i.e.
            // the item in the immediately adjacent row.
            const QRect cur = visualRect(indexFromItem(c));
            const int   cx  = cur.center().x();
            const int   cy  = cur.center().y();

            int bestRow  = -1;
            int bestDx   = INT_MAX;   // horizontal distance to our column
            int bestDy   = INT_MAX;   // vertical distance to adjacent row

            for (int k = 0; k < count(); ++k) {
                const QRect r = visualRect(model()->index(k, 0));
                const int   ky = r.center().y();
                const bool  qualifies = goDown ? (ky > cy) : (ky < cy);
                if (!qualifies) continue;

                const int dx = qAbs(r.center().x() - cx);
                const int dy = qAbs(ky - cy);

                // Prefer smaller dx (same column); break ties by smaller dy
                // (immediately adjacent row rather than two rows away).
                if (dx < bestDx || (dx == bestDx && dy < bestDy)) {
                    bestDx  = dx;
                    bestDy  = dy;
                    bestRow = k;
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

        const int curRow = row(cur);

        if (!m_sRows.contains(curRow)) {
            // Not S-selected yet → add it.
            cur->setSelected(true);
            m_sRows.insert(curRow);
            lastSPressItem = cur;
        } else if (cur == lastSPressItem) {
            // S pressed a second time on the same item → isolate: clear all
            // other S-rows and deselect them visually, keep only this one.
            m_sRows.clear();
            for (int k = 0; k < count(); ++k)
                if (item(k) != cur) item(k)->setSelected(false);
            // Current item stays selected (it was already); m_sRows is now empty
            // so next arrow navigation resumes single-select behaviour.
            lastSPressItem = nullptr;
        } else {
            // S on a different already-S-selected item → remove it.
            cur->setSelected(false);
            m_sRows.remove(curRow);
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

    connect(iconView,SIGNAL(itemSelectionChanged()),this, SLOT(slotClickRedraw()));
    connect(iconView,SIGNAL(customContextMenuRequested(QPoint)),this, SLOT(slotCustomContextMenuRequested(QPoint)));
    connect(iconView,SIGNAL(changeColor(QListWidgetItem*)),SLOT(changeColor(QListWidgetItem*)));
    connect(iconView,SIGNAL(onItem(QListWidgetItem*)),this, SLOT(slotOnItem(QListWidgetItem*)));
    connect(iconView,SIGNAL(paletteGainedFocus()),this, SIGNAL(paletteGainedFocus()));
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


    //Get the list of clusters with their color
    ItemColors& clusterColors = doc->clusterColors();
    int nbClusters = clusterColors.numberOfItems();

    //Construct one icon for each cluster
    QPainter painter;

    for(int i = 0; i<nbClusters; ++i){
        QPixmap pix(12,12);
        pix.fill(backgroundColor);
        painter.begin(&pix);
        painter.fillRect(0,0,12,12,clusterColors.color(i,ItemColors::BY_INDEX));
        painter.end();
        QString clusterText = QString::fromLatin1("%1").arg(clusterColors.itemId(i));

        if(isInUserClusterInfoMode){
            if(clusterColors.itemId(i) == 0){
                clusterText.append(" - ").append("artefact");
            } else if(clusterColors.itemId(i) == 1){
                clusterText.append(" - ").append("noise");
            } else{
                QList<QString> clusterInformation;
                doc->data().getUserClusterInformation(clusterColors.itemId(i),clusterInformation);

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
        }
        else{
            QListWidgetItem * item  = new QListWidgetItem(pix, clusterText, iconView);
            item->setData(INDEX,i);
        }
    }
}

void ClusterPalette::slotCustomContextMenuRequested(const QPoint& pos) {
    QListWidgetItem *item = iconView->itemAt(pos);

    if ( !item ) return; // right pressed on viewport,pix
    else{
        ////If several options are available a poppupmenu can be added////
        /*  //Create a popmenu with an changeColor option
    QPopupMenu menu(iconView);

      int CHANGE_COLOR = menu.insertItem(clusterInformationtr("Change color"));
      clusterNumber
    menu.setMouseTracking( TRUE );
    int id = menu.exec( QCursor::pos());

    if(id == CHANGE_COLOR){
      changeColor(item);
    }*/

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
    ItemColors& clusterColors = doc->clusterColors();

    QList<int> selectedClusters;

    // Build the set of rows to report: the visually selected items UNION
    // m_sRows (S-pinned clusters). Using the union means even the transient
    // intermediate signals fired during arrow navigation (before we restore
    // m_sRows highlights) return the correct cluster list.
    QSet<int> reportRows;
    for (int i = 0; i < iconView->count(); ++i)
        if (iconView->item(i)->isSelected()) reportRows.insert(i);
    for (int k : iconView->getSRows()) reportRows.insert(k);

    for (int i : reportRows)
        selectedClusters.append(clusterColors.itemId(i));

    //Selection has just changed
    isUpToDate = false;

    return selectedClusters;
}

void ClusterPalette::toggleCurrentSelection(){
    QListWidgetItem *cur = iconView->currentItem();
    if (!cur) return;

    const int curRow = iconView->row(cur);

    if (!iconView->m_sRows.contains(curRow)) {
        // Not S-selected → add it.
        cur->setSelected(true);
        iconView->m_sRows.insert(curRow);
        iconView->lastSPressItem = cur;
    } else if (cur == iconView->lastSPressItem) {
        // S twice on same item → isolate: clear all others, keep only this.
        iconView->m_sRows.clear();
        for (int k = 0; k < iconView->count(); ++k)
            if (iconView->item(k) != cur) iconView->item(k)->setSelected(false);
        iconView->lastSPressItem = nullptr;
    } else {
        // S on a different already-S-selected item → deselect it.
        cur->setSelected(false);
        iconView->m_sRows.remove(curRow);
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

    //Loop on the clusters to be selected
    QListWidgetItem *item = 0;
    QList<int>::const_iterator clusterIterator;
    QList<int>::const_iterator clusterIteratorEnd(selectedClusters.constEnd());
    for(clusterIterator = selectedClusters.constBegin(); clusterIterator != clusterIteratorEnd; ++clusterIterator){
        QList<QListWidgetItem *> lst = iconView->findItems(QString::number(*clusterIterator),Qt::MatchStartsWith);
        if(!lst.isEmpty()) {
            item = lst.at(0);
            item->setSelected(true);
        }
    }
    //Last item in selection gets focus if it exists
    if(!selectedClusters.isEmpty())
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



