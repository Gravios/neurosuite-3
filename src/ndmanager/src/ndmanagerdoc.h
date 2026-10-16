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
#ifndef NDMANAGERDOC_H
#define NDMANAGERDOC_H

// include files for QT
#include <QString>



#include <QWidget>

//include files for the application

/**
@author Lynn Hazan
*/
class ndManagerDoc{

private:
    /**The url of the document .*/
    QString docUrl;
    /**The file the document's content was loaded from (or last saved to under
     * its own name).  A save starts from it, so keys ndmanager does not model
     * survive; unlike docUrl it does not follow rename() or a pending saveAs().*/
    QString sourceUrl;
    /**Reason the last openDocument() failed to parse; empty on success.*/
    QString lastParseError;
    
public:
    /**Constructor.
 * @param parent the parent QWidget.
 */
    explicit ndManagerDoc(QWidget* parent);
    ~ndManagerDoc();

    /**Information retun after a call to openFile/saveDocument/createFeatureFile*/
    enum OpenSaveCreateReturnMessage {OK=0,OPEN_ERROR=1,DOWNLOAD_ERROR=3,INCORRECT_FILE=4,SAVE_ERROR=5,
                                      UPLOAD_ERROR=6,INCORRECT_CONTENT=7,CREATION_ERROR=8,PARSE_ERROR=9,MISSING_FILE=10,ALREADY_OPENED=11};

    /**closes the actual document.*/
    void closeDocument();

    /** opens the document by filename.
  * @param url url of the file to open (dat file or eeg file).
  * @return an opensavecreatereturnmessage enum giving the open status.
  */
    OpenSaveCreateReturnMessage openDocument(const QString& url);

    /**
     * Reason the last openDocument() returned PARSE_ERROR (yaml-cpp message with
     * line/column, or why the document is not a parameter file).  Empty otherwise.
     */
    QString parseError() const { return lastParseError; }

    template<typename Reader>
    OpenSaveCreateReturnMessage loadFromReader(Reader& reader);

    /**Returns the QString of the document. */
    inline const QString& url() const{return docUrl;}

    /**Loads information in order to create a new the document .
  * @return an opensavecreatereturnmessage enum giving the initialization status.
  */
    OpenSaveCreateReturnMessage newDocument();

    /**Saves the current parameter file.
 @return an OpenSaveCreateReturnMessage enum giving the saving status.
 */
    inline OpenSaveCreateReturnMessage save(){return save(docUrl);}

    /**Saves the current parameter file on the disk at the location defined by url.
 * The saved file starts from the file the document was loaded from (not the
 * file at url), so keys ndmanager does not model are kept: on Save the two are
 * the same file; on Save As the target is new or an unrelated file the user
 * chose to replace, whose keys describe some other session.
 * @param url url of the file where to save the parameter information.
 * @return an OpenSaveCreateReturnMessage enum giving the saving status.
 */
    OpenSaveCreateReturnMessage save(const QString &url);

    /**Saves the parameter file under a new name.  The document takes the new
 * name only when the file was written; on failure it keeps its old one.
 * @param newUrl new url where to write the parameter fil to.
 * @return an OpenSaveCreateReturnMessage enum giving the saving status.
 */
    OpenSaveCreateReturnMessage saveAs(const QString& newUrl);

    /**Save the current parameter file as the default one used by File > New:
 * <writable ApplicationsLocation>/ndmanager/ndManagerDefault.yaml.  The
 * session itself is not marked as saved.*/
    OpenSaveCreateReturnMessage saveDefault();

    /**Saves the script @p scriptName.
 * @param scriptName name of the script to save.
 * @return an OpenSaveCreateReturnMessage enum giving the saving status.
 */
    OpenSaveCreateReturnMessage saveScript(const QString& scriptName);

    /**Renames the parameter file.
 * @param newUrl new url where to write the parameter fil to.
 */
    void rename(const QString& newUrl){
        docUrl = newUrl;
    }

private:

    /**Writes the document to url, starting from the parameter file baseUrl
 * (empty: from nothing), and carrying every key the model does not own.
 * @param sessionSave true for a save of the session itself (Save, Save As):
 * on success the view's modified state is reset; false for a copy written
 * elsewhere (Save as Default), which leaves the session state untouched.*/
    OpenSaveCreateReturnMessage write(const QString& url, const QString& baseUrl, bool sessionSave);

    /**Pointer on the parent widget (main window).*/
    QWidget* parent;

};

#endif
