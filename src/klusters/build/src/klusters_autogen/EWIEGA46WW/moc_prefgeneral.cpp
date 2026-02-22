/****************************************************************************
** Meta object code from reading C++ file 'prefgeneral.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../src/prefgeneral.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'prefgeneral.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_PrefGeneral_t {
    uint offsetsAndSizes[12];
    char stringdata0[12];
    char stringdata1[32];
    char stringdata2[1];
    char stringdata3[6];
    char stringdata4[29];
    char stringdata5[24];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_PrefGeneral_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_PrefGeneral_t qt_meta_stringdata_PrefGeneral = {
    {
        QT_MOC_LITERAL(0, 11),  // "PrefGeneral"
        QT_MOC_LITERAL(12, 31),  // "updateCrashRecoveryTimeInterval"
        QT_MOC_LITERAL(44, 0),  // ""
        QT_MOC_LITERAL(45, 5),  // "state"
        QT_MOC_LITERAL(51, 28),  // "updateReclusteringExecutable"
        QT_MOC_LITERAL(80, 23)   // "updateRealignExecutable"
    },
    "PrefGeneral",
    "updateCrashRecoveryTimeInterval",
    "",
    "state",
    "updateReclusteringExecutable",
    "updateRealignExecutable"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_PrefGeneral[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   32,    2, 0x08,    1 /* Private */,
       4,    0,   35,    2, 0x08,    3 /* Private */,
       5,    0,   36,    2, 0x08,    4 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject PrefGeneral::staticMetaObject = { {
    QMetaObject::SuperData::link<PrefGeneralLayout::staticMetaObject>(),
    qt_meta_stringdata_PrefGeneral.offsetsAndSizes,
    qt_meta_data_PrefGeneral,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_PrefGeneral_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<PrefGeneral, std::true_type>,
        // method 'updateCrashRecoveryTimeInterval'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'updateReclusteringExecutable'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateRealignExecutable'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void PrefGeneral::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<PrefGeneral *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->updateCrashRecoveryTimeInterval((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->updateReclusteringExecutable(); break;
        case 2: _t->updateRealignExecutable(); break;
        default: ;
        }
    }
}

const QMetaObject *PrefGeneral::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PrefGeneral::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_PrefGeneral.stringdata0))
        return static_cast<void*>(this);
    return PrefGeneralLayout::qt_metacast(_clname);
}

int PrefGeneral::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = PrefGeneralLayout::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
