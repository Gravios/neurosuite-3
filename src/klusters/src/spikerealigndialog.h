/***************************************************************************
 * spikerealigndialog.h
 *
 * Dialog that shows the waveforms of a single cluster and lets the user
 * trigger an automatic re-alignment followed by re-extraction and
 * re-featurization of misaligned spikes.
 *
 * Workflow (all heavy I/O is done in KlustersDoc::realignSpikes):
 *   1. User opens the dialog and selects a cluster (or the active cluster
 *      is pre-selected).
 *   2. The dialog displays a summary of the cluster waveforms using existing
 *      waveform data already loaded in memory.
 *   3. User clicks "Realign & Update" — the dialog delegates to
 *      KlustersDoc::realignSpikes(), which:
 *        a. Reads each spike of the cluster from the .spk.N file.
 *        b. Finds the true peak sample with sub-sample precision.
 *        c. Computes the required shift in samples.
 *        d. Writes the shifted spike back to the .spk.N file.
 *        e. Updates the .res.N file with the new timestamp.
 *        f. If the new timestamp changes the sort order, swaps the affected
 *           entries in .res.N, .spk.N, .clu.N, and .fet.N.
 *        g. Re-featurizes the shifted spikes via process_refeaturize and
 *           patches the updated rows into the .fet.N file.
 *        h. Updates the in-memory features array and spikesByCluster table.
 *   4. The dialog reports success/failure and closes.
 ***************************************************************************/

#pragma once

#include <QDialog>
#include <QList>
#include <QString>

class QLabel;
class QSpinBox;
class QProgressBar;
class QPushButton;
class QTextEdit;
class KlustersDoc;

class SpikeRealignDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @param doc       The active document.
     * @param clusterId The cluster to realign.  Pass -1 to use the first
     *                  currently shown cluster.
     * @param parent    Parent widget.
     */
    explicit SpikeRealignDialog(KlustersDoc& doc, int clusterId, QWidget* parent = nullptr);
    ~SpikeRealignDialog() override;

private slots:
    void slotRealign();
    void slotClose();

private:
    void updateInfo();

    KlustersDoc&  m_doc;
    int           m_clusterId;

    QLabel*       m_clusterLabel;
    QLabel*       m_spikeCountLabel;
    QLabel*       m_pcaFileLabel;
    QTextEdit*    m_logEdit;
    QPushButton*  m_realignBtn;
    QPushButton*  m_closeBtn;
};
