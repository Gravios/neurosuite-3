#ifndef LISTVIEW_H
#define LISTVIEW_H

#include <QListWidget>

class ListWidget : public QListWidget
{
    Q_OBJECT
public:
    explicit ListWidget(QWidget *parent=0);

    void setNewWidth(int width);
    
    virtual QSize sizeHint() const;
};

#endif // LISTVIEW_H
