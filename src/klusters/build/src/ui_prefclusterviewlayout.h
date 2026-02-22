/********************************************************************************
** Form generated from reading UI file 'prefclusterviewlayout.ui'
**
** Created by: Qt User Interface Compiler version 6.4.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PREFCLUSTERVIEWLAYOUT_H
#define UI_PREFCLUSTERVIEWLAYOUT_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_PrefClusterViewLayout
{
public:
    QGridLayout *gridLayout;
    QSpacerItem *spacer21;
    QGridLayout *gridLayout1;
    QSpacerItem *spacer18;
    QGroupBox *groupBox3;
    QGridLayout *gridLayout2;
    QHBoxLayout *hboxLayout;
    QLabel *textLabel6;
    QSpinBox *intervalSpinBox;
    QSpacerItem *spacer19;

    void setupUi(QWidget *PrefClusterViewLayout)
    {
        if (PrefClusterViewLayout->objectName().isEmpty())
            PrefClusterViewLayout->setObjectName("PrefClusterViewLayout");
        PrefClusterViewLayout->resize(377, 155);
        gridLayout = new QGridLayout(PrefClusterViewLayout);
        gridLayout->setSpacing(6);
        gridLayout->setContentsMargins(11, 11, 11, 11);
        gridLayout->setObjectName("gridLayout");
        spacer21 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout->addItem(spacer21, 1, 0, 1, 1);

        gridLayout1 = new QGridLayout();
        gridLayout1->setSpacing(6);
        gridLayout1->setObjectName("gridLayout1");
        spacer18 = new QSpacerItem(16, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        gridLayout1->addItem(spacer18, 0, 0, 1, 1);

        groupBox3 = new QGroupBox(PrefClusterViewLayout);
        groupBox3->setObjectName("groupBox3");
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(groupBox3->sizePolicy().hasHeightForWidth());
        groupBox3->setSizePolicy(sizePolicy);
        groupBox3->setMinimumSize(QSize(320, 70));
        gridLayout2 = new QGridLayout(groupBox3);
        gridLayout2->setSpacing(6);
        gridLayout2->setContentsMargins(11, 11, 11, 11);
        gridLayout2->setObjectName("gridLayout2");
        hboxLayout = new QHBoxLayout();
        hboxLayout->setSpacing(6);
        hboxLayout->setContentsMargins(0, 0, 0, 0);
        hboxLayout->setObjectName("hboxLayout");
        textLabel6 = new QLabel(groupBox3);
        textLabel6->setObjectName("textLabel6");
        textLabel6->setMinimumSize(QSize(0, 15));
        textLabel6->setWordWrap(false);

        hboxLayout->addWidget(textLabel6);

        intervalSpinBox = new QSpinBox(groupBox3);
        intervalSpinBox->setObjectName("intervalSpinBox");
        intervalSpinBox->setMinimumSize(QSize(60, 0));
        intervalSpinBox->setWrapping(true);
        intervalSpinBox->setMinimum(60);
        intervalSpinBox->setMaximum(6000);
        intervalSpinBox->setSingleStep(60);

        hboxLayout->addWidget(intervalSpinBox);

        spacer19 = new QSpacerItem(60, 21, QSizePolicy::Expanding, QSizePolicy::Minimum);

        hboxLayout->addItem(spacer19);


        gridLayout2->addLayout(hboxLayout, 0, 0, 1, 1);


        gridLayout1->addWidget(groupBox3, 0, 1, 1, 1);


        gridLayout->addLayout(gridLayout1, 0, 0, 1, 1);


        retranslateUi(PrefClusterViewLayout);

        QMetaObject::connectSlotsByName(PrefClusterViewLayout);
    } // setupUi

    void retranslateUi(QWidget *PrefClusterViewLayout)
    {
        PrefClusterViewLayout->setWindowTitle(QCoreApplication::translate("PrefClusterViewLayout", "ClusterView", nullptr));
        groupBox3->setTitle(QCoreApplication::translate("PrefClusterViewLayout", "Time lines", nullptr));
        textLabel6->setText(QCoreApplication::translate("PrefClusterViewLayout", "Time interval (in seconds)", nullptr));
    } // retranslateUi

};

namespace Ui {
    class PrefClusterViewLayout: public Ui_PrefClusterViewLayout {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PREFCLUSTERVIEWLAYOUT_H
