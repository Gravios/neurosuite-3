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
#include "klustersdoc_internal.h"   // featureMethod / resolveFeature (custody)

#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>     // patch82
#include <QFileInfo>
#include <QFrame>
#include <algorithm>
#include <QFont>

// Forward declaration — XcorrDispatch is defined in realign_xcorr_dispatch.cpp
namespace XcorrDispatch {
    const char* backendName();
}

SpikeRealignDialog::SpikeRealignDialog(KlustersDoc& document, int cluster,
                                       const QString& realignArgs, QWidget* parent)
    : QDialog(parent)
    , doc(document)
    , clusterId(cluster)
    , args(realignArgs)
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
    grid->addWidget(new QLabel(QString::number(clusterId)), row++, 1);

    // Spike count
    grid->addWidget(new QLabel(tr("Spikes in cluster:")), row, 0);
    spikeCountLabel = new QLabel(tr("—"));
    grid->addWidget(spikeCountLabel, row++, 1);

    // PCA file
    grid->addWidget(new QLabel(tr("PCA eigenvector file:")), row, 0);
    pcaFileLabel = new QLabel(tr("—"));
    pcaFileLabel->setWordWrap(true);
    grid->addWidget(pcaFileLabel, row++, 1);

    // Compute backend
    grid->addWidget(new QLabel(tr("Compute backend:")), row, 0);
    backendLabel = new QLabel(QString::fromLatin1(XcorrDispatch::backendName()));
    grid->addWidget(backendLabel, row++, 1);

    // Parameters summary — parse from args (same logic as realignSpikes)
    Data& d = doc.data();
    int peakSamp = d.peakSampleIndex();
    int maxShift = std::max(1, peakSamp / 2);
    float minScore = 0.70f;
    int nIter = 2;
    {
        const QStringList tokens = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
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

    // patch82 — PCA-projection-maximizing refine pass (opt-in).
    //
    // After the mean-template xcorr alignment converges, a second pass
    // can further refine each spike's position by trying candidate
    // shifts in [-maxShift, +maxShift] and picking the one that
    // MAXIMIZES the spike's PCA projection energy:
    //
    //     energy(s) = sum_ch sum_k <basis_ch_k, spike(s) - mean_ch>²
    //
    // The intuition: a well-aligned spike concentrates its variance in
    // the directions of greatest cluster variance (the kept PCA
    // components); a misaligned spike spreads variance across all
    // directions, suppressing this sum.  Maximizing energy(s) picks the
    // shift where the spike most strongly "looks like a member of this
    // cluster" as measured by the canonical PCA basis.
    //
    // Per-spike .fil re-extract at every candidate shift means this is
    // O(N_spikes × (2·maxShift+1) × nSamp × nChan) more I/O than the
    // mean-only path, but on a typical 32-channel, 32-sample spike with
    // maxShift=2 and 5000 spikes it adds < 1 second on a warm cache.
    bool pcaRefineInitial = false;
    {
        const QStringList tokens = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& tok : tokens) {
            if (tok == QStringLiteral("--pca-refine") || tok == QStringLiteral("-p")) {
                pcaRefineInitial = true;
                break;
            }
        }
    }
    pcaRefineCheck = new QCheckBox(
        tr("Refine with PCA-projection maximization (after mean alignment)"));
    pcaRefineCheck->setChecked(pcaRefineInitial);
    pcaRefineCheck->setToolTip(
        tr("When checked, runs a second alignment pass after the mean-"
           "template xcorr converges.  For each spike, sweeps candidate "
           "shifts in [-maxShift, +maxShift] and picks the shift that "
           "maximizes the spike's PCA-projection energy (sum of squared "
           "scores onto the kept .pca eigenvectors).  Tightens alignment "
           "in directions of greatest cluster variance; sometimes catches "
           "alignment offsets that the global mean template can't.  Adds "
           "one extra .fil read per candidate shift per spike — usually "
           "under a second total."));
    vlay->addWidget(pcaRefineCheck);

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
    startBtn       = new QPushButton(tr("Start Realignment"));
    auto* cancelBtn  = new QPushButton(tr("Cancel"));
    btnLay->addStretch();
    btnLay->addWidget(startBtn);
    btnLay->addWidget(cancelBtn);
    vlay->addLayout(btnLay);

    connect(startBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn,  &QPushButton::clicked, this, &QDialog::reject);

    // ── Populate dynamic fields ───────────────────────────────────────────────
    SortableTable st;
    int nbSpikes = 0;
    if (doc.data().spikePositions(clusterId, st))
        nbSpikes = static_cast<int>(st.nbOfRows());
    spikeCountLabel->setText(QString::number(nbSpikes));

    // The PCA basis realignSpikes will use: <base>.pca.<method>.<group>, with
    // the method read off the session's .fet, resolved through the custody
    // policy exactly as klustersdoc_realign.cpp does.  (This used the retired
    // neurofileio::resolveInput preference lists, which probe the untagged
    // .pca.N and the glued .pcaD.N but not .pca.stderiv_C5.N, so a suffixed
    // session was reported NOT FOUND here while the realign itself found it.)
    // When the file is missing, the path is still the one realign would read,
    // so the message names it.
    const QString grpId   = doc.currentElectrodeGroupID();
    const QString fullBase = doc.documentDirectory() + QStringLiteral("/") + doc.documentBaseName();
    const QString pcaPath = klustersdoc_internal::resolveFeature(
        fullBase, QStringLiteral("pca"), grpId,
        klustersdoc_internal::featureMethod(doc.origFetFilePath()));

    bool pcaOk = QFileInfo::exists(pcaPath);
    if (pcaOk)
        pcaFileLabel->setText(pcaPath);
    else
        pcaFileLabel->setText(
            tr("<font color='red'>NOT FOUND: %1</font>").arg(pcaPath));

    startBtn->setEnabled(pcaOk && nbSpikes > 0);
    if (!pcaOk) {
        // Name the file realign would read (pcaPath carries the session's own
        // method token, e.g. .pca.stderiv_C5.N) rather than a guessed extension.
        const QString tool = doc.isStderivSession() ? QStringLiteral("ndm_pca_stderiv")
                                                    : QStringLiteral("ndm_pca");
        desc->setText(desc->text() +
            tr("<br><br><b><font color='red'>Cannot start: the PCA eigenvector "
               "file (%1) was not found. Run %2 first.</font></b>")
            .arg(QFileInfo(pcaPath).fileName(), tool));
    }
}

// patch82 — Add (or remove) "--pca-refine" from the args string based on
// the new checkbox state, so the chosen mode reaches realignSpikes via
// the same string-based knob plumbing as --threshold / --iterations /
// --maxshift / --topchannels.  Caller (slotRealignSpikes) reads this
// instead of the original args.
QString SpikeRealignDialog::finalArgs() const
{
    const bool wantRefine = pcaRefineCheck && pcaRefineCheck->isChecked();

    // Strip any existing --pca-refine / -p flag, then append iff wanted.
    // Tokens are space-separated; the flag takes no value.
    QStringList tokens = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList out;
    out.reserve(tokens.size() + 1);
    for (const QString& tok : tokens) {
        if (tok == QStringLiteral("--pca-refine") ||
            tok == QStringLiteral("-p")) continue;
        out.append(tok);
    }
    if (wantRefine) out.append(QStringLiteral("--pca-refine"));
    return out.join(QLatin1Char(' '));
}
