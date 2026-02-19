/*
Copyright (C) 2012 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
Modernized for Qt6 - QWebView was removed; use QTextBrowser with openExternalLinks.
*/

#include "qhelpviewer.h"

#include <QVBoxLayout>
#include <QTextBrowser>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>

QHelpViewer::QHelpViewer(QWidget *parent)
    : QDialog(parent)
    , mView(new QTextBrowser(this))
{
    setWindowTitle(tr("Handbook"));

    auto *lay = new QVBoxLayout(this);
    mView->setOpenExternalLinks(false);
    mView->setOpenLinks(false);
    lay->addWidget(mView);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    lay->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QHelpViewer::reject);
    connect(mView, &QTextBrowser::anchorClicked, this, &QHelpViewer::slotLinkClicked);
}

QHelpViewer::~QHelpViewer() = default;

void QHelpViewer::setHtml(const QString &filename, const QString &anchor)
{
    QFile f(filename);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Set search path so relative links/images resolve correctly
        mView->setSearchPaths({QFileInfo(filename).absolutePath()});
        mView->setHtml(QString::fromUtf8(f.readAll()));
    } else {
        mView->setSource(QUrl::fromLocalFile(filename));
    }
    if (!anchor.isEmpty())
        mView->scrollToAnchor(anchor);
}

void QHelpViewer::slotLinkClicked(const QUrl &url)
{
    if (url.scheme() == QLatin1String("file")) {
        mView->setSource(url);
    } else {
        QDesktopServices::openUrl(url);
    }
}
