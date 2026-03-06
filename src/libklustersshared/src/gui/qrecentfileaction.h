/*
Copyright (C) 2012 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
*/

#ifndef QRECENTFILEACTION_H
#define QRECENTFILEACTION_H

#include "libklustersshared_export.h"
#include <qaction.h>

QT_BEGIN_NAMESPACE


class QRecentFileActionPrivate;

class KLUSTERSSHARED_EXPORT QRecentFileAction : public QAction
{
    Q_OBJECT
    Q_PROPERTY(int maximumFileCount READ maximumFileCount WRITE setMaximumFileCount)
    Q_PROPERTY(QStringList recentFiles READ recentFiles WRITE setRecentFiles)
public:
    explicit QRecentFileAction(QObject *parent = nullptr);
    ~QRecentFileAction();

    void setRecentFiles(const QStringList& lst);
    QStringList recentFiles() const;
    void addRecentFile(const QString &file);
    void removeRecentFile(const QString &file);
    void save();
    int maximumFileCount() const;
    void setMaximumFileCount(int);

public Q_SLOTS:
    void clear();

Q_SIGNALS:
    void recentFileSelected(const QString &);
    void recentFileCleared();
    void recentFileListChanged();

private Q_SLOTS:
    void initializeRecentMenu();
    void fileSelected(QAction *action);

private:
    QRecentFileActionPrivate * const d;
    friend class QRecentFileActionPrivate;
    Q_DISABLE_COPY(QRecentFileAction)
};

QT_END_NAMESPACE

#endif // QRECENTFILEACTION_H
