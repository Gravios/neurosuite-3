/***************************************************************************
 *   Copyright (C) 2007 by Lynn Hazan   *
 *   lynn.hazan@myrealbox.com   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
// application specific includes
#include "clusterinformationdialog.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>



ClusterInformationDialog::ClusterInformationDialog(QWidget *parent,const QString& caption)
    : QDialog(parent)
{
    setWindowTitle(caption);
    setModal(true);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6,6,6,6);
    QFormLayout* form = new QFormLayout;
    layout->addLayout(form);

    // Anatomy first (finest label, then the named region, its division, and the
    // atlas the terms come from), then the unit itself.  Tab follows this order.
    auto field = [&](const QString& label, const QString& placeholder, const QString& tip) {
        QLineEdit* e = new QLineEdit(this);
        e->setMinimumWidth(fontMetrics().averageCharWidth()*40);
        e->setPlaceholderText(placeholder);
        e->setToolTip(tip);
        form->addRow(label, e);
        return e;
    };
    structure = field(tr("Structure"), tr("e.g. CA1"),
                      tr("The finest anatomical label of the recording site."));
    region    = field(tr("Region"), tr("e.g. hippocampal formation"),
                      tr("The named brain region the structure belongs to."));
    division  = field(tr("Division"), tr("e.g. archicortex, cerebral cortex, thalamus, cerebellum"),
                      tr("The broad division of the brain the region belongs to."));
    atlas     = field(tr("Atlas"), tr("e.g. Waxholm Space rat atlas (WHS SD) v4"),
                      tr("The reference atlas or ontology the terms above come from."));
    regionId  = field(tr("Region ID"), tr("e.g. the atlas structure ID or an UBERON:… ID"),
                      tr("A stable identifier of the finest term in that atlas or ontology."));
    type      = field(tr("Type"), tr("e.g. pyramidal, interneuron"),
                      tr("The putative cell type of the unit."));
    id        = field(tr("Isolation distance"), QString(), QString());
    id->setObjectName("id");
    quality   = field(tr("Quality"), QString(), QString());
    notes     = field(tr("Notes"), QString(), QString());
    structure->setFocus();

    layout->addStretch(10);

    QDialogButtonBox*buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                                      | QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

UnitAnnotation ClusterInformationDialog::annotation() const
{
    UnitAnnotation a;
    a.structure = structure->text();
    a.type = type->text();
    a.isolationDistance = id->text();
    a.quality = quality->text();
    a.notes = notes->text();
    a.region = region->text();
    a.division = division->text();
    a.atlas = atlas->text();
    a.regionId = regionId->text();
    return a;
}

void ClusterInformationDialog::setAnnotation(const UnitAnnotation& a)
{
    structure->setText(a.structure);
    type->setText(a.type);
    id->setText(a.isolationDistance);
    quality->setText(a.quality);
    notes->setText(a.notes);
    region->setText(a.region);
    division->setText(a.division);
    atlas->setText(a.atlas);
    regionId->setText(a.regionId);
}

ClusterInformationDialog::~ClusterInformationDialog()
{
}

