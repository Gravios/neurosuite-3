/***************************************************************************
 *   Copyright (C) 2006 by Michael Zugaro                                  *
 *   Ported to Qt6 - KDE QPageDialog replaced with QDialog                 *
 ***************************************************************************/
#ifndef QUERYOUTPUTDIALOG_H
#define QUERYOUTPUTDIALOG_H

#include <QDialog>
#include <QTextBrowser>

class QueryOutputDialog : public QDialog
{
    Q_OBJECT
public:
    explicit QueryOutputDialog(const QString& htmlText, const QString& queryResult,
                               QWidget *parent = nullptr,
                               const QString& caption = tr("Query"),
                               const QString& urltext = QString());
    ~QueryOutputDialog();

private slots:
    void slotSaveText();
    void slotSaveHTML();

private:
    QTextBrowser *html;
    QString       htmlText;
    QString       queryResult;
};

#endif
