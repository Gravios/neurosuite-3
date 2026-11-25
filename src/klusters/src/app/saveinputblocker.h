/* saveinputblocker.h -- keeps the curator's input away from the document while
 * a save runs.
 *
 * The save writes a snapshot taken when it started, on the SaveThread.  Edits
 * made meanwhile would not be in the files, and the GUI-thread state the save
 * finishes with (sibling paths, the modified flag) would race them.  While
 * installed on the application, this filter swallows keyboard, mouse, wheel,
 * touch, drag-and-drop and shortcut events for every widget except those inside
 * @p allowed (the "Saving…" dialog).  Paint, timers, posted events and window
 * close requests still go through: the views keep drawing, and closing the
 * window waits for the save (KlustersApp::waitForSaveToFinish).
 */
#ifndef KLUSTERS_SAVEINPUTBLOCKER_H
#define KLUSTERS_SAVEINPUTBLOCKER_H

#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QWidget>

class SaveInputBlocker : public QObject {
public:
    explicit SaveInputBlocker(QObject* parent = nullptr) : QObject(parent) {}

    /** Input to @p w and its children is let through (the progress dialog). */
    void setAllowed(QWidget* w) { allowed = w; }

    static bool isUserInput(QEvent::Type t)
    {
        switch (t) {
        case QEvent::KeyPress: case QEvent::KeyRelease:
        case QEvent::ShortcutOverride: case QEvent::Shortcut:
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick: case QEvent::MouseMove:
        case QEvent::Wheel: case QEvent::ContextMenu:
        case QEvent::DragEnter: case QEvent::DragMove: case QEvent::Drop:
        case QEvent::TouchBegin: case QEvent::TouchUpdate: case QEvent::TouchEnd:
        case QEvent::TabletPress: case QEvent::TabletRelease: case QEvent::TabletMove:
        case QEvent::InputMethod: case QEvent::NativeGesture:
            return true;
        default:
            return false;
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!isUserInput(event->type())) return false;
        if (allowed) {
            for (QObject* o = watched; o; o = o->parent())
                if (o == allowed) return false;
        }
        return true;                     // swallowed while the save runs
    }

private:
    QPointer<QWidget> allowed;
};

#endif // KLUSTERS_SAVEINPUTBLOCKER_H
