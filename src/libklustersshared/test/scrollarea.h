#ifndef SCROLLAREA_H
#define SCROLLAREA_H

#include <QScrollArea>
#include <QLabel>
#include <QMouseEvent>

class QVBoxLayout;
class ListWidget;
class ItemGroupView;
class ScrollArea : public QScrollArea
{
    Q_OBJECT
public:
    explicit ScrollArea(QWidget *parent=0);

    void createItemList(const QString &groupName);
    void createGroup(const QString &id);
    void updateItemList(const QString& groupName);

    void orderTheGroups();
protected:
    void resizeEvent(QResizeEvent* event);

Q_SIGNALS:
    void paletteResized(int parentWidth,int labelSize);


private:
    /**Dictionnary of the iconviews representing the group of items.*/
    QHash<QString, ListWidget*> iconviewDict;
    /**Dictionnary of layout containing the iconviews.*/
    QHash<QString, ItemGroupView*> itemGroupViewDict;
    /**List used to order the event groups.*/
    QStringList itemGroupList;


    QVBoxLayout* verticalContainer;
    /**Dummy widget used to keep the iconviews nicely display in the pannel.*/
    QWidget* spaceWidget;
    QColor backgroundColor;
    int labelSize;
};

class GroupNameLabel : public QLabel{
    Q_OBJECT
public:
    GroupNameLabel(const QString& text,QWidget* parent):
        QLabel(text,parent){}

Q_SIGNALS:
    void leftClickOnLabel(const QString& sourceId,bool shiftKey,bool ctrlAlt);
    void middleClickOnLabel(const QString& sourceId);

protected:
    void mousePressEvent(QMouseEvent* e){
        if(e->button() == Qt::LeftButton && !(e->modifiers() & Qt::ShiftModifier) && !(e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::AltModifier)){
            emit leftClickOnLabel(parent()->objectName(),false,false);
        }
        if(e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier) && !(e->modifiers() & Qt::ControlModifier) && !(e->modifiers() & Qt::AltModifier)){
            emit leftClickOnLabel(parent()->objectName(),true,false);
        }
        if(e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier) && (e->modifiers() & Qt::AltModifier)){
            emit leftClickOnLabel(parent()->objectName(),false,true);
        }
        if(e->button() == Qt::MidButton){
            emit middleClickOnLabel(parent()->objectName());
        }
    }

};

#endif // SCROLLAREA_H
