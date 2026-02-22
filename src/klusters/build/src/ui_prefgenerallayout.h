/********************************************************************************
** Form generated from reading UI file 'prefgenerallayout.ui'
**
** Created by: Qt User Interface Compiler version 6.4.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PREFGENERALLAYOUT_H
#define UI_PREFGENERALLAYOUT_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <qcolorbutton.h>

QT_BEGIN_NAMESPACE

class Ui_PrefGeneralLayout
{
public:
    QGridLayout *gridLayout;
    QSpacerItem *spacer1;
    QHBoxLayout *hboxLayout;
    QSpacerItem *spacer17;
    QGroupBox *groupBox2;
    QGridLayout *gridLayout_3;
    QHBoxLayout *hboxLayout1;
    QLabel *textLabel4;
    QSpinBox *undoSpinBox;
    QSpacerItem *spacer7;
    QHBoxLayout *hboxLayout2;
    QSpacerItem *spacer17_2;
    QGroupBox *groupBox_realign;
    QGridLayout *gridLayout_realign;
    QVBoxLayout *vboxLayout;
    QHBoxLayout *hboxLayout3;
    QLabel *realignExeLabel;
    QLineEdit *realignExecutableLineEdit;
    QPushButton *realignExecutableButton;
    QHBoxLayout *hboxLayout4;
    QLabel *realignArgsLabel;
    QLineEdit *realignArgsLineEdit;
    QHBoxLayout *hboxLayout5;
    QSpacerItem *spacer16;
    QGroupBox *groupBox1;
    QGridLayout *gridLayout_4;
    QHBoxLayout *hboxLayout6;
    QCheckBox *crashRecoveryCheckBox;
    QSpacerItem *spacer6;
    QHBoxLayout *hboxLayout7;
    QLabel *textLabel3;
    QComboBox *crashRecoveryComboBox;
    QSpacerItem *spacer5;
    QHBoxLayout *hboxLayout8;
    QSpacerItem *spacer17_3;
    QGroupBox *groupBox2_2;
    QGridLayout *gridLayout_2;
    QVBoxLayout *vboxLayout1;
    QHBoxLayout *hboxLayout9;
    QLabel *textLabel4_2;
    QLineEdit *reclusteringExecutableLineEdit;
    QPushButton *reclusteringExecutableButton;
    QHBoxLayout *hboxLayout10;
    QLabel *textLabel1_2;
    QLineEdit *reclusteringArgsLineEdit;
    QGroupBox *groupBox3;
    QGridLayout *gridLayout1;
    QHBoxLayout *hboxLayout11;
    QLabel *textLabel1;
    QColorButton *backgroundColorButton;
    QCheckBox *useWhiteColorPrinting;

    void setupUi(QWidget *PrefGeneralLayout)
    {
        if (PrefGeneralLayout->objectName().isEmpty())
            PrefGeneralLayout->setObjectName("PrefGeneralLayout");
        PrefGeneralLayout->resize(450, 514);
        gridLayout = new QGridLayout(PrefGeneralLayout);
        gridLayout->setSpacing(6);
        gridLayout->setContentsMargins(11, 11, 11, 11);
        gridLayout->setObjectName("gridLayout");
        spacer1 = new QSpacerItem(21, 30, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout->addItem(spacer1, 6, 0, 1, 1);

        hboxLayout = new QHBoxLayout();
        hboxLayout->setSpacing(6);
        hboxLayout->setObjectName("hboxLayout");
        spacer17 = new QSpacerItem(16, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        hboxLayout->addItem(spacer17);

        groupBox2 = new QGroupBox(PrefGeneralLayout);
        groupBox2->setObjectName("groupBox2");
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(groupBox2->sizePolicy().hasHeightForWidth());
        groupBox2->setSizePolicy(sizePolicy);
        groupBox2->setMaximumSize(QSize(32767, 70));
        gridLayout_3 = new QGridLayout(groupBox2);
        gridLayout_3->setSpacing(6);
        gridLayout_3->setContentsMargins(11, 11, 11, 11);
        gridLayout_3->setObjectName("gridLayout_3");
        hboxLayout1 = new QHBoxLayout();
        hboxLayout1->setSpacing(6);
        hboxLayout1->setContentsMargins(0, 0, 0, 0);
        hboxLayout1->setObjectName("hboxLayout1");
        textLabel4 = new QLabel(groupBox2);
        textLabel4->setObjectName("textLabel4");
        textLabel4->setWordWrap(false);

        hboxLayout1->addWidget(textLabel4);

        undoSpinBox = new QSpinBox(groupBox2);
        undoSpinBox->setObjectName("undoSpinBox");
        undoSpinBox->setWrapping(true);
        undoSpinBox->setMaximum(30);
        undoSpinBox->setValue(2);

        hboxLayout1->addWidget(undoSpinBox);

        spacer7 = new QSpacerItem(81, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        hboxLayout1->addItem(spacer7);


        gridLayout_3->addLayout(hboxLayout1, 0, 0, 1, 1);


        hboxLayout->addWidget(groupBox2);


        gridLayout->addLayout(hboxLayout, 1, 0, 1, 1);

        hboxLayout2 = new QHBoxLayout();
        hboxLayout2->setSpacing(6);
        hboxLayout2->setObjectName("hboxLayout2");
        spacer17_2 = new QSpacerItem(16, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        hboxLayout2->addItem(spacer17_2);

        groupBox_realign = new QGroupBox(PrefGeneralLayout);
        groupBox_realign->setObjectName("groupBox_realign");
        QSizePolicy sizePolicy1(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(groupBox_realign->sizePolicy().hasHeightForWidth());
        groupBox_realign->setSizePolicy(sizePolicy1);
        groupBox_realign->setMaximumSize(QSize(32767, 140));
        gridLayout_realign = new QGridLayout(groupBox_realign);
        gridLayout_realign->setSpacing(6);
        gridLayout_realign->setContentsMargins(11, 11, 11, 11);
        gridLayout_realign->setObjectName("gridLayout_realign");
        vboxLayout = new QVBoxLayout();
        vboxLayout->setSpacing(0);
        vboxLayout->setObjectName("vboxLayout");
        hboxLayout3 = new QHBoxLayout();
        hboxLayout3->setSpacing(6);
        hboxLayout3->setObjectName("hboxLayout3");
        realignExeLabel = new QLabel(groupBox_realign);
        realignExeLabel->setObjectName("realignExeLabel");
        realignExeLabel->setWordWrap(false);

        hboxLayout3->addWidget(realignExeLabel);

        realignExecutableLineEdit = new QLineEdit(groupBox_realign);
        realignExecutableLineEdit->setObjectName("realignExecutableLineEdit");

        hboxLayout3->addWidget(realignExecutableLineEdit);

        realignExecutableButton = new QPushButton(groupBox_realign);
        realignExecutableButton->setObjectName("realignExecutableButton");
        QSizePolicy sizePolicy2(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(realignExecutableButton->sizePolicy().hasHeightForWidth());
        realignExecutableButton->setSizePolicy(sizePolicy2);
        realignExecutableButton->setMinimumSize(QSize(60, 29));

        hboxLayout3->addWidget(realignExecutableButton);


        vboxLayout->addLayout(hboxLayout3);

        hboxLayout4 = new QHBoxLayout();
        hboxLayout4->setSpacing(6);
        hboxLayout4->setObjectName("hboxLayout4");
        realignArgsLabel = new QLabel(groupBox_realign);
        realignArgsLabel->setObjectName("realignArgsLabel");
        realignArgsLabel->setWordWrap(false);

        hboxLayout4->addWidget(realignArgsLabel);

        realignArgsLineEdit = new QLineEdit(groupBox_realign);
        realignArgsLineEdit->setObjectName("realignArgsLineEdit");
        realignArgsLineEdit->setMinimumSize(QSize(0, 0));

        hboxLayout4->addWidget(realignArgsLineEdit);


        vboxLayout->addLayout(hboxLayout4);


        gridLayout_realign->addLayout(vboxLayout, 0, 0, 1, 1);


        hboxLayout2->addWidget(groupBox_realign);


        gridLayout->addLayout(hboxLayout2, 3, 0, 1, 1);

        hboxLayout5 = new QHBoxLayout();
        hboxLayout5->setSpacing(0);
        hboxLayout5->setObjectName("hboxLayout5");
        spacer16 = new QSpacerItem(16, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        hboxLayout5->addItem(spacer16);

        groupBox1 = new QGroupBox(PrefGeneralLayout);
        groupBox1->setObjectName("groupBox1");
        sizePolicy.setHeightForWidth(groupBox1->sizePolicy().hasHeightForWidth());
        groupBox1->setSizePolicy(sizePolicy);
        groupBox1->setMinimumSize(QSize(370, 0));
        groupBox1->setMaximumSize(QSize(32767, 120));
        gridLayout_4 = new QGridLayout(groupBox1);
        gridLayout_4->setSpacing(6);
        gridLayout_4->setContentsMargins(11, 11, 11, 11);
        gridLayout_4->setObjectName("gridLayout_4");
        hboxLayout6 = new QHBoxLayout();
        hboxLayout6->setSpacing(6);
        hboxLayout6->setContentsMargins(0, 0, 0, 0);
        hboxLayout6->setObjectName("hboxLayout6");
        crashRecoveryCheckBox = new QCheckBox(groupBox1);
        crashRecoveryCheckBox->setObjectName("crashRecoveryCheckBox");

        hboxLayout6->addWidget(crashRecoveryCheckBox);

        spacer6 = new QSpacerItem(16, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        hboxLayout6->addItem(spacer6);


        gridLayout_4->addLayout(hboxLayout6, 0, 0, 1, 1);

        hboxLayout7 = new QHBoxLayout();
        hboxLayout7->setSpacing(6);
        hboxLayout7->setContentsMargins(0, 0, 0, 0);
        hboxLayout7->setObjectName("hboxLayout7");
        textLabel3 = new QLabel(groupBox1);
        textLabel3->setObjectName("textLabel3");
        textLabel3->setWordWrap(false);

        hboxLayout7->addWidget(textLabel3);

        crashRecoveryComboBox = new QComboBox(groupBox1);
        crashRecoveryComboBox->addItem(QString());
        crashRecoveryComboBox->addItem(QString());
        crashRecoveryComboBox->addItem(QString());
        crashRecoveryComboBox->addItem(QString());
        crashRecoveryComboBox->addItem(QString());
        crashRecoveryComboBox->setObjectName("crashRecoveryComboBox");

        hboxLayout7->addWidget(crashRecoveryComboBox);

        spacer5 = new QSpacerItem(326, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        hboxLayout7->addItem(spacer5);


        gridLayout_4->addLayout(hboxLayout7, 1, 0, 1, 1);


        hboxLayout5->addWidget(groupBox1);


        gridLayout->addLayout(hboxLayout5, 0, 0, 1, 1);

        hboxLayout8 = new QHBoxLayout();
        hboxLayout8->setSpacing(6);
        hboxLayout8->setObjectName("hboxLayout8");
        spacer17_3 = new QSpacerItem(16, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);

        hboxLayout8->addItem(spacer17_3);

        groupBox2_2 = new QGroupBox(PrefGeneralLayout);
        groupBox2_2->setObjectName("groupBox2_2");
        sizePolicy1.setHeightForWidth(groupBox2_2->sizePolicy().hasHeightForWidth());
        groupBox2_2->setSizePolicy(sizePolicy1);
        groupBox2_2->setMaximumSize(QSize(32767, 140));
        gridLayout_2 = new QGridLayout(groupBox2_2);
        gridLayout_2->setSpacing(6);
        gridLayout_2->setContentsMargins(11, 11, 11, 11);
        gridLayout_2->setObjectName("gridLayout_2");
        vboxLayout1 = new QVBoxLayout();
        vboxLayout1->setSpacing(0);
        vboxLayout1->setObjectName("vboxLayout1");
        hboxLayout9 = new QHBoxLayout();
        hboxLayout9->setSpacing(6);
        hboxLayout9->setObjectName("hboxLayout9");
        textLabel4_2 = new QLabel(groupBox2_2);
        textLabel4_2->setObjectName("textLabel4_2");
        textLabel4_2->setWordWrap(false);

        hboxLayout9->addWidget(textLabel4_2);

        reclusteringExecutableLineEdit = new QLineEdit(groupBox2_2);
        reclusteringExecutableLineEdit->setObjectName("reclusteringExecutableLineEdit");

        hboxLayout9->addWidget(reclusteringExecutableLineEdit);

        reclusteringExecutableButton = new QPushButton(groupBox2_2);
        reclusteringExecutableButton->setObjectName("reclusteringExecutableButton");
        sizePolicy2.setHeightForWidth(reclusteringExecutableButton->sizePolicy().hasHeightForWidth());
        reclusteringExecutableButton->setSizePolicy(sizePolicy2);
        reclusteringExecutableButton->setMinimumSize(QSize(60, 29));

        hboxLayout9->addWidget(reclusteringExecutableButton);


        vboxLayout1->addLayout(hboxLayout9);

        hboxLayout10 = new QHBoxLayout();
        hboxLayout10->setSpacing(6);
        hboxLayout10->setObjectName("hboxLayout10");
        textLabel1_2 = new QLabel(groupBox2_2);
        textLabel1_2->setObjectName("textLabel1_2");
        textLabel1_2->setWordWrap(false);

        hboxLayout10->addWidget(textLabel1_2);

        reclusteringArgsLineEdit = new QLineEdit(groupBox2_2);
        reclusteringArgsLineEdit->setObjectName("reclusteringArgsLineEdit");
        reclusteringArgsLineEdit->setMinimumSize(QSize(0, 0));

        hboxLayout10->addWidget(reclusteringArgsLineEdit);


        vboxLayout1->addLayout(hboxLayout10);


        gridLayout_2->addLayout(vboxLayout1, 0, 0, 1, 1);


        hboxLayout8->addWidget(groupBox2_2);


        gridLayout->addLayout(hboxLayout8, 2, 0, 1, 1);

        groupBox3 = new QGroupBox(PrefGeneralLayout);
        groupBox3->setObjectName("groupBox3");
        sizePolicy.setHeightForWidth(groupBox3->sizePolicy().hasHeightForWidth());
        groupBox3->setSizePolicy(sizePolicy);
        gridLayout1 = new QGridLayout(groupBox3);
        gridLayout1->setSpacing(6);
        gridLayout1->setContentsMargins(11, 11, 11, 11);
        gridLayout1->setObjectName("gridLayout1");
        hboxLayout11 = new QHBoxLayout();
        hboxLayout11->setSpacing(6);
        hboxLayout11->setContentsMargins(0, 0, 0, 0);
        hboxLayout11->setObjectName("hboxLayout11");
        textLabel1 = new QLabel(groupBox3);
        textLabel1->setObjectName("textLabel1");
        textLabel1->setWordWrap(false);

        hboxLayout11->addWidget(textLabel1);

        backgroundColorButton = new QColorButton(groupBox3);
        backgroundColorButton->setObjectName("backgroundColorButton");
        sizePolicy.setHeightForWidth(backgroundColorButton->sizePolicy().hasHeightForWidth());
        backgroundColorButton->setSizePolicy(sizePolicy);
        backgroundColorButton->setMinimumSize(QSize(200, 30));

        hboxLayout11->addWidget(backgroundColorButton);


        gridLayout1->addLayout(hboxLayout11, 0, 0, 1, 1);

        useWhiteColorPrinting = new QCheckBox(groupBox3);
        useWhiteColorPrinting->setObjectName("useWhiteColorPrinting");

        gridLayout1->addWidget(useWhiteColorPrinting, 1, 0, 1, 1);


        gridLayout->addWidget(groupBox3, 4, 0, 1, 1);


        retranslateUi(PrefGeneralLayout);

        QMetaObject::connectSlotsByName(PrefGeneralLayout);
    } // setupUi

    void retranslateUi(QWidget *PrefGeneralLayout)
    {
        PrefGeneralLayout->setWindowTitle(QCoreApplication::translate("PrefGeneralLayout", "General", nullptr));
        groupBox2->setTitle(QCoreApplication::translate("PrefGeneralLayout", "Undo", nullptr));
        textLabel4->setText(QCoreApplication::translate("PrefGeneralLayout", "Number of steps", nullptr));
        groupBox_realign->setTitle(QCoreApplication::translate("PrefGeneralLayout", "Realignment", nullptr));
        realignExeLabel->setText(QCoreApplication::translate("PrefGeneralLayout", "Executable", nullptr));
        realignExecutableButton->setText(QString());
        realignArgsLabel->setText(QCoreApplication::translate("PrefGeneralLayout", "Arguments", nullptr));
        groupBox1->setTitle(QCoreApplication::translate("PrefGeneralLayout", "Crash and recovery", nullptr));
        crashRecoveryCheckBox->setText(QCoreApplication::translate("PrefGeneralLayout", "Periodically save data to a recovery file", nullptr));
        textLabel3->setText(QCoreApplication::translate("PrefGeneralLayout", "Time-interval", nullptr));
        crashRecoveryComboBox->setItemText(0, QCoreApplication::translate("PrefGeneralLayout", "1 min", nullptr));
        crashRecoveryComboBox->setItemText(1, QCoreApplication::translate("PrefGeneralLayout", "3 min", nullptr));
        crashRecoveryComboBox->setItemText(2, QCoreApplication::translate("PrefGeneralLayout", "5 min", nullptr));
        crashRecoveryComboBox->setItemText(3, QCoreApplication::translate("PrefGeneralLayout", "15 min", nullptr));
        crashRecoveryComboBox->setItemText(4, QCoreApplication::translate("PrefGeneralLayout", "30 min", nullptr));

        groupBox2_2->setTitle(QCoreApplication::translate("PrefGeneralLayout", "Reclustering", nullptr));
        textLabel4_2->setText(QCoreApplication::translate("PrefGeneralLayout", "Executable", nullptr));
        reclusteringExecutableButton->setText(QString());
        textLabel1_2->setText(QCoreApplication::translate("PrefGeneralLayout", "Arguments", nullptr));
        groupBox3->setTitle(QCoreApplication::translate("PrefGeneralLayout", "Miscellaneous", nullptr));
        textLabel1->setText(QCoreApplication::translate("PrefGeneralLayout", "Background color", nullptr));
        backgroundColorButton->setText(QString());
        useWhiteColorPrinting->setText(QCoreApplication::translate("PrefGeneralLayout", "Use white background when printing", nullptr));
    } // retranslateUi

};

namespace Ui {
    class PrefGeneralLayout: public Ui_PrefGeneralLayout {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PREFGENERALLAYOUT_H
