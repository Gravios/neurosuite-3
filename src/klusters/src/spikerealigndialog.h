/***************************************************************************
 * spikerealigndialog.h
 *
 * Pre-flight configuration panel for spike realignment.
 *
 * This dialog lets the user review the cluster to be realigned, confirm
 * that the .pca.N eigenvector file exists, and click "Start Realignment"
 * to launch the operation.  It does NOT run the realignment itself —
 * that is done by RealignWorker on a background thread, with output
 * streamed to a ProcessWidget tab in the main window (matching the
 * reclustering UI pattern exactly).
 *
 * KlustersApp::slotRealignSpikes() creates this dialog, connects its
 * accepted() signal to the async launch logic, and shows it.
 ***************************************************************************/

#pragma once

#include <QDialog>

class QLabel;
class QPushButton;
class KlustersDoc;

class SpikeRealignDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param doc       Active document.
     * @param clusterId Cluster to be realigned (must be > 1).
     * @param parent    Parent widget.
     */
    explicit SpikeRealignDialog(KlustersDoc& doc, int clusterId,
                                const QString& args = QString(),
                                QWidget* parent = nullptr);
    ~SpikeRealignDialog() override;

    /** Cluster ID selected for realignment. */
    int getClusterId() const { return clusterId; }

    /** patch82 — args string with --pca-refine appended/removed based on
     *  the new checkbox state.  Callers should use this instead of the
     *  args passed in to the constructor when building the RealignWorker,
     *  so the user's choice in the dialog takes effect.
     */
    QString finalArgs() const;

private:
    void buildUi();

    KlustersDoc& doc;
    int          clusterId;
    QString      args;

    QLabel*      spikeCountLabel;
    QLabel*      pcaFileLabel;
    QLabel*      backendLabel;
    QPushButton* startBtn;
    class QCheckBox* pcaRefineCheck{nullptr};  // patch82
};
