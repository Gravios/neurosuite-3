/*  This file is part of the KDE Libraries
 *  Copyright (C) 1998 Thomas Tanghus (tanghus@earthling.net)
 *  Additions 1999-2000 by Espen Sand (espen@kde.org)
 *                      by Holger Freyther <freyther@kde.org>
 *            2005-2009 by Olivier Goffart (ogoffart at kde.org)
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#include "qextenddialog.h"
#include "qextenddialog_p.h"
#if 0
#include "kdialogqueue_p.h"
#endif
#include <QApplication>
#include <QGuiApplication>
#include <QFontMetricsF>
#include <QScreen>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QPointer>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWhatsThis>
#include <QPushButton>
#include <QSettings>
#include <QDebug>

#if 0

#include <klocale.h>
#include <kpushbutton.h>
#include <kseparator.h>
#include <kstandardguiitem.h>
#include <ktoolinvocation.h>
#include <kurllabel.h>
#endif

#include <klusterseparator.h>


static bool sAllowEmbeddingInGraphicsView = false;

void QExtendDialogPrivate::setupLayout()
{
    Q_Q(QExtendDialog);
    if (!dirty) {
        QMetaObject::invokeMethod( q, "queuedLayoutUpdate", Qt::QueuedConnection );
        dirty = true;
    }
}

void QExtendDialogPrivate::queuedLayoutUpdate()
{
  if (!dirty)
    return;

  dirty = false;

  Q_Q(QExtendDialog);

    // Don't lose the focus widget when re-creating the layout.
    // Testcase: KOrganizer's "Select Categories" dialog
    QPointer<QWidget> focusWidget = mMainWidget ? mMainWidget->focusWidget() : 0;

  if (q->layout() && q->layout() != mTopLayout) {
      qDebug() << q->metaObject()->className() << "created with a layout; don't do that, QExtendDialog takes care of it, use mainWidget or setMainWidget instead";
      delete q->layout();
  }

  delete mTopLayout;

  if ( mButtonOrientation == Qt::Horizontal )
        mTopLayout = new QVBoxLayout(q);
  else
        mTopLayout = new QHBoxLayout(q);
#if 0
  if ( mUrlHelp )
    mTopLayout->addWidget( mUrlHelp, 0, Qt::AlignRight );
#endif
  if ( mMainWidget )
    mTopLayout->addWidget( mMainWidget, 10 );

  if ( mDetailsWidget )
    mTopLayout->addWidget( mDetailsWidget );

  if ( mActionSeparator )
    mTopLayout->addWidget( mActionSeparator );

  if ( mButtonBox ) {
    mButtonBox->setOrientation( mButtonOrientation );
    mTopLayout->addWidget( mButtonBox );
  }

    if (focusWidget) {
        focusWidget->setFocus();
    }
}

void QExtendDialogPrivate::appendButton(QExtendDialog::ButtonCode key, const QString &item)
{
    Q_Q(QExtendDialog);

  QDialogButtonBox::ButtonRole role = QDialogButtonBox::InvalidRole;
  switch ( key ) {
    case QExtendDialog::Help:
    case QExtendDialog::Details:
      role = QDialogButtonBox::HelpRole;
      break;
    case QExtendDialog::Default:
    case QExtendDialog::Reset:
      role = QDialogButtonBox::ResetRole;
      break;
    case QExtendDialog::Ok:
      role = QDialogButtonBox::AcceptRole;
      break;
    case QExtendDialog::Apply:
      role = QDialogButtonBox::ApplyRole;
      break;
    case QExtendDialog::Try:
    case QExtendDialog::Yes:
      role = QDialogButtonBox::YesRole;
      break;
    case QExtendDialog::Close:
    case QExtendDialog::Cancel:
      role = QDialogButtonBox::RejectRole;
      break;
    case QExtendDialog::No:
      role = QDialogButtonBox::NoRole;
      break;
    case QExtendDialog::User1:
    case QExtendDialog::User2:
    case QExtendDialog::User3:
      role = QDialogButtonBox::ActionRole;
      break;
    default:
      role = QDialogButtonBox::InvalidRole;
      break;
  }

  if ( role == QDialogButtonBox::InvalidRole )
    return;

  QPushButton *button = new QPushButton( item );
  mButtonBox->addButton( button, role );
  switch(key) {
  case QExtendDialog::Ok:
      button->setIcon(QPixmap(":/shared-icons/dialog-ok"));
      break;
  case QExtendDialog::Apply:
      button->setIcon(QPixmap(":/shared-icons/dialog-ok-apply"));
      break;
  case QExtendDialog::Cancel:
      button->setIcon(QPixmap(":/shared-icons/dialog-cancel"));
      break;
  case QExtendDialog::Close:
      button->setIcon(QPixmap(":/shared-icons/dialog-close"));
      break;
  case QExtendDialog::Help:
      button->setIcon(QPixmap(":/shared-icons/help-contents"));
      break;
  default:
      break;
  }

  mButtonList.insert( key, button );
  mButtonSignalMapper.setMapping( button, key );

    QObject::connect(button, SIGNAL(clicked()),
           &mButtonSignalMapper, SLOT(map()) );

    if (key == mDefaultButton) {
        // Now that it exists, set it as default
        q->setDefaultButton(mDefaultButton);
    }
}

void QExtendDialogPrivate::init(QExtendDialog *q)
{
    q_ptr = q;

    dirty = false;

    q->setButtons(QExtendDialog::Ok | QExtendDialog::Cancel);
    q->setDefaultButton(QExtendDialog::Ok);

    // Qt6: QSignalMapper::mapped(int) was removed; use mappedInt instead.
    q->connect(&mButtonSignalMapper, &QSignalMapper::mappedInt,
               q, &QExtendDialog::slotButtonClicked);

    q->setPlainCaption(/*KGlobal::caption()*/QString()); // set appropriate initial window title for case it gets not set later
}

void QExtendDialogPrivate::helpLinkClicked()
{
    q_ptr->slotButtonClicked(QExtendDialog::Help);
}

QExtendDialog::QExtendDialog( QWidget *parent, Qt::WindowFlags flags )
  : QDialog(parent, sAllowEmbeddingInGraphicsView ? flags : flags | Qt::BypassGraphicsProxyWidget ), d_ptr(new QExtendDialogPrivate)
{
    d_ptr->init(this);
}

QExtendDialog::QExtendDialog(QExtendDialogPrivate &dd, QWidget *parent, Qt::WindowFlags flags)
    : QDialog(parent, sAllowEmbeddingInGraphicsView ? flags : flags | Qt::BypassGraphicsProxyWidget), d_ptr(&dd)
{
    d_ptr->init(this);
}

QExtendDialog::~QExtendDialog()
{
    delete d_ptr;
}

void QExtendDialog::setButtons( ButtonCodes buttonMask )
{
    Q_D(QExtendDialog);
  if ( d->mButtonBox ) {
    d->mButtonList.clear();

    delete d->mButtonBox;
    d->mButtonBox = 0;
  }

  if ( buttonMask & Cancel )
    buttonMask &= ~Close;

  if ( buttonMask & Apply )
    buttonMask &= ~Try;

  if ( buttonMask & Details )
    buttonMask &= ~Default;

  if ( buttonMask == None ) {
    d->setupLayout();
    return; // When we want no button box
  }

  d->mEscapeButton = (buttonMask & Cancel) ? Cancel : Close;
  d->mButtonBox = new QDialogButtonBox( this );

  if ( buttonMask & Help )
      d->appendButton( Help, tr("Help") );
  if ( buttonMask & Default )
    d->appendButton( Default, tr("Default") );
  if ( buttonMask & Reset )
    d->appendButton( Reset, tr("Reset") );
  if ( buttonMask & User3 )
      d->appendButton( User3, QString() );
  if ( buttonMask & User2 )
    d->appendButton( User2, QString() );
  if ( buttonMask & User1 )
    d->appendButton( User1, QString() );
  if ( buttonMask & Ok )
      d->appendButton( Ok, tr("Ok") );
  if ( buttonMask & Apply )
      d->appendButton( Apply, tr("Apply") );
  if ( buttonMask & Try )
    d->appendButton( Try, tr( "&Try" ) );
  if ( buttonMask & Cancel )
      d->appendButton( Cancel, tr("Cancel") );
  if ( buttonMask & Close )
      d->appendButton( Close, tr("Close") );
  if ( buttonMask & Yes )
      d->appendButton( Yes, tr("Yes") );
  if ( buttonMask & No )
      d->appendButton( No, tr("No") );
  if ( buttonMask & Details ) {
      d->appendButton( Details, /*KGuiItem(QString(), "help-about")*/QString() );
    setDetailsWidgetVisible( false );
  }

  d->setupLayout();
}


void QExtendDialog::setButtonsOrientation( Qt::Orientation orientation )
{
    Q_D(QExtendDialog);
  if ( d->mButtonOrientation != orientation ) {
    d->mButtonOrientation = orientation;

    if ( d->mActionSeparator )
      d->mActionSeparator->setOrientation( d->mButtonOrientation );

    /*
    if ( d->mButtonOrientation == Qt::Vertical )
      enableLinkedHelp( false ); // 2000-06-18 Espen: No support for this yet.
      */
  }
}

void QExtendDialog::setEscapeButton( ButtonCode id )
{
    d_func()->mEscapeButton = id;
}

void QExtendDialog::setDefaultButton( ButtonCode newDefaultButton )
{
    Q_D(QExtendDialog);

    if (newDefaultButton == None)
        newDefaultButton = NoDefault; // #148969

    const QExtendDialog::ButtonCode oldDefault = defaultButton();

    bool oldDefaultHadFocus = false;

    if (oldDefault != NoDefault) {
        QPushButton *old = button(oldDefault);
        if (old) {
            oldDefaultHadFocus = (focusWidget() == old);
            old->setDefault(false);
        }
    }

    if (newDefaultButton != NoDefault) {
        QPushButton *b = button(newDefaultButton);
        if (b) {
            b->setDefault(true);
            if (focusWidget() == 0 || oldDefaultHadFocus) {
                // No widget had focus yet, or the old default button had
                // -> ok to give focus to the new default button, so that it's
                // really default (Enter triggers it).
                // But we don't do this if the kdialog user gave focus to a
                // specific widget in the dialog.
                b->setFocus();
            }
        }
    }
    d->mDefaultButton = newDefaultButton;
    Q_ASSERT(defaultButton() == newDefaultButton);
}

QExtendDialog::ButtonCode QExtendDialog::defaultButton() const
{
    Q_D(const QExtendDialog);
  QHashIterator<int, QPushButton*> it( d->mButtonList );
  while ( it.hasNext() ) {
    it.next();
    if (it.value()->isDefault()) {
      return (ButtonCode)it.key();
    }
  }

    return d->mDefaultButton;
}

void QExtendDialog::setMainWidget( QWidget *widget )
{
    Q_D(QExtendDialog);
    if ( d->mMainWidget == widget )
        return;
    d->mMainWidget = widget;
    if (d->mMainWidget && d->mMainWidget->layout()) {
        // Avoid double-margin problem
        d->mMainWidget->layout()->setContentsMargins(0, 0, 0, 0);
    }
    d->setupLayout();
}

QWidget *QExtendDialog::mainWidget()
{
    Q_D(QExtendDialog);
    if (!d->mMainWidget)
        setMainWidget( new QWidget(this) );
    return d->mMainWidget;
}

QSize QExtendDialog::sizeHint() const
{
    Q_D(const QExtendDialog);

    if (!d->mMinSize.isEmpty())
        return d->mMinSize.expandedTo( minimumSizeHint() ) + d->mIncSize;
    else {
        if (d->dirty)
            const_cast<QExtendDialogPrivate*>(d)->queuedLayoutUpdate();
        return QDialog::sizeHint() + d->mIncSize;
    }
}

QSize QExtendDialog::minimumSizeHint() const
{
    Q_D(const QExtendDialog);

    if (d->dirty)
        const_cast<QExtendDialogPrivate*>(d)->queuedLayoutUpdate();
    return QDialog::minimumSizeHint() + d->mIncSize;
}

//
// Grab QDialogs keypresses if non-modal.
//
void QExtendDialog::keyPressEvent( QKeyEvent *event )
{
    Q_D(QExtendDialog);
  if ( event->modifiers() == 0 ) {
    if ( event->key() == Qt::Key_F1 ) {
      QPushButton *button = this->button( Help );

      if ( button ) {
        button->animateClick();
        event->accept();
        return;
      }
    }

    if ( event->key() == Qt::Key_Escape ) {
      QPushButton *button = this->button( d->mEscapeButton );

      if ( button ) {
        button->animateClick();
        event->accept();
        return;
      }

    }
  } else if ( event->key() == Qt::Key_F1 && event->modifiers() == Qt::ShiftModifier ) {
    QWhatsThis::enterWhatsThisMode();
    event->accept();
    return;
  } else if ( event->modifiers() == Qt::ControlModifier &&
            ( event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ) ) {
    // accept the dialog when Ctrl-Return is pressed
    QPushButton *button = this->button( Ok );

    if ( button ) {
      button->animateClick();
      event->accept();
      return;
    }
  }

  QDialog::keyPressEvent( event );
}

int QExtendDialog::marginHint()
{
    return QApplication::style()->pixelMetric( QStyle::PM_LayoutLeftMargin );
}

int QExtendDialog::spacingHint()
{
    return QApplication::style()->pixelMetric( QStyle::PM_LayoutHorizontalSpacing );
}

int QExtendDialog::groupSpacingHint()
{
    return QFontMetricsF(QGuiApplication::font()).lineSpacing();
}

QString QExtendDialog::makeStandardCaption( const QString &userCaption,
                                      QWidget* window,
                                      CaptionFlags flags )
{
  Q_UNUSED(window);
  QString caption;/* = KGlobal::caption();*/
  QString captionString = userCaption.isEmpty() ? caption : userCaption;

  // If the document is modified, add '[modified]'.
  if (flags & ModifiedCaption)
      captionString += QString::fromUtf8(" [") + tr("modified") + QString::fromUtf8("]");

  if ( !userCaption.isEmpty() ) {
      // Add the application name if:
      // User asked for it, it's not a duplication  and the app name (caption()) is not empty
      if ( flags & AppNameCaption &&
           !caption.isEmpty() &&
           !userCaption.endsWith(caption)  ) {
           // TODO: check to see if this is a transient/secondary window before trying to add the app name
           //       on platforms that need this
          captionString += " – " + caption;
      }
  }

  return captionString;
}

void QExtendDialog::setCaption( const QString &_caption )
{
  const QString caption = makeStandardCaption( _caption, this );
  setPlainCaption( caption );
}

void QExtendDialog::setCaption( const QString &caption, bool modified )
{
    CaptionFlags flags = HIGCompliantCaption;

    if ( modified )
    {
        flags |= ModifiedCaption;
    }

    setPlainCaption( makeStandardCaption(caption, this, flags) );
}


void QExtendDialog::setPlainCaption( const QString &caption )
{
    if (QWidget *win = window()) {
        win->setWindowTitle( caption );
#if 0
#ifdef Q_WS_X11
        NETWinInfo info( QX11Info::display(), win->winId(), QX11Info::appRootWindow(), 0 );
        info.setName( caption.toUtf8().constData() );
#endif
#endif
    }
}

void QExtendDialog::resizeLayout( QWidget *widget, int margin, int spacing ) //static
{
  if ( widget->layout() )
    resizeLayout( widget->layout(), margin, spacing );

  if ( widget->children().count() > 0 ) {
    const QList<QObject*> list = widget->children();
    for ( QObject *object : list ) {
      if ( object->isWidgetType() )
        resizeLayout( (QWidget*)object, margin, spacing );
    }
  }
}

void QExtendDialog::resizeLayout( QLayout *layout, int margin, int spacing ) //static
{
  QLayoutItem *child;
  int pos = 0;

  while ( (child = layout->itemAt( pos ) ) ) {
    if ( child->layout() )
      resizeLayout( child->layout(), margin, spacing );

    ++pos;
  }

  if ( layout->layout() ) {
    layout->layout()->setContentsMargins(margin, margin, margin, margin);
    layout->layout()->setSpacing( spacing );
  }
}

static QRect screenRect( QWidget *widget, int /*screen*/ )
{
  QScreen *scr = widget ? widget->screen() : QGuiApplication::primaryScreen();
  if (!scr)
    scr = QGuiApplication::primaryScreen();
  return scr->availableGeometry();
}

void QExtendDialog::centerOnScreen( QWidget *widget, int screen )
{
  if ( !widget )
    return;
#if 0
#ifdef Q_WS_X11
  if( !( widget->windowFlags() & Qt::X11BypassWindowManagerHint ) && widget->windowType() != Qt::Popup
      && NETRootInfo( QX11Info::display(), NET::Supported ).isSupported( NET::WM2FullPlacement )) {
      return; // the WM can handle placement much better
  }
#endif
#endif
  QRect rect = screenRect( widget, screen );

  widget->move( rect.center().x() - widget->width() / 2,
                rect.center().y() - widget->height() / 2 );
}

bool QExtendDialog::avoidArea( QWidget *widget, const QRect& area, int screen )
{
  if ( !widget )
    return false;

  QRect fg = widget->frameGeometry();
  if ( !fg.intersects( area ) )
    return true; // nothing to do.

  const QRect scr = screenRect( widget, screen );
  QRect avoid( area ); // let's add some margin
  avoid.translate( -5, -5 );
  avoid.setRight( avoid.right() + 10 );
  avoid.setBottom( avoid.bottom() + 10 );

  if ( qMax( fg.top(), avoid.top() ) <= qMin( fg.bottom(), avoid.bottom() ) ) {
    // We need to move the widget up or down
    int spaceAbove = qMax( 0, avoid.top() - scr.top() );
    int spaceBelow = qMax( 0, scr.bottom() - avoid.bottom() );
    if ( spaceAbove > spaceBelow ) // where's the biggest side?
      if ( fg.height() <= spaceAbove ) // big enough?
        fg.setY( avoid.top() - fg.height() );
      else
        return false;
    else
      if ( fg.height() <= spaceBelow ) // big enough?
        fg.setY( avoid.bottom() );
      else
        return false;
  }

  if ( qMax( fg.left(), avoid.left() ) <= qMin( fg.right(), avoid.right() ) ) {
    // We need to move the widget left or right
    const int spaceLeft = qMax( 0, avoid.left() - scr.left() );
    const int spaceRight = qMax( 0, scr.right() - avoid.right() );
    if ( spaceLeft > spaceRight ) // where's the biggest side?
      if ( fg.width() <= spaceLeft ) // big enough?
        fg.setX( avoid.left() - fg.width() );
      else
        return false;
    else
      if ( fg.width() <= spaceRight ) // big enough?
        fg.setX( avoid.right() );
      else
        return false;
  }

  widget->move( fg.x(), fg.y() );

  return true;
}

void QExtendDialog::showButtonSeparator( bool state )
{
    Q_D(QExtendDialog);
  if ( ( d->mActionSeparator != 0 ) == state )
    return;
  if ( state ) {
    if ( d->mActionSeparator )
      return;

     d->mActionSeparator = new KLusterSeparator( this );
     d->mActionSeparator->setOrientation( d->mButtonOrientation );
  } else {
    delete d->mActionSeparator;
    d->mActionSeparator = 0;
  }

  d->setupLayout();
}

void QExtendDialog::setInitialSize( const QSize &size )
{
    d_func()->mMinSize = size;
  adjustSize();
}

void QExtendDialog::incrementInitialSize( const QSize &size )
{
    d_func()->mIncSize = size;
  adjustSize();
}

QPushButton *QExtendDialog::button( ButtonCode id ) const
{
    Q_D(const QExtendDialog);
  return d->mButtonList.value( id, 0 );
}

void QExtendDialog::enableButton( ButtonCode id, bool state )
{
  QPushButton *button = this->button( id );
  if ( button )
    button->setEnabled( state );
}

bool QExtendDialog::isButtonEnabled( ButtonCode id ) const
{
  QPushButton *button = this->button( id );
  if ( button )
    return button->isEnabled();

  return false;
}

void QExtendDialog::enableButtonOk( bool state )
{
  enableButton( Ok, state );
}

void QExtendDialog::enableButtonApply( bool state )
{
  enableButton( Apply, state );
}

void QExtendDialog::enableButtonCancel( bool state )
{
  enableButton( Cancel, state );
}

void QExtendDialog::showButton( ButtonCode id, bool state )
{
  QPushButton *button = this->button( id );
  if ( button )
    state ? button->show() : button->hide();
}
#if 0
void QExtendDialog::setButtonGuiItem( ButtonCode id, const KGuiItem &item )
{
  QPushButton *button = this->button( id );
  if ( !button )
    return;

  button->setGuiItem( item );
}
#endif
void QExtendDialog::setButtonMenu( ButtonCode id, QMenu *menu, ButtonPopupMode popupmode)
{
  QPushButton *button = this->button( id );
  if ( button ) {
    //if (popupmode==InstantPopup)
      button->setMenu( menu );
    //else
      //button->setDelayedMenu(menu);
  }
}

void QExtendDialog::setButtonText( ButtonCode id, const QString &text )
{
    Q_D(QExtendDialog);
  if ( !d->mSettingDetails && (id == Details) ) {
    d->mDetailsButtonText = text;
    setDetailsWidgetVisible( d->mDetailsVisible );
    return;
  }

  QPushButton *button = this->button( id );
  if ( button )
    button->setText( text );
}

QString QExtendDialog::buttonText( ButtonCode id ) const
{
  QPushButton *button = this->button( id );
  if ( button )
    return button->text();
  else
    return QString();
}

void QExtendDialog::setButtonIcon( ButtonCode id, const QIcon &icon )
{
  QPushButton *button = this->button( id );
  if ( button )
    button->setIcon( icon );
}

QIcon QExtendDialog::buttonIcon( ButtonCode id ) const
{
  QPushButton *button = this->button( id );
  if ( button )
    return QIcon(button->icon());
  else
    return QIcon();
}

void QExtendDialog::setButtonToolTip( ButtonCode id, const QString &text )
{
  QPushButton *button = this->button( id );
  if ( button ) {
    if ( text.isEmpty() )
      button->setToolTip( QString() );
    else
      button->setToolTip( text );
  }
}

QString QExtendDialog::buttonToolTip( ButtonCode id ) const
{
  QPushButton *button = this->button( id );
  if ( button )
    return button->toolTip();
  else
    return QString();
}

void QExtendDialog::setButtonWhatsThis( ButtonCode id, const QString &text )
{
  QPushButton *button = this->button( id );
  if ( button ) {
    if ( text.isEmpty() )
      button->setWhatsThis( QString() );
    else
      button->setWhatsThis( text );
  }
}

QString QExtendDialog::buttonWhatsThis( ButtonCode id ) const
{
  QPushButton *button = this->button( id );
  if ( button )
    return button->whatsThis();
  else
    return QString();
}

void QExtendDialog::setButtonFocus( ButtonCode id )
{
  QPushButton *button = this->button( id );
  if ( button ) {
      button->setFocus();
  }
}

void QExtendDialog::setDetailsWidget( QWidget *detailsWidget )
{
    Q_D(QExtendDialog);
  if ( d->mDetailsWidget == detailsWidget )
    return;
  delete d->mDetailsWidget;
  d->mDetailsWidget = detailsWidget;

  if ( d->mDetailsWidget->parentWidget() != this )
    d->mDetailsWidget->setParent( this );

  d->mDetailsWidget->hide();
  d->setupLayout();

  if ( !d->mSettingDetails )
    setDetailsWidgetVisible( d->mDetailsVisible );
}

bool QExtendDialog::isDetailsWidgetVisible() const
{
    return d_func()->mDetailsVisible;
}

void QExtendDialog::setDetailsWidgetVisible( bool visible )
{
    Q_D(QExtendDialog);
  if ( d->mDetailsButtonText.isEmpty() )
    d->mDetailsButtonText = tr( "&Details" );

  d->mSettingDetails = true;
  d->mDetailsVisible = visible;
  if ( d->mDetailsVisible ) {
    emit aboutToShowDetails();
    setButtonText( Details, d->mDetailsButtonText + " <<" );
    if ( d->mDetailsWidget ) {
      if ( layout() )
        layout()->setEnabled( false );

      d->mDetailsWidget->show();

      adjustSize();

      if ( layout() ) {
        layout()->activate();
        layout()->setEnabled( true );
      }
    }
  } else {
    setButtonText( Details, d->mDetailsButtonText + " >>" );
    if ( d->mDetailsWidget )
      d->mDetailsWidget->hide();

    if ( layout() ) {
      layout()->activate();
      adjustSize();
    }

  }

  d->mSettingDetails = false;
}

void QExtendDialog::delayedDestruct()
{
  if ( isVisible() )
    hide();

  deleteLater();
}


void QExtendDialog::slotButtonClicked( int button )
{
    Q_D(QExtendDialog);
  emit buttonClicked( static_cast<QExtendDialog::ButtonCode>(button) );

  switch( button ) {
    case Ok:
      emit okClicked();
      accept();
      break;
    case Apply:
        emit applyClicked();
      break;
    case Try:
      emit tryClicked();
      break;
    case User3:
      emit user3Clicked();
      break;
    case User2:
        emit user2Clicked();
      break;
    case User1:
        emit user1Clicked();
        break;
    case Yes:
      emit yesClicked();
      done( Yes );
      break;
    case No:
        emit noClicked();
      done( No );
      break;
    case Cancel:
      emit cancelClicked();
      reject();
      break;
    case Close:
      emit closeClicked();
      done(Close); // KDE5: call reject() instead; more QDialog-like.
      break;
    case Help:
      emit helpClicked();
      if ( !d->mAnchor.isEmpty() || !d->mHelpApp.isEmpty() ) {
        //KToolInvocation::invokeHelp( d->mAnchor, d->mHelpApp );
      }
      break;
    case Default:
      emit defaultClicked();
      break;
    case Reset:
      emit resetClicked();
      break;
    case Details:
      setDetailsWidgetVisible( !d->mDetailsVisible );
      break;
  }

    // If we're here from the closeEvent, and auto-delete is on, well, auto-delete now.
    if (d->mDeferredDelete) {
        d->mDeferredDelete = false;
        delayedDestruct();
    }
}

#if 0
void QExtendDialog::enableLinkedHelp( bool state )
{
    Q_D(QExtendDialog);
    if ( ( d->mUrlHelp != 0 ) == state )
        return;
    if ( state ) {
        if ( d->mUrlHelp )
            return;

        d->mUrlHelp = new KUrlLabel( this );
        d->mUrlHelp->setText( helpLinkText() );
        d->mUrlHelp->setFloatEnabled( true );
        d->mUrlHelp->setUnderline( true );
        d->mUrlHelp->setMinimumHeight( fontMetrics().height() + marginHint() );
        connect( d->mUrlHelp, SIGNAL(leftClickedUrl()), this, SLOT(helpLinkClicked()) );

        d->mUrlHelp->show();
    } else {
        delete d->mUrlHelp;
        d->mUrlHelp = 0;
    }

    d->setupLayout();
}
#endif

void QExtendDialog::setHelp( const QString &anchor, const QString &appname )
{
    Q_D(QExtendDialog);
  d->mAnchor  = anchor;
  d->mHelpApp = appname;
}


/*
void QExtendDialog::setHelpLinkText( const QString &text )
{
    Q_D(QExtendDialog);
  d->mHelpLinkText = text;
  if ( d->mUrlHelp )
    d->mUrlHelp->setText( helpLinkText() );
}
*/

QString QExtendDialog::helpLinkText() const
{
    Q_D(const QExtendDialog);
  return ( d->mHelpLinkText.isEmpty() ? tr( "Get help..." ) : d->mHelpLinkText );
}

void QExtendDialog::updateGeometry()
{
}

void QExtendDialog::hideEvent( QHideEvent *event )
{
  emit hidden();

  if ( !event->spontaneous() )
    emit finished();
}

void QExtendDialog::closeEvent( QCloseEvent *event )
{
    Q_D(QExtendDialog);
    QPushButton *button = this->button(d->mEscapeButton);
    if (button && !isHidden()) {
        button->animateClick();

        if (testAttribute(Qt::WA_DeleteOnClose)) {
            // Don't let QWidget::close do a deferred delete just yet, wait for the click first
            d->mDeferredDelete = true;
            setAttribute(Qt::WA_DeleteOnClose, false);
        }
    } else {
        QDialog::closeEvent(event);
    }
}

void QExtendDialog::restoreDialogSize()
{
    QSettings settings;
  int width, height;
  QScreen *scr = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
  QRect desk = scr ? scr->availableGeometry() : QRect(0,0,800,600);

  width = sizeHint().width();
  height = sizeHint().height();

  width = settings.value( QString::fromLatin1( "Width %1" ).arg( desk.width() ), width ).toInt();
  height = settings.value( QString::fromLatin1( "Height %1" ).arg( desk.height() ), height ).toInt();

  resize( width, height );
}

void QExtendDialog::saveDialogSize() const
{
    QSettings settings;

   QScreen *scr = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
   QRect desk = scr ? scr->availableGeometry() : QRect(0,0,800,600);

   const QSize sizeToSave = size();

   settings.setValue( QString::fromLatin1("Width %1").arg( desk.width() ), sizeToSave.width() );
   settings.setValue( QString::fromLatin1("Height %1").arg( desk.height() ), sizeToSave.height() );
}

void QExtendDialog::setAllowEmbeddingInGraphicsView( bool allowEmbedding )
{
  sAllowEmbeddingInGraphicsView = allowEmbedding;
}

// Forwarding slots: Q_PRIVATE_SLOT removed for Qt6 compatibility
void QExtendDialog::queuedLayoutUpdate()
{
    d_ptr->queuedLayoutUpdate();
}

void QExtendDialog::helpLinkClicked()
{
    d_ptr->helpLinkClicked();
}

#include "moc_qextenddialog.cpp"
