/****************************************************************************
** Meta object code from reading C++ file 'processlinemaker.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../src/processlinemaker.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'processlinemaker.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_ProcessLineMaker_t {
    uint offsetsAndSizes[20];
    char stringdata0[17];
    char stringdata1[19];
    char stringdata2[1];
    char stringdata3[5];
    char stringdata4[19];
    char stringdata5[20];
    char stringdata6[19];
    char stringdata7[19];
    char stringdata8[17];
    char stringdata9[18];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_ProcessLineMaker_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_ProcessLineMaker_t qt_meta_stringdata_ProcessLineMaker = {
    {
        QT_MOC_LITERAL(0, 16),  // "ProcessLineMaker"
        QT_MOC_LITERAL(17, 18),  // "receivedStdoutLine"
        QT_MOC_LITERAL(36, 0),  // ""
        QT_MOC_LITERAL(37, 4),  // "line"
        QT_MOC_LITERAL(42, 18),  // "receivedStderrLine"
        QT_MOC_LITERAL(61, 19),  // "outputTreatmentOver"
        QT_MOC_LITERAL(81, 18),  // "slotReceivedStdout"
        QT_MOC_LITERAL(100, 18),  // "slotReceivedStderr"
        QT_MOC_LITERAL(119, 16),  // "slotWidgetHidden"
        QT_MOC_LITERAL(136, 17)   // "slotProcessExited"
    },
    "ProcessLineMaker",
    "receivedStdoutLine",
    "",
    "line",
    "receivedStderrLine",
    "outputTreatmentOver",
    "slotReceivedStdout",
    "slotReceivedStderr",
    "slotWidgetHidden",
    "slotProcessExited"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_ProcessLineMaker[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   56,    2, 0x06,    1 /* Public */,
       4,    1,   59,    2, 0x06,    3 /* Public */,
       5,    0,   62,    2, 0x06,    5 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       6,    0,   63,    2, 0x0a,    6 /* Public */,
       7,    0,   64,    2, 0x0a,    7 /* Public */,
       8,    0,   65,    2, 0x0a,    8 /* Public */,
       9,    0,   66,    2, 0x0a,    9 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject ProcessLineMaker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_ProcessLineMaker.offsetsAndSizes,
    qt_meta_data_ProcessLineMaker,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_ProcessLineMaker_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ProcessLineMaker, std::true_type>,
        // method 'receivedStdoutLine'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'receivedStderrLine'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'outputTreatmentOver'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'slotReceivedStdout'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'slotReceivedStderr'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'slotWidgetHidden'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'slotProcessExited'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void ProcessLineMaker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ProcessLineMaker *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->receivedStdoutLine((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->receivedStderrLine((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->outputTreatmentOver(); break;
        case 3: _t->slotReceivedStdout(); break;
        case 4: _t->slotReceivedStderr(); break;
        case 5: _t->slotWidgetHidden(); break;
        case 6: _t->slotProcessExited(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ProcessLineMaker::*)(const QString & );
            if (_t _q_method = &ProcessLineMaker::receivedStdoutLine; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ProcessLineMaker::*)(const QString & );
            if (_t _q_method = &ProcessLineMaker::receivedStderrLine; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (ProcessLineMaker::*)();
            if (_t _q_method = &ProcessLineMaker::outputTreatmentOver; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
    }
}

const QMetaObject *ProcessLineMaker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ProcessLineMaker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ProcessLineMaker.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int ProcessLineMaker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void ProcessLineMaker::receivedStdoutLine(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void ProcessLineMaker::receivedStderrLine(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void ProcessLineMaker::outputTreatmentOver()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
