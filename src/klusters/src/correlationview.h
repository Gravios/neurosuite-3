/***************************************************************************
                          correlationview.h  -  description
                             -------------------
    begin                : Fri Sep 26 2003
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

#ifndef CORRELATIONVIEW_H
#define CORRELATIONVIEW_H

//include files for the application
#include "zoomwindow.h"
#include "viewwidget.h"
#include "pair.h"
#include "data.h"
#include "klustersjobpool.h"   // KlustersJobToken (shared with the jobs)

// include files for QT
#include <QPainter>
#include <QStyle>
#include <QPixmap>
#include <QTimer>
#include <QList>


#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <memory>


class KlustersDoc;
class KlustersView;
class CorrelationThread;
namespace input { class BindingRegistry; }   // registerInput (input-remapping plan)

/**
  * View displaying auto- and cross-correlations of all selected clusters.
  * Using the text boxes in the Parameter Bar, the user can modified the bin size and duration
  * used to compute the correlograms.
  * All modification request is sent directly to the KlustersDoc object and the view
  * is automatically updated via KlustersView when the correlograms have been changed.
  * If the view is active, it is also automatically updated when clusers are changed.
  *@author Lynn Hazan
  */

class CorrelationView : public ViewWidget  {
    Q_OBJECT

public:

    friend class CorrelationThread;
    
    CorrelationView(KlustersDoc& doc, KlustersView& view, const QColor &backgroundColor, QStatusBar * statusBar, QWidget* parent=nullptr, Data::ScaleMode scale = Data::MAX,
                    int binSize = 0, int correlationTimeFrame = 0, bool shoulderLine = false, const char* name=nullptr,
                    int minSize = 50, int maxSize = 4000, int windowTopLeft = -500,
                    int windowBottomRight = 1001, int border = 0);
    ~CorrelationView();

    /**Signals that the widget is about to be deleted.*/
    void willBeKilled() override;

    /**Supersedes all in-flight correlogram jobs and waits for them to retire.
     * Called by KlustersView::stopAllViewThreads() before cluster-mutating /
     * .spk.pending writes so an in-flight correlogram read can't race the
     * mutation — the synchronous quiesce contract.  Unlike willBeKilled(),
     * does NOT set goingToDie, so the view relaunches normally afterwards.
     * (CorrelationView is a ViewWidget, so without this override
     * stopAllViewThreads's ViewWidget loop hit only the empty base virtual.)*/
    void stopRunningThreads() override;
    void supersedeRunningThreads() override;

    /** Returns the size of the bins to use in the correlograms, given in miliseconds.
  *@return size of the bins.
  */
    int getBinSize()const{return binSize;}
    
    /** Returns the time frame use to compute the correlograms, given in miliseconds.
  *@return  time frame.
  */
    int getTimeWindow()const{return timeWindow;}

    /**Returns the type of scale used to present the correlation data.
  * @return type of scale.
  */
    Data::ScaleMode getScaleMode() const {return scaleMode;}

    /**Returns a boolean indicating if a doted line is drawn at the shoulder level
  * of the correlograms.
  * @return true if a line is drawn, false otherwise.
  */
    bool isShoulderLine() const {return shoulderLine;}

    /** Register this view's resolver-dispatched input (co-located; called once from
     *  KlustersApp::registerInputBindings).  Adds the `view.correlation` scope and the
     *  Ctrl+wheel amplitude-scaling commands (Overview redesign: the amplitude views scale
     *  instead of zoom/pan).  Input-remapping plan ("Keymap profiles" uses the same registry). */
    static void registerInput(input::BindingRegistry& reg);

public Q_SLOTS:

    /**Updates the view only for one cluster for which the color has been changed
  * @param clusterId cluster Id for which the color have changed.
  * @param active true if the view is the active one, false otherwise.
  */
    void singleColorUpdate(int clusterId,bool active) override;

    /**
  * Draws an additional cluster to those already shown.
  * This method aims to reduce the number of clusters to draw.
  * @param clusterId cluster Id to add to the clusters already drawn
  * @param active true if the view is the active one, false otherwise.
  */
    void addClusterToView(int clusterId,bool active) override;

    /**
  * Removes a cluster from those already shown. Which impose to redraw everything
  * @param clusterId cluster Id to remove.
  * @param active true if the view is the active one, false otherwise.
  */
    void removeClusterFromView(int clusterId,bool active) override;

    /**
  * Adds a newly created cluster to those already shown.
  * This method aims to reduce the number of clusters to draw.
  * @param fromClusters list of clusters from which the spikes of the new cluster are coming.
  * @param clusterId cluster Id to add to the clusters already drawn
  * @param active true if the view is the active one, false otherwise.
 */
    void addNewClusterToView(QList<int>& fromClusters,int clusterId,bool active) override;

    /**
  * Adds a newly created cluster to those already shown.
  * This method aims to reduce the number of clusters to draw.
  * @param clusterId cluster Id to add to the clusters already drawn
  * @param active true if the view is the active one, false otherwise.
  */
    void addNewClusterToView(int clusterId,bool active) override {addClusterToView(clusterId,active);}

    /**
  * Updates the content of the widget due to the removal of spikes in a cluster.
  * This method aims to reduce the number of clusters to draw.
  * @param fromClusters list of clusters from which the spikes have been taken.
  * @param active true if the view is the active one, false otherwise.
  */
    void spikesRemovedFromClusters(QList<int>& fromClusters,bool active) override;

    /**
  * Updates the content of the widget due to the addition of spikes in a cluster.
  * This method aims to reduce the number of clusters to draw.
  * @param clusterId cluster Id to which the spikes have been added
  * @param active true if the view is the active one, false otherwise.
  */
    void spikesAddedToCluster(int clusterId,bool active) override;

    /**Changes the current mode, call by a selection of a tool
  * @param selectedMode new mode of drawing (selection or zoom)
  */
    void setMode(BaseFrame::Mode selectedMode) override {}

    /**Sets the mode of presentation to raw mode, meaning that the value of each bin in a
  * correlogram will be the computed value.
  */
    void setNoScale();

    /**Sets the mode of presentation to raw mode, meaning that the value of each bin in a
  * correlogram will be the computed value scale by the maximum value.
  */
    void setMaximumScale();

    /**Sets the mode of presentation to raw mode, meaning that the value of each bin in a
  * correlogram will be the computed value scale by the shoulder value.
  */
    void setShoulderScale();

    /**Changes the size of the bins and the size of the time frame used to compute the correlograms.*/
    void setBinSizeAndTimeWindow(int size,int width);

    /**Increase of the amplitude of the correlograms.
  */
    void increaseAmplitude();

    /**Decrease of the amplitude of the correlograms.
  */
    void decreaseAmplitude();

    /**Enables the caller to know if there is any thread running launch by the view.*/
    bool isThreadsRunning() const override;

    /**
  * Update the clusters which have been modified by the suppression of spikes
  * (used to create a new cluster or simply move to the cluster of noise or artefact).
  * This method is call only during an undo otherwise the updateClusters is call.
  * There are 2 functions in order to reduce the number of clusters to draw whenever possible.
  * @param modifiedClusters list of clusters from which spikes were taken from.
  * @param active true if the view is the active one, false otherwise.
  */
    void undoUpdateClusters(QList<int>& modifiedClusters,bool active) override {
        spikesRemovedFromClusters(modifiedClusters,active);
    }

    /**
  * Update the clusters which have been modified by the suppression of spikes
  * (used to create a new cluster or simply move to the cluster of noise or artefact).
  * This method aims to reduce the number of clusters to draw.
  * @param modifiedClusters list of clusters from which spikes were taken from.
  * @param active true if the view is the active one, false otherwise.
  * @param isModifiedByDeletion true if the clusters of @p modifiedClusters have been modified
  * by the deletion of spikes (moved to cluster 0 or 1, cluster of artefact and cluster of noise respectively).
  */
    void updateClusters(QList<int>& modifiedClusters,bool active,bool isModifiedByDeletion) override {
        spikesRemovedFromClusters(modifiedClusters,active);
    }

    /**
  * Update the presentation of a doted line at the shoulder level.
  * If @p b is true a line will be drawn, none will be drawn otherwise.
  * @param b boolean indicating if a shoulder line has to be drawn.
  */
    void setShoulderLine(bool b);

    /**Update the information presented in the view if need it.*/
    void updateDrawing() override;

    /**
 * Update the information presented in the view after a renumbering if need it.
 * @param active true if the view is the active one, false otherwise.
 */
    void clustersRenumbered(bool active);

    /**Prints the currently display information on a printer via the painter @p printPainter.
 * @param printPainter painter on a printer.
 * @param metrics object providing information about the printer.
 * @param whiteBackground true if the printed background has to be white, false otherwise.
 */
    void print(QPainter& printPainter,int width,int height, bool whiteBackground) override;

protected:
    // ── BufferedView hooks (the paint lifecycle lives in the base now) ────────
    /**Only paint once correlogram data has arrived.*/
    bool renderReady() const override { return dataReady; }
    /**Inset the viewport to leave room for the cluster-id legends (margins depend
     *  on whether the left border is in view after a zoom).*/
    QRect computeViewport() override;
    /**The buffer has always been sized a touch larger than the viewport.*/
    int bufferPad() const override { return 10; }
    /**Cached layer: the correlograms (REDRAW) / just the changed pairs (UPDATE).*/
    void paintBuffer(QPainter& painter, DrawContentsMode level) override;
    /**Device-space content baked into the buffer: the cluster ids.*/
    void paintBufferDeviceLayer(QPainter& painter) override;
    /**Restore the zoom cursor after every paint.*/
    void afterPaint(QPainter& widget) override;
    /**Treat the events sent by the CorrelationThread instances.
  * @param event custom event.
  */
    void customEvent (QEvent* event) override;
    /**The view responds to a double click.
  * The correlograms are retrieve in case the data have changed (an other view has changed its parameters)
  * as all the views are sharing the same data.
  * @param event mouse event.
  */
    void mouseDoubleClickEvent (QMouseEvent* event) override;

    /**The view responds to a mouse click.
  * The correlograms are retrieve in case the data have changed (an other view has chaneg its parameters.
  * @param event mouse release event.
  */
    void mouseReleaseEvent(QMouseEvent* event) override;
    
    /**The view responds to a resize event.
  * The correlograms are retrieve in case the data have changed (an other view has changed its parameters)
  * as all the views are sharing the same data.
  * @param event resize event.
  */
    void resizeEvent(QResizeEvent* event) override;

    /**The view responds to a mouse move event.
  * The time is display in the status bar.
  * @param event mouse move event.
  */
    void mouseMoveEvent(QMouseEvent* event) override;
    /**The view responds to a wheel event: Ctrl+wheel scales the correlogram amplitude (the
  * registry correlation.scaleUp / scaleDown commands); a plain wheel defers to the base.*/
    void wheelEvent(QWheelEvent* event) override;
    /**Return to the default amplitude for the current scale mode, dropping any manual
  * Ctrl+wheel / +- scaling — i.e. the default auto scale in the usual scale-by-maximum
  * mode.  Called when the displayed cluster selection changes so a new selection draws
  * auto-scaled instead of inheriting the previous one's manual amplitude.*/
    void resetToDefaultScale();

private:

    /**Type of scale used, raw, scale by the maximum, or scale by the shoulder.
  * The default is raw mode.*/
    Data::ScaleMode scaleMode;

    /**Creates a job which will get the correlations information for
  * the pairs of clusters contained in @p pairsToCompute due to the clusters in @p clusterIds.
  * Creating it launches the request on the shared worker pool, which owns and
  * deletes it after it runs.
  * @param pairsToCompute couple of clusters for which a correlogram has to be obtained.
  * @param clusterIds clusters for which the correlograms will be computed.
  */
    CorrelationThread* getCorrelations(const QList<Pair>& pairsToCompute, const QList<int> &clusterIds);

    /**
 * Draws the correlograms of the pair of clusters in the list @p pairList on the given painter.
 * @param painter painter on which to draw the correlograms.
 * @param pairList list of pair of clusters for which a correlogram has to be drawn.
 */
    void drawCorrelograms(QPainter& painter,QList<Pair>& pairList);

    /**Updates the dimension of the window.*/
    void updateWindow();

    /**
 * Asks the correlograms for all the clusters currently shown by launching a CorrelationThread.
 */
    void askForCorrelograms();

    /**Draws the clusters identifiers.
 * @param painter painter on which to draw the information.
 */
    void drawClusterIds(QPainter& painter);

    //Members

    /**Cancellation/completion state shared with the correlogram jobs this
    * view enqueues on the worker pool.  Replaces the threadsToBeKill
    * ownership list (jobs are owned and deleted by the pool).*/
    std::shared_ptr<KlustersJobToken> jobToken;

    /**True if the correlation information needed to draw the correlograms are available.*/
    bool dataReady;

    /**List of pairs of clusters for which a correlogram has to be drawn.*/
    QList<Pair> pairs;

    /**size of the bins to use in the correlograms, given in miliseconds */
    int binSize;

    /**Time frame use to compute the correlograms, given in miliseconds.*/
    int timeWindow;

    /**Minimal abscissa in window coordinate*/
    long abscissaMin;

    /**Maximal abscissa in window coordinate*/
    long abscissaMax;

    /**Minimal ordinate in window coordinate*/
    long ordinateMin;

    /**Maximal ordinate in window coordinate*/
    long ordinateMax;

    /**The width border*/
    uint widthBorder;

    /**The height border*/
    uint heightBorder;

    /**The width of a bins.*/
    int binWidth;

    /**Abscissa space between two correlograms.*/
    uint Xspace;

    /**Ordinate space between two correlograms*/
    uint Yspace;

    /**Size in pixels corresponding to the maximale value of a correlogram.*/
    uint YsizeForMaxAmp;

    /**Factor use to calculate the ordinate value to been drawn.
 * The factor equals YsizeForMaxAmp multipled by a zoom factor.
 */
    float Yfactor;

    /**The delta between the starting abscisses of two correlograms.*/
    int shift;

    /**Number of bins per correlogram (2k + 1).*/
    int nbBins;

    /**List of pairs corresponding to clusters to update.*/
    QList<Pair> pairUpdateList;

    /**Boolean indicating if a shoulder line has to be drawn on the correlograms.*/
    bool shoulderLine;

    /**Step in pixels between two tick marks.*/
    float tickMarkStep;

    /**Number of tick marks in a time window*/
    int nbTickMarks;

    /**Abscissa of the tick mark for the center bin (time = 0)*/
    int tickMarkZero;

    /**Map of firing rate for the autocorrelograms.*/
    QMap<int,QString> firingRates;

    /**True if the view has been zoomed, false ohterwise.*/
    bool isZoomed;

    /**True if the widget is about to be deleted, false otherwise.*/
    bool goingToDie;

    /**Border on the left and right sides inside the window (QRect corresponding
 * to the part of the drawing which will actually be drawn onto the widget).*/
    static const int XMARGIN;

    /**Border on the top and bottom sides inside the window (QRect corresponding
 * to the part of the drawing which will actually be drawn onto the widget).*/
    static const int YMARGIN;

    /*True if the view is currently been printed, false otherwise.**/
    bool printState;

    /**The region representing the area to print.*/
    QRegion printRegion;

};

#endif
