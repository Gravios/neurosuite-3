/*  This file is part of the KDE project
    Copyright (C) 2007 Matthias Kretz <kretz@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License version 2 as published by the Free Software Foundation.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.

*/

#ifndef KDEUI_KDIALOG_P_H
#define KDEUI_KDIALOG_P_H

#include "qextenddialog.h"
#include <QtCore/QPointer>
#include <QtCore/QSignalMapper>
#include <QtCore/QSize>
#include <QtCore/QHash>

class QBoxLayout;
class QPushButton;
//class KUrlLabel;
class KLusterSeparator;
class QDialogButtonBox;

class QExtendDialogPrivate
{
    Q_DECLARE_PUBLIC(QExtendDialog)
    protected:
        QExtendDialogPrivate()
            : mDetailsVisible(false), mSettingDetails(false), mDeferredDelete(false),
            mDetailsWidget(0),
            mTopLayout(0), mMainWidget(0)
          //, mUrlHelp(0)
          , mActionSeparator(0),
            mButtonOrientation(Qt::Horizontal),
            mDefaultButton(QExtendDialog::NoDefault),
            mButtonBox(0)
        {
        }

        virtual ~QExtendDialogPrivate() {}

        QExtendDialog *q_ptr;

        void setupLayout();
        void appendButton(QExtendDialog::ButtonCode code , const QString &item );
        QPushButton *button( QExtendDialog::ButtonCode code ) const;


        bool mDetailsVisible;
        bool mSettingDetails;
        bool mDeferredDelete;
        QWidget *mDetailsWidget;
        QSize mIncSize;
        QSize mMinSize;
        QString mDetailsButtonText;

        QBoxLayout *mTopLayout;
        QPointer<QWidget> mMainWidget;
        //KUrlLabel *mUrlHelp;
        KLusterSeparator *mActionSeparator;

        QString mAnchor;
        QString mHelpApp;
        QString mHelpLinkText;

        Qt::Orientation mButtonOrientation;
        QExtendDialog::ButtonCode mDefaultButton;
        QExtendDialog::ButtonCode mEscapeButton;

        QDialogButtonBox *mButtonBox;
        QHash<int, QPushButton*> mButtonList;
        QSignalMapper mButtonSignalMapper;

    protected Q_SLOTS:
        void queuedLayoutUpdate();
        void helpLinkClicked();

    private:
        void init(QExtendDialog *);
        bool dirty: 1;
};

#endif // KDEUI_KDIALOG_P_H
