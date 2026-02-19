/*
Copyright (C) 2012 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
Modernized for Qt6 - uses QTextBrowser instead of removed QWebView.
*/

#ifndef QHELPVIEWER_H
#define QHELPVIEWER_H

#include <QDialog>
#include <QUrl>
#include "libklustersshared_export.h"

class QTextBrowser;

class KLUSTERSSHARED_EXPORT QHelpViewer : public QDialog
{
    Q_OBJECT
public:
    explicit QHelpViewer(QWidget *parent = nullptr);
    ~QHelpViewer() override;

    void setHtml(const QString &filename, const QString &anchor = QString());

private Q_SLOTS:
    void slotLinkClicked(const QUrl &url);

private:
    QTextBrowser *mView;
};

#endif // QHELPVIEWER_H
