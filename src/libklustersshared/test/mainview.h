#ifndef MAINVIEW_H
#define MAINVIEW_H

#include <QWidget>
class ScrollArea;
class MainView : public QWidget
{
    Q_OBJECT
public:
    explicit MainView(QWidget *parent=0);

public Q_SLOTS:
    void slotAddListWidget();

private:
    ScrollArea *scrollArea;
    int i;
};

#endif // MAINVIEW_H
