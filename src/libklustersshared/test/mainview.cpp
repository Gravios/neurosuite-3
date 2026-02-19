#include "mainview.h"
#include "scrollarea.h"
#include <QSplitter>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>

MainView::MainView(QWidget *parent)
    : QWidget(parent), i(0)
{
    QHBoxLayout *lay = new QHBoxLayout;
    QSplitter *splitter = new QSplitter;

    scrollArea = new ScrollArea;
    splitter->addWidget(scrollArea);

    QWidget *w = new QWidget;
    QVBoxLayout *vbox = new QVBoxLayout;
    QPushButton *addListWidget = new QPushButton("add list widget");
    connect(addListWidget, SIGNAL(clicked(bool)), SLOT(slotAddListWidget()));
    vbox->addWidget(addListWidget);
    vbox->addWidget(new QTextEdit);
    w->setLayout(vbox);

    splitter->addWidget(w);

    lay->addWidget(splitter);
    setLayout(lay);

    resize(800,600);
}

void MainView::slotAddListWidget()
{
    scrollArea->createItemList(QString::number(i));
    i++;
}
