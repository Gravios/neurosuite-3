/***************************************************************************
                          prefgeneral.h  -  description
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

#ifndef PREFGENERAL_H
#define PREFGENERAL_H

// include files for QT
#include <QWidget>
#include <QComboBox>
#include <QCheckBox> 
#include <QSpinBox>
#include <QPushButton>
#include <QLineEdit>


//include files for the application
#include <prefgenerallayout.h>

/**
  * Class representing the Klusters General Configuration page of the Klusters preferences dialog.
  *@author Lynn Hazan
  */

class PrefGeneral : public PrefGeneralLayout  {
    Q_OBJECT
public: 
    explicit PrefGeneral(QWidget *parent=0);
    ~PrefGeneral();

    /**Sets the use of a crash and recovery autosave.*/
    void setCrashRecovery(bool use);

    /**Sets the index of time interval between 2 crash and recovery autosave.*/
    void setCrashRecoveryIndex(int index);

    /**Sets the number of step in the undo/redo mechanism.*/
    void setNbUndo(int nb);

    /**Sets the background color.*/
    void setBackgroundColor(const QColor& color);

    /**Sets the reclustering executable.*/
    void setReclusteringExecutable(const QString& executable);

    /**Sets the arguments for the reclustering.*/
    void setReclusteringArguments(const QString &arguments);

    void setRealignThreshold(double v);
    void setRealignIterations(int n);
    void setRealignMaxShift(int n);

    /**Returns true if a crash and recovery autosave is performed, false othewise.*/
    bool isCrashRecovery() const;

    /**Returns the index of the time interval between 2 crash and recovery autosave in minutes.*/
    int crashRecoveryIntervalIndex() const;

    /**Returns the number of step in the undo/redo mechanism.*/
    int getNbUndo() const;

    /**Returns the background color.*/
    QColor getBackgroundColor() const;

    /**Returns the reclustering executable.*/
    QString getReclusteringExecutable() const;

    /**Returns the arguments for the reclustering.*/
    QString getReclusteringArguments() const;

    double getRealignThreshold() const;
    int    getRealignIterations() const;
    int    getRealignMaxShift() const;

    bool useWhiteColorDuringPrinting() const;

    void setUseWhiteColorDuringPrinting(bool b);
    bool getAutoSelectFeatures() const;
    void setAutoSelectFeatures(bool checked);
    int  getAutoSelectNFeatures() const;
    void setAutoSelectNFeatures(int n);

    /**Sets the scatter plot marker size.*/
    void setMarkerSize(int size);

    /**Returns the scatter plot marker size.*/
    int getMarkerSize() const;

    /**Sets the selection polygon line width.*/
    void setSelectionLineWidth(int w);

    /**Returns the selection polygon line width.*/
    int getSelectionLineWidth() const;

    /**Returns the minimum threshold for the template match slider.*/
    double getTemplateThresholdMin() const;
    /**Sets the minimum threshold for the template match slider.*/
    void setTemplateThresholdMin(double v);
    /**Returns the maximum threshold for the template match slider.*/
    double getTemplateThresholdMax() const;
    /**Sets the maximum threshold for the template match slider.*/
    void setTemplateThresholdMax(double v);
private Q_SLOTS:
    void updateCrashRecoveryTimeInterval(int state);

    void updateReclusteringExecutable();

};

#endif
