/***************************************************************************
                          prefclusterview.cpp  -  description
                             -------------------
    begin                : Thu Dec 11 2003
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

#include "prefclusterview.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QIntValidator>
#include <QDoubleValidator>


PrefClusterView::PrefClusterView(QWidget *parent) : PrefClusterViewLayout(parent) {
    // Text box by request; the validator keeps it numeric.  Floor mirrors
    // Configuration::setTsneSpikeCap, which clamps again on apply.
    tsneCapLineEdit->setValidator(
        new QIntValidator(1000, 100000000, tsneCapLineEdit));
    tsneStartPerpLineEdit->setValidator(new QDoubleValidator(2.0, 1000.0, 1, tsneStartPerpLineEdit));
    tsneStartPerpLineEdit->setToolTip(tr(
        "Perplexity a fresh embedding starts at; the up/down arrows move from here.\n"
        "Low values expose local structure, high values preserve the global layout."));
    tsneItersLineEdit->setValidator(new QIntValidator(50, 100000, tsneItersLineEdit));
    tsneItersLineEdit->setToolTip(tr(
        "Gradient iterations: the main quality-versus-time dial.\n"
        "250 for a quick look, 500 by default, 1000 when the result matters."));
    tsneThetaLineEdit->setValidator(new QDoubleValidator(0.0, 0.8, 2, tsneThetaLineEdit));
    tsneThetaLineEdit->setToolTip(tr(
        "Barnes-Hut approximation of the repulsive term.  0 is exact and very slow;\n"
        "0.5 is the usual compromise, higher is faster and coarser."));
    tsneDimsLineEdit->setValidator(new QIntValidator(0, 4096, tsneDimsLineEdit));
    tsneDimsLineEdit->setToolTip(tr(
        "How many feature dimensions to embed, counting from the first.\n"
        "0 uses every dimension except time."));
    tsneEtaLineEdit->setValidator(new QDoubleValidator(1.0, 100000.0, 1, tsneEtaLineEdit));
    tsneExagLineEdit->setValidator(new QDoubleValidator(1.0, 1000.0, 1, tsneExagLineEdit));
    tsneExagItersLineEdit->setValidator(new QIntValidator(0, 100000, tsneExagItersLineEdit));
    tsneAdvancedGroupBox->setToolTip(tr(
        "These change how the optimiser converges.  Wrong values do not fail loudly —\n"
        "they produce a plausible-looking embedding that means nothing."));
    tsneSubsampleCheckBox->setToolTip(tr(
        "With more spikes selected than the cap allows, embed a random subsample of\n"
        "cap size instead of refusing.  The sample is drawn across the whole selection."));
    tsneRandomSeedCheckBox->setToolTip(tr(
        "Off, the same selection always gives the same embedding.  On, each run draws a\n"
        "new seed — run twice and see whether a split survives before trusting it."));
    tsneStepLineEdit->setValidator(
        new QIntValidator(1, 1000, tsneStepLineEdit));
    tsneStepLineEdit->setToolTip(
        tr("How much one up/down arrow press changes the perplexity while the\n"
           "t-SNE view is showing.  Each press recomputes the embedding."));
    tsneCapLineEdit->setToolTip(
        tr("Selections with more spikes than this refuse the t-SNE toggle.\n"
           "Latency budget: ~30k spikes take on the order of a minute."));

    projScopeMinutesLineEdit->setValidator(
        new QDoubleValidator(0.1, 600.0, 2, projScopeMinutesLineEdit));
    projModeCombo->setToolTip(tr(
        "Session-spanning projects every selected spike onto the pinned template(s).\n"
        "Temporally-restricted only projects spikes whose time falls in the template's scope."));
    projScopeMinutesLineEdit->setToolTip(tr(
        "Chunk granularity (minutes) defining a template's temporal window; matches the\n"
        "fiber-template drift binning.  Default 12."));
    projHideOutOfScopeCheckBox->setToolTip(tr(
        "In temporally-restricted mode: hide out-of-scope spikes entirely, or (unchecked)\n"
        "keep them greyed and non-selectable for context."));
    correctCollisionRowsCheckBox->setToolTip(tr(
        "A spike row whose .eap row names two or more template classes holds two units'\n"
        "spikes.  Checked: every waveform read of such a row gets the record with the\n"
        "partner's fitted template subtracted, so the row shows as its own unit's spike.\n"
        "The .spk file is never changed; the corrected rows are rebuilt from it and are\n"
        "written as a sparse .spkc at Save.  Unchecked: the raw records."));
    showCollisionsCheckBox->setToolTip(tr(
        "Overlay a ring on feature-view spikes whose .eap row has two or more template\n"
        "classes set (collisions) — so multi-membership is visible over the .clu colours."));
    densitySigmaSpinBox->setToolTip(tr(
        "Width (sigma) of the Gaussian kernel that smooths the density heatmap, in screen\n"
        "pixels.  Small values keep fine structure; larger ones merge nearby points into\n"
        "smooth basins.  0 shows the raw 2-pixel binned counts."));
    showClassMembersCheckBox->setToolTip(tr(
        "Ring the .eap members of the template class selected in the template-library\n"
        "view — a class's membership footprint across the feature view, over .clu colours."));

    // ── The spike under the cursor ──
    QGroupBox* hoverBox = new QGroupBox(tr("Spike under the cursor"), this);
    hoverBox->setToolTip(tr(
        "Moving the cursor over a feature view rings the nearest spike and draws its waveform over\n"
        "the waveform view, in a colour that stands off the cluster's."));
    QFormLayout* hform = new QFormLayout(hoverBox);
    hform->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    hoverEnabledCheck = new QCheckBox(tr("Ring the spike under the cursor and draw its waveform"), hoverBox);
    hoverEnabledCheck->setToolTip(tr(
        "Off: no feature view tracks the cursor.  Shift+V still turns the tracking on or off in the\n"
        "view it is pressed in, until the next Apply."));
    hform->addRow(hoverEnabledCheck);
    hoverRingMarginSpin = new QSpinBox(hoverBox);
    hoverRingMarginSpin->setRange(0, 64); hoverRingMarginSpin->setPrefix(tr("point size + ")); hoverRingMarginSpin->setSuffix(tr(" px"));
    hoverRingMarginSpin->setToolTip(tr("The ring's radius in the feature view: the size the points are drawn at, plus this."));
    hform->addRow(tr("Ring radius"), hoverRingMarginSpin);
    const auto width = [hoverBox]() {
        QDoubleSpinBox* s = new QDoubleSpinBox(hoverBox);
        s->setRange(0.5, 12.0); s->setDecimals(1); s->setSingleStep(0.5); s->setSuffix(tr(" px"));
        return s;
    };
    hoverRingWidthSpin = width();
    hform->addRow(tr("Ring line width"), hoverRingWidthSpin);
    hoverTraceWidthSpin = width();
    hoverTraceWidthSpin->setToolTip(tr("The spike's waveform drawn over the waveform view.  Screen pixels at any zoom."));
    hform->addRow(tr("Waveform trace width"), hoverTraceWidthSpin);
    // Under the Density heatmap group, in the grid that holds the page's groups.
    QGridLayout* groups = nullptr;
    if (QLayout* outer = layout())
        for (int i = 0; i < outer->count() && !groups; ++i)
            if (QGridLayout* g = qobject_cast<QGridLayout*>(outer->itemAt(i)->layout()))
                if (g->indexOf(densityGroupBox) >= 0) groups = g;
    if (groups) {
        int row = 0, column = 0, rowSpan = 1, columnSpan = 1;
        groups->getItemPosition(groups->indexOf(densityGroupBox), &row, &column, &rowSpan, &columnSpan);
        groups->addWidget(hoverBox, groups->rowCount(), column, 1, columnSpan);
    } else if (QGridLayout* grid = qobject_cast<QGridLayout*>(layout())) {
        grid->addWidget(hoverBox, grid->rowCount(), 0, 1, -1);
    } else if (QBoxLayout* rows = qobject_cast<QBoxLayout*>(layout())) {
        rows->addWidget(hoverBox);
    }
    setHoverSpike(configuration().getHoverSpikeDefault());
    connect(hoverEnabledCheck, &QCheckBox::toggled, this, [this](bool on) {
        hoverRingMarginSpin->setEnabled(on); hoverRingWidthSpin->setEnabled(on); hoverTraceWidthSpin->setEnabled(on);
        Q_EMIT hoverSpikeChanged();
    });
    connect(hoverRingMarginSpin, &QSpinBox::valueChanged, this, [this](int) { Q_EMIT hoverSpikeChanged(); });
    connect(hoverRingWidthSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { Q_EMIT hoverSpikeChanged(); });
    connect(hoverTraceWidthSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { Q_EMIT hoverSpikeChanged(); });
}
PrefClusterView::~PrefClusterView(){
}







































void PrefClusterView::setHoverSpike(const Configuration::HoverSpike& h)
{
    const QSignalBlocker b1(hoverEnabledCheck), b2(hoverRingMarginSpin), b3(hoverRingWidthSpin), b4(hoverTraceWidthSpin);
    hoverEnabledCheck->setChecked(h.enabled);
    hoverRingMarginSpin->setValue(h.ringMarginPx);
    hoverRingWidthSpin->setValue(h.ringWidthPx);
    hoverTraceWidthSpin->setValue(h.traceWidthPx);
    hoverRingMarginSpin->setEnabled(h.enabled); hoverRingWidthSpin->setEnabled(h.enabled); hoverTraceWidthSpin->setEnabled(h.enabled);
}

Configuration::HoverSpike PrefClusterView::getHoverSpike() const
{
    Configuration::HoverSpike h;
    h.enabled      = hoverEnabledCheck->isChecked();
    h.ringMarginPx = hoverRingMarginSpin->value();
    h.ringWidthPx  = hoverRingWidthSpin->value();
    h.traceWidthPx = hoverTraceWidthSpin->value();
    return h;
}
