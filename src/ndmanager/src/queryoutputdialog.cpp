/***************************************************************************
 *   Copyright (C) 2006 by Michael Zugaro                                  *
 *   Ported to Qt6 - KDE QPageDialog replaced with QDialog                 *
 ***************************************************************************/
#include "queryoutputdialog.h"

#include <QTextStream>
#include <QMessageBox>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDialogButtonBox>

QueryOutputDialog::QueryOutputDialog(const QString& htmlText, const QString& queryResult,
                                     QWidget *parent, const QString& caption, const QString& urltext) :
    QDialog(parent),
    htmlText(htmlText),
    queryResult(queryResult)
{
    setWindowTitle(caption);
    resize(800, 600);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    html = new QTextBrowser(this);
    html->setHtml(htmlText);
    mainLayout->addWidget(html);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    QPushButton *saveTextBtn = buttonBox->addButton(tr("Save As Text"), QDialogButtonBox::ActionRole);
    QPushButton *saveHTMLBtn = buttonBox->addButton(tr("Save As HTML"), QDialogButtonBox::ActionRole);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(saveTextBtn, &QPushButton::clicked, this, &QueryOutputDialog::slotSaveText);
    connect(saveHTMLBtn, &QPushButton::clicked, this, &QueryOutputDialog::slotSaveHTML);
}

QueryOutputDialog::~QueryOutputDialog() {}

void QueryOutputDialog::slotSaveText()
{
    const QString filename = QFileDialog::getSaveFileName(this, tr("Save query"), QString(), "*");
    if (filename.isEmpty()) return;
    if (QFile::exists(filename) &&
        QMessageBox::question(this, QString(), tr("File already exists. Overwrite?"),
                              QMessageBox::Yes|QMessageBox::No) == QMessageBox::No)
        return;
    QFile textFile(filename);
    if (textFile.open(QIODevice::WriteOnly)) {
        QTextStream stream(&textFile);
        stream << queryResult;
        textFile.close();
        if (textFile.error() == QFile::WriteError)
            QMessageBox::critical(this, QString(), tr("Could not save the report."));
    } else {
        QMessageBox::critical(this, QString(), tr("Could not save the report."));
    }
}

void QueryOutputDialog::slotSaveHTML()
{
    const QString filename = QFileDialog::getSaveFileName(this, tr("Save query as HTML"), QString(), "*.html");
    if (filename.isEmpty()) return;
    if (QFile::exists(filename) &&
        QMessageBox::question(this, QString(), tr("File already exists. Overwrite?"),
                              QMessageBox::Yes|QMessageBox::No) == QMessageBox::No)
        return;
    QFile htmlFile(filename);
    if (htmlFile.open(QIODevice::WriteOnly)) {
        QTextStream stream(&htmlFile);
        stream << htmlText;
        htmlFile.close();
        if (htmlFile.error() == QFile::WriteError)
            QMessageBox::critical(this, QString(), tr("Could not save the report."));
    } else {
        QMessageBox::critical(this, QString(), tr("Could not save the report."));
    }
}
