/***************************************************************************
 *   Copyright (C) 2004 by Lynn Hazan                                      *
 *   lynn.hazan@myrealbox.com                                              *
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
#include "launcherpage.h"

// include files for Qt
#include <QDir> 

#include <QComboBox>

#include <QStringList> 

#include <QDebug>

LauncherPage::LauncherPage(const QString& url,QWidget* parent)
    : LauncherLayout(parent){
    QDir dir(url);
    // Each filter must be a separate QStringList entry — a single string like
    // "*.dat;*.eeg;*.fil" is treated as one literal pattern (never matches).
    QStringList neuroscopeFiles = dir.entryList(QStringList() << QLatin1String("*.dat")
                                                               << QLatin1String("*.eeg")
                                                               << QLatin1String("*.fil"));
    neuroscopeFiles.append("");
    neuroscopeComboBox->addItems(neuroscopeFiles);
    QStringList klustersFiles = dir.entryList(QStringList() << QLatin1String("*.fet")
                                                             << QLatin1String("*.spk")
                                                             << QLatin1String("*.clu"));
    klustersFiles.append("");
    klustersComboBox->addItems(klustersFiles);

}


LauncherPage::~LauncherPage()
{
}


