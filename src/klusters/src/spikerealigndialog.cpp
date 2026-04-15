/***************************************************************************
 * spikerealigndialog.cpp
 *
 * Pre-flight configuration panel for spike realignment.
 ***************************************************************************/

#include "spikerealigndialog.h"
#include "klustersdoc.h"
#include "data.h"
#include "sortabletable.h"
#include "realign_xcorr.h"

#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QFrame>
#include <algorithm>
#include <QFont>

// Forward declaration — XcorrDispatch is defined in realign_xcorr_dispatch.cpp
namespace XcorrDispatch {
    const char* backendName();
}

SpikeRealignDialog::SpikeRealignDialog(KlustersDoc& doc, int clusterId,
                                       const QString& args, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
    , m_clusterId(clusterId)
    , m_args(args)
{
    setWindowTitle(tr("Realign Spikes — Cluster %1").arg(clusterId));
    setMinimumWidth(500);
    buildUi();
}

SpikeRealignDialog::~SpikeRealignDialog() = default;

void SpikeRealignDialog::buildUi()
{
    auto* vlay = new QVBoxLayout(this);
    vlay->setSpacing(8);

    // ── Title ────────────────────────────────────────────────────────────────
    auto* titleLabel = new QLabel(tr("<b>Spike Realignment</b>"));
    QFont f = titleLabel->font();
    f.setPointSize(f.pointSize() + 2);
    titleLabel->setFont(f);
    vlay->addWidget(titleLabel);

    // ── Info grid ────────────────────────────────────────────────────────────
    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    int row = 0;

    grid->addWidget(new QLabel(tr("Cluster:")), row, 0);
    grid->addWidget(new QLabel(QString::number(m_clusterId)), row++, 1);

    // Spike count
    grid->addWidget(new QLabel(tr("Spikes in cluster:")), row, 0);
    m_spikeCountLabel = new QLabel(tr("—"));
    grid->addWidget(m_spikeCountLabel, row++, 1);

    // PCA file
    grid->addWidget(new QLabel(tr("PCA eigenvector file:")), row, 0);
    m_pcaFileLabel = new QLabel(tr("—"));
    m_pcaFileLabel->setWordWrap(true);
    grid->addWidget(m_pcaFileLabel, row++, 1);

    // Compute backend
    grid->addWidget(new QLabel(tr("Compute backend:")), row, 0);
    m_backendLabel = new QLabel(QString::fromLatin1(XcorrDispatch::backendName()));
    grid->addWidget(m_backendLabel, row++, 1);

    // Parameters summary — parse from args (same logic as realignSpikes)
    Data& d = m_doc.data();
    int peakSamp = d.peakSampleIndex();
    int maxShift = std::max(1, peakSamp / 2);
    float minScore = 0.70f;
    int nIter = 2;
    {
        const QStringList tokens = m_args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (int ti = 0; ti < tokens.size(); ++ti) {
            const QString& tok = tokens[ti];
            if ((tok == QStringLiteral("--threshold") || tok == QStringLiteral("-t"))
                    && ti + 1 < tokens.size()) {
                bool ok2; float v = tokens[++ti].toFloat(&ok2);
                if (ok2 && v > 0.0f && v <= 1.0f) minScore = v;
            } else if ((tok == QStringLiteral("--iterations") || tok == QStringLiteral("-i"))
                    && ti + 1 < tokens.size()) {
                bool ok2; int v = tokens[++ti].toInt(&ok2);
                if (ok2 && v >= 1 && v <= 20) nIter = v;
            } else if ((tok == QStringLiteral("--maxshift") || tok == QStringLiteral("-m"))
                    && ti + 1 < tokens.size()) {
                bool ok2; int v = tokens[++ti].toInt(&ok2);
                if (ok2 && v >= 1) maxShift = v;
            }
        }
    }

    grid->addWidget(new QLabel(tr("Search radius:")), row, 0);
    grid->addWidget(new QLabel(tr("±%1 samples").arg(maxShift)), row++, 1);

    grid->addWidget(new QLabel(tr("Acceptance threshold:")), row, 0);
    grid->addWidget(new QLabel(tr("%1 (normalised xcorr)").arg(
        QString::number(static_cast<double>(minScore), 'f', 2))), row++, 1);

    grid->addWidget(new QLabel(tr("Alignment iterations:")), row, 0);
    grid->addWidget(new QLabel(QString::number(nIter)), row++, 1);

    vlay->addLayout(grid);

    // ── Divider ──────────────────────────────────────────────────────────────
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    vlay->addWidget(line);

    // ── Description ──────────────────────────────────────────────────────────
    auto* desc = new QLabel(
        tr("For each spike in the cluster the normalised cross-correlation "
           "between its waveform and the cluster template (mean waveform) is "
           "computed across all channels simultaneously at every lag in the "
           "search range. The lag with the highest score is used as the "
           "alignment shift. Spikes scoring below the acceptance threshold "
           "are left unchanged.<br><br>"
           "Progress and diagnostics will be printed in the "
           "<b>Realign output</b> tab that opens in the main window. "
           "The interface is locked for editing during the process, "
           "exactly as during reclustering."));
    desc->setWordWrap(true);
    vlay->addWidget(desc);

    vlay->addStretch();

    // ── Buttons ──────────────────────────────────────────────────────────────
    auto* btnLay = new QHBoxLayout;
    m_startBtn       = new QPushButton(tr("Start Realignment"));
    auto* cancelBtn  = new QPushButton(tr("Cancel"));
    btnLay->addStretch();
    btnLay->addWidget(m_startBtn);
    btnLay->addWidget(cancelBtn);
    vlay->addLayout(btnLay);

    connect(m_startBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn,  &QPushButton::clicked, this, &QDialog::reject);

    // ── Populate dynamic fields ───────────────────────────────────────────────
    SortableTable st;
    int nbSpikes = 0;
    if (m_doc.data().spikePositions(m_clusterId, st))
        nbSpikes = static_cast<int>(st.nbOfRows());
    m_spikeCountLabel->setText(QString::number(nbSpikes));

    // Prefer .pcaD.N for stderiv sessions, .pca.N otherwise.
    const QString grpId  = m_doc.currentElectrodeGroupID();
    const QString pcaDPath = m_doc.documentDirectory() + QStringLiteral("/")
                           + m_doc.documentBaseName()  + QStringLiteral(".pcaD.")
                           + grpId;
    const QString pcaRawPath = m_doc.documentDirectory() + QStringLiteral("/")
                             + m_doc.documentBaseName()  + QStringLiteral(".pca.")
                             + grpId;
    const bool isStderiv = m_doc.isStderivSession();
    const QString pcaPath = (isStderiv && QFileInfo::exists(pcaDPath))
                           ? pcaDPath
                           : (isStderiv ? pcaDPath   // show expected path even when missing
                                        : pcaRawPath);

    bool pcaOk = QFileInfo::exists(pcaPath);
    if (pcaOk)
        m_pcaFileLabel->setText(pcaPath);
    else
        m_pcaFileLabel->setText(
            tr("<font color='red'>NOT FOUND: %1</font>").arg(pcaPath));

    m_startBtn->setEnabled(pcaOk && nbSpikes > 0);
    if (!pcaOk) {
        const QString ext = isStderiv ? QStringLiteral(".pcaD.") : QStringLiteral(".pca.");
        const QString tool = isStderiv ? QStringLiteral("ndm_pca_stderiv")
                                       : QStringLiteral("ndm_pca");
        desc->setText(desc->text() +
            tr("<br><br><b><font color='red'>Cannot start: the PCA eigenvector "
               "file (%1%2) was not found. Run %3 first.</font></b>")
            .arg(ext).arg(grpId).arg(tool));
    }
}
