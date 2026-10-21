/***************************************************************************
 *  appicon.cpp  -  see appicon.h
 ***************************************************************************/

#include "appicon.h"

#include <QFile>
#include <QGuiApplication>
#include <QIcon>

namespace neurosuite {

bool installApplicationIcon(const QString &desktopFileName)
{
    QIcon icon;
    for (const int size : {16, 22, 32, 48, 64, 128, 256}) {
        const QString path = QStringLiteral(":/app-icon/%1").arg(size);
        if (QFile::exists(path))
            icon.addFile(path, QSize(size, size));
    }
    if (icon.isNull())
        return false;
    QGuiApplication::setWindowIcon(icon);
    QGuiApplication::setDesktopFileName(desktopFileName);
    return true;
}

} // namespace neurosuite
