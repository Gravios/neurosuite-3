/****************************************************************************
** Meta object code from reading C++ file 'clustersprovider.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../src/clustersprovider.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'clustersprovider.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_ClustersProvider_t {
    uint offsetsAndSizes[22];
    char stringdata0[17];
    char stringdata1[10];
    char stringdata2[1];
    char stringdata3[17];
    char stringdata4[5];
    char stringdata5[10];
    char stringdata6[13];
    char stringdata7[21];
    char stringdata8[13];
    char stringdata9[29];
    char stringdata10[25];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_ClustersProvider_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_ClustersProvider_t qt_meta_stringdata_ClustersProvider = {
    {
        QT_MOC_LITERAL(0, 16),  // "ClustersProvider"
        QT_MOC_LITERAL(17, 9),  // "dataReady"
        QT_MOC_LITERAL(27, 0),  // ""
        QT_MOC_LITERAL(28, 16),  // "Array<dataType>&"
        QT_MOC_LITERAL(45, 4),  // "data"
        QT_MOC_LITERAL(50, 9),  // "initiator"
        QT_MOC_LITERAL(60, 12),  // "providerName"
        QT_MOC_LITERAL(73, 20),  // "nextClusterDataReady"
        QT_MOC_LITERAL(94, 12),  // "startingTime"
        QT_MOC_LITERAL(107, 28),  // "startingTimeInRecordingUnits"
        QT_MOC_LITERAL(136, 24)   // "previousClusterDataReady"
    },
    "ClustersProvider",
    "dataReady",
    "",
    "Array<dataType>&",
    "data",
    "initiator",
    "providerName",
    "nextClusterDataReady",
    "startingTime",
    "startingTimeInRecordingUnits",
    "previousClusterDataReady"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_ClustersProvider[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    3,   32,    2, 0x06,    1 /* Public */,
       7,    5,   39,    2, 0x06,    5 /* Public */,
      10,    5,   50,    2, 0x06,   11 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, QMetaType::QObjectStar, QMetaType::QString,    4,    5,    6,
    QMetaType::Void, 0x80000000 | 3, QMetaType::QObjectStar, QMetaType::QString, QMetaType::Long, QMetaType::Long,    4,    5,    6,    8,    9,
    QMetaType::Void, 0x80000000 | 3, QMetaType::QObjectStar, QMetaType::QString, QMetaType::Long, QMetaType::Long,    4,    5,    6,    8,    9,

       0        // eod
};

Q_CONSTINIT const QMetaObject ClustersProvider::staticMetaObject = { {
    QMetaObject::SuperData::link<DataProvider::staticMetaObject>(),
    qt_meta_stringdata_ClustersProvider.offsetsAndSizes,
    qt_meta_data_ClustersProvider,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_ClustersProvider_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ClustersProvider, std::true_type>,
        // method 'dataReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Array<dataType> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<QObject *, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'nextClusterDataReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Array<dataType> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<QObject *, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        QtPrivate::TypeAndForceComplete<long, std::false_type>,
        QtPrivate::TypeAndForceComplete<long, std::false_type>,
        // method 'previousClusterDataReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Array<dataType> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<QObject *, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        QtPrivate::TypeAndForceComplete<long, std::false_type>,
        QtPrivate::TypeAndForceComplete<long, std::false_type>
    >,
    nullptr
} };

void ClustersProvider::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ClustersProvider *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->dataReady((*reinterpret_cast< std::add_pointer_t<Array<dataType>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QObject*>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3]))); break;
        case 1: _t->nextClusterDataReady((*reinterpret_cast< std::add_pointer_t<Array<dataType>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QObject*>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<long>>(_a[4])),(*reinterpret_cast< std::add_pointer_t<long>>(_a[5]))); break;
        case 2: _t->previousClusterDataReady((*reinterpret_cast< std::add_pointer_t<Array<dataType>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QObject*>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<long>>(_a[4])),(*reinterpret_cast< std::add_pointer_t<long>>(_a[5]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ClustersProvider::*)(Array<dataType> & , QObject * , QString );
            if (_t _q_method = &ClustersProvider::dataReady; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ClustersProvider::*)(Array<dataType> & , QObject * , QString , long , long );
            if (_t _q_method = &ClustersProvider::nextClusterDataReady; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (ClustersProvider::*)(Array<dataType> & , QObject * , QString , long , long );
            if (_t _q_method = &ClustersProvider::previousClusterDataReady; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
    }
}

const QMetaObject *ClustersProvider::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ClustersProvider::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ClustersProvider.stringdata0))
        return static_cast<void*>(this);
    return DataProvider::qt_metacast(_clname);
}

int ClustersProvider::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = DataProvider::qt_metacall(_c, _id, _a);
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

// SIGNAL 0
void ClustersProvider::dataReady(Array<dataType> & _t1, QObject * _t2, QString _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void ClustersProvider::nextClusterDataReady(Array<dataType> & _t1, QObject * _t2, QString _t3, long _t4, long _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void ClustersProvider::previousClusterDataReady(Array<dataType> & _t1, QObject * _t2, QString _t3, long _t4, long _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
