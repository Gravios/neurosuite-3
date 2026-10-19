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
#include <QPalette>
#include <QTextDocument>

QHelpViewer::QHelpViewer(QWidget *parent)
    : QDialog(parent)
    , mView(new QTextBrowser(this))
{
    setWindowTitle(tr("Handbook"));

    auto *lay = new QVBoxLayout(this);
    mView->setOpenExternalLinks(false);
    mView->setOpenLinks(false);

    // The handbook HTML is authored as a light page (it sets BGCOLOR="#FFFFFF"
    // TEXT="#000000").  QTextBrowser honours the TEXT colour but paints the
    // widget background from QPalette::Base, which follows the application /
    // desktop theme — so under a dark theme the help renders black-on-dark and
    // "doesn't match the theme".  Pin the viewer to the light scheme the pages
    // expect, independent of the surrounding palette.
    QPalette pal = mView->palette();
    pal.setColor(QPalette::Base, Qt::white);
    pal.setColor(QPalette::Text, Qt::black);
    mView->setPalette(pal);

    lay->addWidget(mView);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    lay->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QHelpViewer::reject);
    connect(mView, &QTextBrowser::anchorClicked, this, &QHelpViewer::slotLinkClicked);
}

QHelpViewer::~QHelpViewer() = default;

void QHelpViewer::setHtml(const QString &filename, const QString &anchor)
{
    // A Markdown handbook (Klusters, NDManager: their README.md) is rendered by
    // QTextBrowser itself; links to other .md files open in the viewer too.
    if (filename.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)) {
        mView->setSearchPaths({QFileInfo(filename).absolutePath()});
        mView->setSource(QUrl::fromLocalFile(filename), QTextDocument::MarkdownResource);
        if (!anchor.isEmpty())
            mView->scrollToAnchor(anchor);
        return;
    }
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
    // Handbook pages are chunked DocBook HTML that cross-link with RELATIVE
    // hrefs (e.g. "x325.html#direct-selection").  QTextBrowser emits those as
    // relative QUrls; handing a relative URL to QDesktopServices::openUrl makes
    // the OS opener resolve it against the process working directory (the data
    // dir NeuroScope was launched from), producing "No such file" errors.
    // Resolve relative links against the handbook directory (the search path
    // set in setHtml) and navigate INSIDE the viewer; only genuinely external
    // links (http/https/mailto/…) go out to the desktop.
    if (url.isRelative() && url.path().isEmpty() && url.hasFragment()) {
        mView->scrollToAnchor(url.fragment());   // "#section" within this page
        return;
    }
    QUrl target = url;
    if (target.isRelative()) {
        const QStringList paths = mView->searchPaths();
        if (!paths.isEmpty())
            target = QUrl::fromLocalFile(paths.first() + QLatin1Char('/')).resolved(url);
    }
    if (target.isRelative() || target.scheme() == QLatin1String("file")) {
        // A page the install does not carry (e.g. a link from a README to a
        // developer document) stays put rather than blanking the viewer.
        if (target.isLocalFile() && !QFileInfo::exists(target.toLocalFile()))
            return;
        mView->setSource(target);
    }
    else
        QDesktopServices::openUrl(target);
}
