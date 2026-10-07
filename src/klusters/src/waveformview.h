/***************************************************************************
                          waveformview.h  -  description
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

#ifndef WAVEFORMVIEW_H
#define WAVEFORMVIEW_H

// include files for QT
#include <QPainter>
#include <QStyle>
#include <QPixmap>
#include <QList>

#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <memory>

//include files for the application
#include "zoomwindow.h"
#include "viewwidget.h"
#include "klustersjobpool.h"   // KlustersJobToken (shared with the jobs)



// forward declaration
class KlustersDoc;
class KlustersView;
class WaveformThread;
namespace input { class BindingRegistry; }   // registerInput (input-remapping plan)

/**
  * View displaying the waveforms of a subset of the spikes evenly
  * spaced in time over the whole recording.
  * Using the spin boxes in the Parameter Bar, the user can specify which spikes to use.
  * All modification request is sent directly to the KlustersDoc object and the view
  * is automatically updated via KlustersView when the waveforms have been changed.
  * If the view is active, it is also automatically updated when clusers are changed.
  *@author Lynn Hazan
  */

class WaveformView : public ViewWidget  {
    Q_OBJECT

public:

    friend class WaveformThread;

    WaveformView(KlustersDoc& doc, KlustersView& view, const QColor &backgroundColor, int acquisitionGain, const QList<int> &positions, QStatusBar * statusBar, QWidget* parent=nullptr,
                 bool isTimeFrameMode = false, long start = 0, long timeFrameWidth = 0, long nbSpkToDisplay =0, bool overLay = false, bool mean = false,
                 const char* name=nullptr, int minSize = 50, int maxSize = 4000, int windowTopLeft = -500,
                 int windowBottomRight = 1001, int border = 0);
    ~WaveformView();

    /**
  * String indicating in which presentation mode the user is (sample, time frame).
  */
    enum PresentationMode{SAMPLE=1,TIME_FRAME=2};

    /**Signals that the widget is about to be deleted.*/
    void willBeKilled() override;
    /**Supersedes all in-flight waveform jobs and waits for them to retire,
     * without setting goingToDie.  Synchronous quiesce with ZERO live
     * callers: the view's own relaunch resets use the non-blocking
     * supersedeRunningThreads() (jobs read their captured snapshot, so no
     * wait is owed), leaving this as the view's member of the documented
     * blocking family, reachable only through the stopRunningThreads()
     * override that the zero-caller KlustersView::stopAllViewThreads()
     * would invoke. */
    void stopAndClearThreads();

    // ── Input-binding seam (input-remapping plan) ────────────────────────────
    /** Register WaveformView's resolver-dispatched mouse gesture into the one app-wide
     *  registry (called once from KlustersApp::registerInputBindings).  The press trigger
     *  (Ctrl+Left channel pick) is bound here; the toggle body stays in mouseReleaseEvent
     *  and the batch commit on Ctrl-release stays in eventFilter — the plan's "seam". */
    static void registerInput(input::BindingRegistry& reg);
    /** Arm the Ctrl+Left channel pick (the body — channelAtWorldY + the pendingChannelSelection
     *  toggle — runs in mouseReleaseEvent, and the commit on Ctrl-release in eventFilter).
     *  Invoked by the waveform.channelPick Gesture command; was the inline Ctrl+Left press
     *  swallow.  Arming consumes the press so the base never starts a zoom rubber band. */
    void beginChannelPick(){ channelPickArmed = true; }

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
    inline void addNewClusterToView(int clusterId,bool active) override {addClusterToView(clusterId,active);}

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
    inline void setMode(BaseFrame::Mode selectedMode) override {}

    /**
  * Updates the clusters which have been modified by the suppression of spikes
  * (used to create a new cluster or simply move to the cluster of noise or artefact).
  * This method aims to reduce the number of clusters to draw.
  * @param modifiedClusters list of clusters from which spikes were taken from.
  * @param active true if the view is the active one, false otherwise.
  * @param isModifiedByDeletion true if the clusters of @p modifiedClusters have been modified
  * by the deletion of spikes (moved to cluster 0 or 1, cluster of artefact and cluster of noise respectively).
  */
    inline void updateClusters(QList<int>& modifiedClusters,bool active,bool isModifiedByDeletion) override {
        spikesRemovedFromClusters(modifiedClusters,active);
    }

    /**
  * Updates the clusters which have been modified by the suppression of spikes
  * (used to create a new cluster or simply move to the cluster of noise or artefact).
  * This method is call only during an undo otherwise the updateClusters is call.
  * There are 2 functions in order to reduce the number of clusters to draw whenever possible.
  * @param modifiedClusters list of clusters from which spikes were taken from.
  * @param active true if the view is the active one, false otherwise.
  */
    inline void undoUpdateClusters(QList<int>& modifiedClusters,bool active) override {
        spikesRemovedFromClusters(modifiedClusters,active);
    }

    /** Sets the way of presenting the information concerning the waveforms selected to
  * only show the waveforms of the mean and the standard deviation.
  */
    void setMeanPresentation();

    /** Sets the way of presenting the information concerning the waveforms selected to
  * show all the waveforms corresponding to the mode of presentation.
  */
    void setAllWaveformsPresentation();

    /** The waveforms of each cluster are overlaying.
  */
    void setOverLayPresentation();

    /** The waveforms of each cluster are presented side by side.
  */
    void setSideBySidePresentation();

    /**Sets the mode of presentation to sample mode, meaning that, for each shown cluster,
  * only one out of the number of spikes to be displayed will be shown.
  */
    void setSampleMode();

    /**Sets the mode of presentation to time frame mode, meaning that, for each shown cluster,
  * only the spikes within the current time frame will be shown.
  */
    void setTimeFrameMode();

    /**Changes the time and update the view accordingly.
  * @param start start time of the time frame in second.
  * @param width width of the time frame in second.
  */
    void setTimeFrame(long start, long width);

    /**Sets the initial maximum amplitude of the waveforms.
  * @param gain the new value to set the initial maximum amplitude of the waveforms (before increase or decrease).
 */
    inline void setGain(int gain){
        this->acquisitionGain = gain;
        this->gain = 0;
        Yfactor = static_cast<float>(YsizeForMaxAmp)/static_cast<float>(acquisitionGain);

        //Everything has to be redraw
        drawContentsMode = REDRAW ;
    }

    /**Increase of the amplitude of the waveforms.
  */
    void increaseAmplitude();
    /** Adjusts gain so the tallest loaded waveform spans ~75 %
     *  of the channel slot height. Safe to call before data loads
     *  (no-op if no spikes are available yet). */
    void autoFitAmplitude();

    /**Decrease of the amplitude of the waveforms.
  */
    void decreaseAmplitude();

    /**Increase of the number of samples display in the sample mode.
  */
    void setDisplayNbSpikes(long nbSpikes);

    /** Post-lasso residual preview (filled by ClusterView via KlustersView).
     *  Shows the given channel-major traces INSTEAD of the normal waveforms until
     *  cleared: the mean lassoed waveform (grey), the basis reconstruction B·ā
     *  (blue) and the mean residual x̄−B·ā (red), so a cut can be judged before it
     *  is committed.  Arrays are length nChan*nSamp (channel c, sample s at
     *  c*nSamp+s); @p fit may be empty (own-mean mode).  @p peakWave is the max
     *  |mean| across channels: the three traces are drawn at ONE common scale that
     *  makes it fill ~75% of a channel's height, so the preview is readable at any
     *  data gain while the residual still reads small relative to the mean.
     *  @p canDecollide adds the "D — decollide" line to the review panel (true only
     *  when a 2-cluster oblique basis is pinned, matching the key handler). */
    void setResidualPreview(int nChan, int nSamp,
                            const std::vector<float>& meanWave,
                            const std::vector<float>& fit,
                            const std::vector<float>& resid,
                            const QString& verdict,
                            double peakWave = 0.0,
                            bool canDecollide = false);
    /** Clear the residual preview and return to the normal waveform display. */
    void clearResidualPreview();
    bool hasResidualPreview() const { return hasResidualPreview_; }

    // ── template preview (plan §11.4) ──────────────────────────────────────────
    // Pushed by ClusterView (via KlustersView::getViewList) while the lineage
    // overlay is engaged.  `templates` is a list of raw, CHANNEL-MAJOR curves
    // (index ch*nSamp + i) — the same layout + sign convention setResidualPreview
    // uses.  `editMode` flips the roles:
    //   review (false): the spike waveforms stay in front, the template(s) drawn as
    //     a faint underlay behind them;
    //   edit   (true):  the spike waveforms stay visible and the template(s) are
    //     drawn ON TOP as a translucent overlay, with cluster `bandCluster`'s
    //     mean±std as a grey reference band.
    // `scaleAbsolute` true draws at the data's gain (Yfactor); false (best-fit)
    // normalises each template's peak to ~75% of a channel's height.
    // `stds`, when non-empty, is parallel to `templates` (same CHANNEL-MAJOR layout)
    // and makes each template draw as a translucent mean±std BAND (filled) with a
    // thin mean centreline, instead of a bare mean polyline — the clearest way to
    // read the node/leaf model against the actual spikes.
    void setTemplatePreview(bool editMode, int nChan, int nSamp,
                            const std::vector<std::vector<float>>& templates,
                            const std::vector<QColor>& colors,
                            int bandCluster, bool scaleAbsolute,
                            const std::vector<std::vector<float>>& stds = {});
    void clearTemplatePreview();
    bool hasTemplatePreview() const { return hasTemplatePreview_; }
    void setTemplatePreviewScaleAbsolute(bool absolute);   // best-fit <-> absolute, repaints

    /**Enables the caller to know if there is any thread running launch by the view.*/
    bool isThreadsRunning() const override;
    void stopRunningThreads() override { stopAndClearThreads(); }
    void supersedeRunningThreads() override;

    /**Update the information presented in the view if need it.*/
    void updateDrawing() override;
    /**Initialize the position of the channels in the view.
 * @param positions positions of the channels to use in the view set by the user in the settings dialog.
 */
    inline void setChannelPositions(QList<int>& positions){
        // nbchannels = positions.size(), checked in calling functions.
        channelPositions.assign(positions.begin(), positions.end());

        //Everything has to be redraw
        drawContentsMode = REDRAW;
    }

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
    /**Only paint once the first waveform data has arrived.*/
    bool renderReady() const override { return dataReady; }
    /**Cached layer: fill + draw the waveforms (REDRAW) / repaint changed clusters (UPDATE).*/
    void paintBuffer(QPainter& painter, DrawContentsMode level) override;
    /**Device-space content baked into the buffer: the pending-lasso panel and cluster ids.*/
    void paintBufferDeviceLayer(QPainter& painter) override;
    /**Restore the zoom cursor after every paint.*/
    void afterPaint(QPainter& widget) override;
    /**Treat the events sent by the WaveformThread instances*/
    void customEvent(QEvent* event) override;
    /**The view responds to a double click.
  * The waveforms are retrieve in case the data have changed (an other view has changed its parameters)
  * as all the views are sharing the same data.
  * @param event mouse event.
  */
    void mouseDoubleClickEvent (QMouseEvent* event) override;
    /**The view responds to a mouse click.
  * The waveforms are retrieve in case the data have changed (an other view has changed its parameters)
  * as all the views are sharing the same data.
  * @param event mouse release event.
  */
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    /**The view responds to a wheel event: Ctrl+wheel scales the waveform amplitude (the
  * registry waveform.scaleUp / scaleDown commands); a plain wheel defers to the base.*/
    void wheelEvent(QWheelEvent* event) override;
    /** WaveformView owns its primary Left press (the Ctrl+Left channel pick resolves at the
     *  top of mousePressEvent), so it keeps the shared rubber-band ZOOM out of its scope — a
     *  Ctrl+Left must arm the pick, not the base's any-modifier-Left zoom.  Its plain-Left
     *  zoom still works via the base's inline fall-through.  (Input-remapping plan P0d-z.) */
    bool managesOwnPrimaryPress() const override { return true; }
    /**Watches the application for the Ctrl release that commits a channel
     * selection.  The view hierarchy sets no focus policy, so it never receives
     * key events itself; filtering the application avoids having to give it
     * focus, which would disturb the Tab/arrow-key focus ring KlustersApp
     * manages.*/
    bool eventFilter(QObject* watched, QEvent* event) override;
    /**The view responds to a resize event.
  * The waveforms are retrieve in case the data have changed (an other view has changed its parameters)
  * as all the views are sharing the same data.
  * @param event resize event.
  */
    void resizeEvent(QResizeEvent* event) override;
private:
    //members

    /**The time maximum in second.*/
    long maximumTime;

    /**Mode of presentation used, spike sample or time frame. The default is sample mode.*/
    PresentationMode presentationMode;

    /**
  * Boolean indicating if all the waveforms, corresponding to the mode of presentation,
  * are shown or if only the mean and the standard deviation.
  */
    bool meanPresentation;

    /**
  * Boolean indicating if the waveforms for the presented clusters have to overlay.
  */
    bool overLayPresentation;

    /**Minimal abscissa in window coordinate*/
    long abscissaMin;

    /**Maximal abscissa in window coordinate*/
    long abscissaMax;

    /**Minimal ordinate in window coordinate*/
    long ordinateMin;

    /**Maximal ordinate in window coordinate*/
    long ordinateMax;

    /**Number of points used to describe a waveform.*/
    int nbSamplesInWaveform;

    /**Position of the peak among the points decribing the waveform.*/
    int peakPositionInWaveform;

    /**Number of electrodes used in that experiment.*/
    int nbchannels;

    /**The width border*/
    int widthBorder;

    /**The height border*/
    int heightBorder;

    /**The delta between the starting abscisses of two clusters.*/
    int shift;

    /**Abscissa step between two points of a given spike.*/
    int Xstep;

    /**Abscissa space between two clusters.*/
    int Xspace;

    /**Ordinate space between two electrodes.*/
    int Yspace;

    /**The abscissa of the system coordinate center for the channel
  * which is presented at the top of the view.*/
    long X0;

    /**The ordinate of the system coordinate center for the channel
  * which is presented at the top of the view.*/
    long Y0;

    /**Acquisition system gain.*/
    int acquisitionGain;

    /**The actual gain is 0.75 raised to the power of gain times
  * the acquisition system gain.
  */
    int gain;
    
    /**Size in pixels corresponding to the amplitude maximal.*/
    int YsizeForMaxAmp;

    /**Factor use to calculate the ordinate value to been drawn.
  * The factor equals YsizeForMaxAmp divided by gain.
  */
    float Yfactor;

    /**Index positions of the channels*/
    std::vector<int> channelPositions;

    /**Channel indices (group-local) toggled by Ctrl+click but not yet pushed to
     * the document: the selection is committed only when Ctrl is released, so a
     * multi-channel pick triggers one recompute rather than one per click.*/
    QList<int> pendingChannelSelection;
    /**True once a Ctrl+click changed pendingChannelSelection; cleared on commit.*/
    bool channelSelectionDirty = false;
    /**True between the waveform.channelPick press (beginChannelPick) and its release —
     * the release toggles the clicked channel while armed, then disarms.  Gated on this
     * flag, not the live modifiers, so a rebind of the trigger keeps working (the same
     * arm-flag release idiom as ClusterView's Ctrl-pan).*/
    bool channelPickArmed = false;

    /**Group-local channel index drawn at world ordinate @p worldY, or -1 if the
     * point falls outside every channel band.  Inverts the layout used by
     * drawWaveforms: channel j is drawn about the baseline
     * -(Y0 - channelPositions[j] * (YsizeForMaxAmp + Yspace)).*/
    int channelAtWorldY(long worldY) const;

    // ── per-channel layout geometry (Tier 3 of the paint refactor) ───────────
    // The vertical pitch between channel baselines, and a channel's baseline world
    // Y, were open-coded identically in every draw method (drawWaveforms,
    // drawChannelSelection, drawResidualPreview, drawTemplate{Band,Preview}, the
    // channel-shade pass).  Centralised here so the convention lives in one place;
    // the fallback (`chan` when it has no explicit position) matches what the
    // preview paths already did.  Callers still apply their own sign and amplitude
    // scale (which legitimately differ between the spike, residual and template paths).
    int  channelStep() const { return YsizeForMaxAmp + Yspace; }
    long channelBaselineY(int chan) const {
        const int cpos = (chan >= 0 && chan < static_cast<int>(channelPositions.size()))
                             ? channelPositions[chan] : chan;
        return Y0 - static_cast<long>(cpos) * channelStep();
    }

    /**Shade the bands of the channels in pendingChannelSelection.*/
    void drawChannelSelection(QPainter& painter);

    /**When the presentation mode is time frame, this variable keeps track of
  * the current start time of the time window.*/
    long startTime;

    /**When the presentation mode is time frame, this variable keeps track of
  * the current end time of the time window.*/
    long endTime;

    /**True is the data where recording using a 12 or 16 bits recording system which
  * gives data coded on 2 bytes, false otherwise (the recording is then assume to be 32 bits
  * and then the data are coded on 4 bytes.*/
    bool isTwoBytesRecording;
    
    /**Cancellation/completion state shared with the waveform jobs this view
    * enqueues on the worker pool.  Replaces the threadsToBeKill ownership
    * list (jobs are owned and deleted by the pool) and the view-local
    * cap-queue that bounded live loader threads: requests beyond the pool's
    * worker count now wait in the pool as inert job objects, costing no
    * thread and no file descriptors.*/
    std::shared_ptr<KlustersJobToken> jobToken;

    /**True if the waveform information needed to draw the waveforms are available.*/
    bool dataReady;

    /**List of the clusters to be disregared because they have been changed.*/
    QList<int> clustersToDisregard;

    /**The number of spikes to display in sample mode.*/
    long nbSpkToDisplay;

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

    //Functions

    /**
  * Draws the waveforms of the clusters in the list @p clustersList on the given painter
  * @param painter painter on which to draw the waveforms
  * @param clusterList list of clusters to draw
  */
    void drawWaveforms(QPainter& painter,const QList<int>& clusterList);

    /** Draw the post-lasso residual preview (setResidualPreview) in place of the
     *  normal waveforms, reusing the per-channel baseline geometry drawWaveforms
     *  and drawChannelSelection use. */
    void drawResidualPreview(QPainter& painter);

    /** Draw the review panel over the residual preview (device coordinates, after
     *  the world transform is reset): a titled box with the verdict read-out, a
     *  colour legend for the three traces, and the accept/reject/decollide keys, so
     *  the preview says what it is and how to act on it. */
    void drawResidualPreviewPanel(QPainter& painter);

    // Post-lasso residual preview overlay — set by ClusterView through
    // KlustersView, drawn instead of the cluster waveforms while present.
    bool               hasResidualPreview_ = false;
    int                rpChan_ = 0;
    int                rpSamp_ = 0;
    std::vector<float> rpMean_;
    std::vector<float> rpFit_;
    std::vector<float> rpResid_;
    QString            rpVerdict_;
    double             rpPeakWave_     = 0.0;   ///< max |mean| across channels (common trace scale)
    bool               rpCanDecollide_ = false; ///< a 2-cluster oblique basis is pinned (D enabled)

    /** Draw the committed template curves (setTemplatePreview) over the normal
     *  per-channel baseline geometry — front in edit mode, a faint underlay in
     *  review mode.  Scaled by scaleAbsolute (gain) or best-fit (peak→~75%). */
    void drawTemplatePreview(QPainter& painter);
    /** Draw cluster `tpBandCluster_`'s mean±std as a grey band underlay (edit
     *  mode only), reusing the per-channel baseline geometry. */
    void drawTemplateBand(QPainter& painter);
    /** Point the template band's grey reference at the CURRENT first shown real
     *  cluster and request its mean, so the overlay reads against the waveforms on
     *  screen now (a new cluster selected, or the current one edited). No-op without
     *  a preview. */
    void syncTemplateBandCluster();

    // Template preview overlay (plan §11.4) — set by ClusterView through
    // KlustersView while the lineage overlay is engaged.  Templates are raw,
    // CHANNEL-MAJOR (ch*nSamp + i), same sign convention as the residual
    // preview.  Review mode: underlay behind the waveforms; edit mode: in
    // front, with tpBandCluster_'s mean±std as a grey band.
    bool                            hasTemplatePreview_ = false;
    bool                            tpEdit_ = false;
    bool                            tpScaleAbsolute_ = false;
    int                             tpChan_ = 0;
    int                             tpSamp_ = 0;
    int                             tpBandCluster_ = -1;
    std::vector<std::vector<float>> tpTemplates_;
    std::vector<std::vector<float>> tpStds_;      // per-template std (±band), parallel to tpTemplates_; may be empty
    std::vector<QColor>             tpColors_;

    /**Updates the dimension of the window.*/
    void updateWindow();

    /**Creates a job which will get the waveform information (the caller
    * launches it through one of its get* methods, which enqueue it on the
    * shared worker pool; the pool owns and deletes it after it runs).*/
    WaveformThread* getWaveforms();

    /**
  * Asks the waveform information for the cluster @p clusterId by enqueueing a waveform job.
  * @param clusterId id of the cluster to ask waveform information for.
  */
    void askForWaveformInformation(int clusterId);

    /**
  * Asks the waveform information for the clusters listed in @p clusterIds by enqueueing a waveform job.
  * @param clusterIds ids of the clusters to ask waveform information for.
  */
    void askForWaveformInformation(const QList<int>& clusterIds);


    /**Draws the clusters identifiers.
  * @param painter painter on which to draw the information
  */
    void drawClusterIds(QPainter& painter);

};

#endif
