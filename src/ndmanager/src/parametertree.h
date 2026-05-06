#ifndef PARAMETERTREE_H
#define PARAMETERTREE_H

#include <QTreeWidget>

class ParameterTreeItem : public QTreeWidgetItem
{
public:
    explicit ParameterTreeItem(QTreeWidget * parent = 0);
    explicit ParameterTreeItem(QTreeWidgetItem *parent);
    ~ParameterTreeItem();

    void setWidget(QWidget *w);
    QWidget *widget() const;
private:
    QWidget *mWidget;
};

class ParameterTree : public QTreeWidget
{
    Q_OBJECT
public:
    explicit ParameterTree(QWidget *parent = 0);
    ~ParameterTree();

    QTreeWidgetItem* addPage(const QString &icon, const QString &name, QWidget *page);
    QTreeWidgetItem* addSubPage(QTreeWidgetItem *parentItem, const QString &name, QWidget *page);

Q_SIGNALS:
    void showWidgetPage(QWidget *);

protected:
    // PageUp / PageDown navigate between top-level sections (General
    // Information, Acquisition System, Video, ...).  Plain Up/Down keep
    // their default QTreeWidget behaviour — single-item navigation that
    // walks into expanded subtrees (e.g. individual Plugin children).
    // Putting "step between sections" on PageUp/PageDown lets a user
    // with the Plugins subtree expanded jump straight to Pipeline (the
    // next top-level entry) without having to step through every plugin
    // child or first collapse the tree.
    void keyPressEvent(QKeyEvent *event) override;

private Q_SLOTS:
    void slotItemClicked(QTreeWidgetItem*,int);
    void slotSelectionChanged();
};

#endif // PARAMETERTREE_H
