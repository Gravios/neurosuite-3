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
#ifndef PARAMETERPAGE_H
#define PARAMETERPAGE_H

//include files for the application
#include <parameterlayout.h>

// include files for QT
#include <QStringList> 

#include <QLineEdit> 


#include <QList>
#include <safetablewidget.h>
#include "cellguard.h"
class QComboBox;
struct ParameterCatalogEntry;
struct ParameterType;
class QTableWidgetItem;

/**
@author Lynn Hazan
*/
class ParameterPage : public ParameterLayout
{
    Q_OBJECT
public:
    /**Constructor.
 * @param expertMode true if the file is opened in expert mode, false otherwise.
 * @param parent the parent QWidget.
 * @param name name of the widget (can be used for introspection).
*/
    ParameterPage(bool expertMode, QWidget *parent = 0);
    ~ParameterPage();

    /**Sets the name of the program.
  * @param name name of the program.
  */
    void setProgramName(const QString& name){nameLineEdit->setText(name);}

    /**Gets the name of the program.*/
    QString getProgramName() const{return nameLineEdit->text();}

    /**Updates an internal status to reflect the change in program name.
  */
    void programNameChanged(){descriptionModified = true;}


    /** Initializes the parameter table.
 * @param parameters map containing the list of parameters.
 Each entry contains the name of the parameter, its value and its status (optional, mandatory or dynamic).
 */
    void setParameterInformation(const QMap<int, QStringList >& parameters);

    /**Returns the information for each parameter.
 * @return map containing the list of parameters. Each entry contains the name of the parameter, its value and its status (optional, mandatory or dynamic).
 */
    QMap<int, QStringList > getParameterInformation();

    /**Sets the value cell of the parameter named @p parameterName to
   * @p newValue.  No-op when no row matches.  When @p createIfMissing is
   * true and the name doesn't exist, a new optional row is appended.
   * Used by PipelinePage to write toggle state back into ndm_start's
   * parameter table.  Returns true if anything was changed.
   */
    bool setParameterValue(const QString& parameterName,
                           const QString& newValue,
                           bool createIfMissing = false);

    /**True if at least one of the description property has been modified, false otherwise.*/
    bool isDescriptionModified()const{return descriptionModified;}

    /**True if the value of at least one of parameters has been modified, false otherwise.*/
    bool isValueModified()const{return valueModified;}

signals:
    void nameChanged(const QString& extension);

    /**Emitted whenever a parameter VALUE cell (column 1) is committed.
   * Carries the parameter name and its new value as strings.  Used by
   * PipelinePage to keep the graphical pipeline view in sync with the
   * ndm_start flags without polling.  Description-only edits do not
   * fire this signal.
   */
    void parameterValueChanged(const QString& parameterName, const QString& newValue);

public slots:
    /**Handles the change of the program's name.*/
    void changeCaption();
    /**Adds parameters: for a plugin the catalog knows, those picked from
     * the ones it reads that the table does not list; otherwise (or on
     * "Other Name…") an empty row to type a name into.*/
    void addParameter();
    /**Removes the selected lines from the parameter table.*/
    void removeParameter();
    /**Moves the current line one row up (the order is the order the
     * description lists the parameters in).*/
    void moveParameterUp();
    /**Moves the current line one row down.*/
    void moveParameterDown();

    /** Will be called when any item in the table is modified.*/
    void itemModified(QTableWidgetItem* item);

    /**Resets the internal modification status to false.*/
    void resetModificationStatus(){
        valueModified = false;
        descriptionModified = false;
    }

    void slotValueModified() {
        valueModified = true;
    }


private:
    QComboBox *createCombobox();
    /** Appends @p entries (from the catalog) and moves to the last one's value. */
    void addCatalogParameters(const QList<ParameterCatalogEntry>& entries);
    /** Appends an empty row and starts editing its name. */
    void addEmptyParameter();
    /** Appends a row; @p extra is {type, default, hidden}, kept on the name
     *  cell.  Signals are the caller's to block.  Returns the row. */
    int appendRow(const QString& name, const QString& value, const QString& statusText,
                  const QStringList& extra = QStringList());
    /** The declared type of @p row's parameter (text when none). */
    ParameterType typeOfRow(int row) const;
    /** Sets the value cell's tooltip from the type and default. */
    void describeRow(int row);
    /** Swaps the current row with the one @p step away; false at an end. */
    bool moveCurrentRow(int step);

    QStringList status;
    /**Stores the list of column containing drop down list.*/
    QList<int> ddList;
    bool valueModified;
    bool descriptionModified;
    bool mExpertMode;
    /** Puts back a value its declared type does not take. */
    CellGuard guard;
};

#endif
