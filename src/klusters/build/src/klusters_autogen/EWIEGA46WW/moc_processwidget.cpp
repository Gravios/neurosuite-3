/****************************************************************************
** Meta object code from reading C++ file 'processwidget.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../src/processwidget.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'processwidget.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.4.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
namespace {
struct qt_meta_stringdata_ProcessWidget_t {
    uint offsetsAndSizes[36];
    char stringdata0[14];
    char stringdata1[9];
    char stringdata2[1];
    char stringdata3[21];
    char stringdata4[23];
    char stringdata5[7];
    char stringdata6[18];
    char stringdata7[9];
    char stringdata8[4];
    char stringdata9[8];
    char stringdata10[8];
    char stringdata11[5];
    char stringdata12[8];
    char stringdata13[17];
    char stringdata14[5];
    char stringdata15[17];
    char stringdata16[18];
    char stringdata17[24];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_ProcessWidget_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_ProcessWidget_t qt_meta_stringdata_ProcessWidget = {
    {
        QT_MOC_LITERAL(0, 13),  // "ProcessWidget"
        QT_MOC_LITERAL(14, 8),  // "finished"
        QT_MOC_LITERAL(23, 0),  // ""
        QT_MOC_LITERAL(24, 20),  // "QProcess::ExitStatus"
        QT_MOC_LITERAL(45, 22),  // "processOutputsFinished"
        QT_MOC_LITERAL(68, 6),  // "hidden"
        QT_MOC_LITERAL(75, 17),  // "processNotStarted"
        QT_MOC_LITERAL(93, 8),  // "startJob"
        QT_MOC_LITERAL(102, 3),  // "dir"
        QT_MOC_LITERAL(106, 7),  // "command"
        QT_MOC_LITERAL(114, 7),  // "program"
        QT_MOC_LITERAL(122, 4),  // "args"
        QT_MOC_LITERAL(127, 7),  // "killJob"
        QT_MOC_LITERAL(135, 16),  // "insertStdoutLine"
        QT_MOC_LITERAL(152, 4),  // "line"
        QT_MOC_LITERAL(157, 16),  // "insertStderrLine"
        QT_MOC_LITERAL(174, 17),  // "slotProcessExited"
        QT_MOC_LITERAL(192, 23)   // "slotOutputTreatmentOver"
    },
    "ProcessWidget",
    "finished",
    "",
    "QProcess::ExitStatus",
    "processOutputsFinished",
    "hidden",
    "processNotStarted",
    "startJob",
    "dir",
    "command",
    "program",
    "args",
    "killJob",
    "insertStdoutLine",
    "line",
    "insertStderrLine",
    "slotProcessExited",
    "slotOutputTreatmentOver"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_ProcessWidget[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      11,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    2,   80,    2, 0x06,    1 /* Public */,
       4,    0,   85,    2, 0x06,    4 /* Public */,
       5,    0,   86,    2, 0x06,    5 /* Public */,
       6,    0,   87,    2, 0x06,    6 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       7,    2,   88,    2, 0x0a,    7 /* Public */,
       7,    3,   93,    2, 0x0a,   10 /* Public */,
      12,    0,  100,    2, 0x0a,   14 /* Public */,
      13,    1,  101,    2, 0x0a,   15 /* Public */,
      15,    1,  104,    2, 0x0a,   17 /* Public */,
      16,    2,  107,    2, 0x09,   19 /* Protected */,
      17,    0,  112,    2, 0x09,   22 /* Protected */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int, 0x80000000 | 3,    2,    2,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Bool, QMetaType::QString, QMetaType::QString,    8,    9,
    QMetaType::Bool, QMetaType::QString, QMetaType::QString, QMetaType::QStringList,    8,   10,   11,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   14,
    QMetaType::Void, QMetaType::QString,   14,
    QMetaType::Void, QMetaType::Int, 0x80000000 | 3,    2,    2,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject ProcessWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QListWidget::staticMetaObject>(),
    qt_meta_stringdata_ProcessWidget.offsetsAndSizes,
    qt_meta_data_ProcessWidget,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_ProcessWidget_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ProcessWidget, std::true_type>,
        // method 'finished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<QProcess::ExitStatus, std::false_type>,
        // method 'processOutputsFinished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'hidden'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'processNotStarted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'startJob'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'startJob'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QStringList &, std::false_type>,
        // method 'killJob'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'insertStdoutLine'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'insertStderrLine'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'slotProcessExited'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<QProcess::ExitStatus, std::false_type>,
        // method 'slotOutputTreatmentOver'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void ProcessWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ProcessWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->finished((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QProcess::ExitStatus>>(_a[2]))); break;
        case 1: _t->processOutputsFinished(); break;
        case 2: _t->hidden(); break;
        case 3: _t->processNotStarted(); break;
        case 4: { bool _r = _t->startJob((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 5: { bool _r = _t->startJob((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QStringList>>(_a[3])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 6: _t->killJob(); break;
        case 7: _t->insertStdoutLine((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 8: _t->insertStderrLine((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 9: _t->slotProcessExited((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QProcess::ExitStatus>>(_a[2]))); break;
        case 10: _t->slotOutputTreatmentOver(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ProcessWidget::*)(int , QProcess::ExitStatus );
            if (_t _q_method = &ProcessWidget::finished; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ProcessWidget::*)();
            if (_t _q_method = &ProcessWidget::processOutputsFinished; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (ProcessWidget::*)();
            if (_t _q_method = &ProcessWidget::hidden; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (ProcessWidget::*)();
            if (_t _q_method = &ProcessWidget::processNotStarted; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
    }
}

const QMetaObject *ProcessWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ProcessWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ProcessWidget.stringdata0))
        return static_cast<void*>(this);
    return QListWidget::qt_metacast(_clname);
}

int ProcessWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QListWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 11;
    }
    return _id;
}

// SIGNAL 0
void ProcessWidget::finished(int _t1, QProcess::ExitStatus _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void ProcessWidget::processOutputsFinished()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void ProcessWidget::hidden()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void ProcessWidget::processNotStarted()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
