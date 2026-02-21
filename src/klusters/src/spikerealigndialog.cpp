/***************************************************************************
 * spikerealigndialog.cpp
 ***************************************************************************/

#include "spikerealigndialog.h"
#include "klustersdoc.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSpinBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QMessageBox>
#include <QFileInfo>

SpikeRealignDialog::SpikeRealignDialog(KlustersDoc& doc, int clusterId, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
    , m_clusterId(clusterId)
{
    setWindowTitle(tr("Realign Spikes — Cluster %1").arg(clusterId));
    setMinimumWidth(480);

    auto* vlay = new QVBoxLayout(this);

    // -----------------------------------------------------------------------
    // Info grid
    // -----------------------------------------------------------------------
    auto* grid = new QGridLayout;

    grid->addWidget(new QLabel(tr("Cluster:")), 0, 0);
    m_clusterLabel = new QLabel(QString::number(clusterId));
    grid->addWidget(m_clusterLabel, 0, 1);

    grid->addWidget(new QLabel(tr("Spikes in cluster:")), 1, 0);
    m_spikeCountLabel = new QLabel(tr("—"));
    grid->addWidget(m_spikeCountLabel, 1, 1);

    grid->addWidget(new QLabel(tr("PCA eigenvector file:")), 2, 0);
    m_pcaFileLabel = new QLabel(tr("—"));
    m_pcaFileLabel->setWordWrap(true);
    grid->addWidget(m_pcaFileLabel, 2, 1);

    vlay->addLayout(grid);

    // -----------------------------------------------------------------------
    // Description
    // -----------------------------------------------------------------------
    auto* descLabel = new QLabel(
        tr("<b>What this does:</b><br>"
           "For each spike in the selected cluster, the peak is located "
           "within the recorded waveform by scanning a ±½ window around the "
           "stored peak sample. If the true peak is shifted, the spike "
           "waveform is re-extracted at the correct position, the .res file "
           "is updated with the new sample timestamp, the .spk file is "
           "updated with the re-aligned waveform, and the feature values are "
           "recomputed using the saved PCA eigenvectors (.pca.N file). "
           "If a timestamp change would break the sort order of .res, the "
           "affected entries in .res, .spk, .clu and .fet are swapped "
           "atomically. The in-memory cluster table is also updated."));
    descLabel->setWordWrap(true);
    vlay->addWidget(descLabel);

    // -----------------------------------------------------------------------
    // Log output
    // -----------------------------------------------------------------------
    vlay->addWidget(new QLabel(tr("Log:")));
    m_logEdit = new QTextEdit;
    m_logEdit->setReadOnly(true);
    m_logEdit->setMinimumHeight(120);
    vlay->addWidget(m_logEdit);

    // -----------------------------------------------------------------------
    // Buttons
    // -----------------------------------------------------------------------
    auto* btnLay = new QHBoxLayout;
    m_realignBtn = new QPushButton(tr("Realign && Update"));
    m_closeBtn   = new QPushButton(tr("Close"));
    btnLay->addStretch();
    btnLay->addWidget(m_realignBtn);
    btnLay->addWidget(m_closeBtn);
    vlay->addLayout(btnLay);

    connect(m_realignBtn, &QPushButton::clicked, this, &SpikeRealignDialog::slotRealign);
    connect(m_closeBtn,   &QPushButton::clicked, this, &SpikeRealignDialog::slotClose);

    updateInfo();
}

SpikeRealignDialog::~SpikeRealignDialog() = default;

void SpikeRealignDialog::updateInfo()
{
    // Spike count
    const Data& data = m_doc.data();
    int nbSpikes = 0;
    SortableTable st;
    if (data.spikePositions(m_clusterId, st))
        nbSpikes = static_cast<int>(st.nbOfRows());
    m_spikeCountLabel->setText(QString::number(nbSpikes));

    // PCA file
    QString pcaPath = m_doc.documentDirectory() + QStringLiteral("/")
                    + m_doc.documentBaseName()  + QStringLiteral(".pca.")
                    + m_doc.currentElectrodeGroupID();
    if (QFileInfo::exists(pcaPath))
        m_pcaFileLabel->setText(pcaPath);
    else
        m_pcaFileLabel->setText(tr("<font color='red'>%1 — NOT FOUND</font>").arg(pcaPath));

    m_realignBtn->setEnabled(QFileInfo::exists(pcaPath) && nbSpikes > 0);
}

void SpikeRealignDialog::slotRealign()
{
    m_logEdit->clear();
    m_realignBtn->setEnabled(false);
    m_logEdit->append(tr("Starting realignment of cluster %1 …").arg(m_clusterId));
    QApplication::processEvents();

    QString log;
    int nShifted = 0;
    int nSwapped = 0;
    bool ok = m_doc.realignSpikes(m_clusterId, log, nShifted, nSwapped);

    m_logEdit->append(log);

    if (ok) {
        m_logEdit->append(tr("Done. %1 spike(s) shifted, %2 order swap(s).")
                          .arg(nShifted).arg(nSwapped));
        m_spikeCountLabel->setText(QString::number(
            [&]{ SortableTable st; m_doc.data().spikePositions(m_clusterId, st);
                 return static_cast<int>(st.nbOfRows()); }()));
    } else {
        m_logEdit->append(tr("<font color='red'>Realignment failed — see log above.</font>"));
        m_realignBtn->setEnabled(true);
    }
}

void SpikeRealignDialog::slotClose()
{
    accept();
}
