/***************************************************************************
 * realignreviewdialog.h
 *
 * Modal dialog shown after spike realignment completes.
 * Accept: caller keeps pending in-memory changes (written to disk on save).
 * Reject: caller calls doc->rejectLastRealign() to restore previous state.
 ***************************************************************************/

#pragma once

#include <QDialog>
#include <QVector>
#include <QString>

class QKeyEvent;
class QPushButton;

class RealignReviewDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RealignReviewDialog(int clusterId, int nShifted, int nSwapped,
                                 int nChan, int nSamp,
                                 const QVector<float>& meanBefore,
                                 const QVector<float>& meanAfter,
                                 const QString& backupBase,   // unused, API compat
                                 QWidget* parent = nullptr);

    bool isAccepted() const { return accepted; }

protected:
    /** Left/Right arrows move focus between Accept and Reject buttons. */
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void slotAccept();
    void slotReject();

private:
    bool         accepted  = false;
    QPushButton* acceptBtn = nullptr;
    QPushButton* rejectBtn = nullptr;
};

