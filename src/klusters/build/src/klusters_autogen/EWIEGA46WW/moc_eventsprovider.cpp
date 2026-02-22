/****************************************************************************
** Meta object code from reading C++ file 'eventsprovider.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../src/eventsprovider.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'eventsprovider.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_EventsProvider_t {
    uint offsetsAndSizes[40];
    char stringdata0[15];
    char stringdata1[10];
    char stringdata2[1];
    char stringdata3[17];
    char stringdata4[6];
    char stringdata5[12];
    char stringdata6[4];
    char stringdata7[10];
    char stringdata8[13];
    char stringdata9[19];
    char stringdata10[13];
    char stringdata11[23];
    char stringdata12[27];
    char stringdata13[14];
    char stringdata14[15];
    char stringdata15[15];
    char stringdata16[22];
    char stringdata17[24];
    char stringdata18[16];
    char stringdata19[25];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_EventsProvider_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_EventsProvider_t qt_meta_stringdata_EventsProvider = {
    {
        QT_MOC_LITERAL(0, 14),  // "EventsProvider"
        QT_MOC_LITERAL(15, 9),  // "dataReady"
        QT_MOC_LITERAL(25, 0),  // ""
        QT_MOC_LITERAL(26, 16),  // "Array<dataType>&"
        QT_MOC_LITERAL(43, 5),  // "times"
        QT_MOC_LITERAL(49, 11),  // "Array<int>&"
        QT_MOC_LITERAL(61, 3),  // "ids"
        QT_MOC_LITERAL(65, 9),  // "initiator"
        QT_MOC_LITERAL(75, 12),  // "providerName"
        QT_MOC_LITERAL(88, 18),  // "nextEventDataReady"
        QT_MOC_LITERAL(107, 12),  // "startingTime"
        QT_MOC_LITERAL(120, 22),  // "previousEventDataReady"
        QT_MOC_LITERAL(143, 26),  // "newEventDescriptionCreated"
        QT_MOC_LITERAL(170, 13),  // "QMap<int,int>"
        QT_MOC_LITERAL(184, 14),  // "oldNewEventIds"
        QT_MOC_LITERAL(199, 14),  // "newOldEventIds"
        QT_MOC_LITERAL(214, 21),  // "eventDescriptionAdded"
        QT_MOC_LITERAL(236, 23),  // "eventDescriptionRemoved"
        QT_MOC_LITERAL(260, 15),  // "eventIdToRemove"
        QT_MOC_LITERAL(276, 24)   // "eventDescriptionToRemove"
    },
    "EventsProvider",
    "dataReady",
    "",
    "Array<dataType>&",
    "times",
    "Array<int>&",
    "ids",
    "initiator",
    "providerName",
    "nextEventDataReady",
    "startingTime",
    "previousEventDataReady",
    "newEventDescriptionCreated",
    "QMap<int,int>",
    "oldNewEventIds",
    "newOldEventIds",
    "eventDescriptionAdded",
    "eventDescriptionRemoved",
    "eventIdToRemove",
    "eventDescriptionToRemove"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_EventsProvider[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       5,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    4,   44,    2, 0x06,    1 /* Public */,
       9,    5,   53,    2, 0x06,    6 /* Public */,
      11,    5,   64,    2, 0x06,   12 /* Public */,
      12,    4,   75,    2, 0x06,   18 /* Public */,
      17,    5,   84,    2, 0x06,   23 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3, 0x80000000 | 5, QMetaType::QObjectStar, QMetaType::QString,    4,    6,    7,    8,
    QMetaType::Void, 0x80000000 | 3, 0x80000000 | 5, QMetaType::QObjectStar, QMetaType::QString, QMetaType::Long,    4,    6,    7,    8,   10,
    QMetaType::Void, 0x80000000 | 3, 0x80000000 | 5, QMetaType::QObjectStar, QMetaType::QString, QMetaType::Long,    4,    6,    7,    8,   10,
    QMetaType::Void, QMetaType::QString, 0x80000000 | 13, 0x80000000 | 13, QMetaType::QString,    8,   14,   15,   16,
    QMetaType::Void, QMetaType::QString, 0x80000000 | 13, 0x80000000 | 13, QMetaType::Int, QMetaType::QString,    8,   14,   15,   18,   19,

       0        // eod
};

Q_CONSTINIT const QMetaObject EventsProvider::staticMetaObject = { {
    QMetaObject::SuperData::link<DataProvider::staticMetaObject>(),
    qt_meta_stringdata_EventsProvider.offsetsAndSizes,
    qt_meta_data_EventsProvider,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_EventsProvider_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<EventsProvider, std::true_type>,
        // method 'dataReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Array<dataType> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<Array<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<QObject *, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'nextEventDataReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Array<dataType> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<Array<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<QObject *, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        QtPrivate::TypeAndForceComplete<long, std::false_type>,
        // method 'previousEventDataReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Array<dataType> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<Array<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<QObject *, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        QtPrivate::TypeAndForceComplete<long, std::false_type>,
        // method 'newEventDescriptionCreated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMap<int,int>, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMap<int,int>, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        // method 'eventDescriptionRemoved'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMap<int,int>, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMap<int,int>, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<QString, std::false_type>
    >,
    nullptr
} };

void EventsProvider::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<EventsProvider *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->dataReady((*reinterpret_cast< std::add_pointer_t<Array<dataType>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<Array<int>&>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QObject*>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[4]))); break;
        case 1: _t->nextEventDataReady((*reinterpret_cast< std::add_pointer_t<Array<dataType>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<Array<int>&>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QObject*>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast< std::add_pointer_t<long>>(_a[5]))); break;
        case 2: _t->previousEventDataReady((*reinterpret_cast< std::add_pointer_t<Array<dataType>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<Array<int>&>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QObject*>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[4])),(*reinterpret_cast< std::add_pointer_t<long>>(_a[5]))); break;
        case 3: _t->newEventDescriptionCreated((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QMap<int,int>>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QMap<int,int>>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[4]))); break;
        case 4: _t->eventDescriptionRemoved((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QMap<int,int>>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QMap<int,int>>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[4])),(*reinterpret_cast< std::add_pointer_t<QString>>(_a[5]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (EventsProvider::*)(Array<dataType> & , Array<int> & , QObject * , QString );
            if (_t _q_method = &EventsProvider::dataReady; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (EventsProvider::*)(Array<dataType> & , Array<int> & , QObject * , QString , long );
            if (_t _q_method = &EventsProvider::nextEventDataReady; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (EventsProvider::*)(Array<dataType> & , Array<int> & , QObject * , QString , long );
            if (_t _q_method = &EventsProvider::previousEventDataReady; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (EventsProvider::*)(QString , QMap<int,int> , QMap<int,int> , QString );
            if (_t _q_method = &EventsProvider::newEventDescriptionCreated; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (EventsProvider::*)(QString , QMap<int,int> , QMap<int,int> , int , QString );
            if (_t _q_method = &EventsProvider::eventDescriptionRemoved; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
    }
}

const QMetaObject *EventsProvider::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *EventsProvider::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_EventsProvider.stringdata0))
        return static_cast<void*>(this);
    return DataProvider::qt_metacast(_clname);
}

int EventsProvider::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = DataProvider::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void EventsProvider::dataReady(Array<dataType> & _t1, Array<int> & _t2, QObject * _t3, QString _t4)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void EventsProvider::nextEventDataReady(Array<dataType> & _t1, Array<int> & _t2, QObject * _t3, QString _t4, long _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void EventsProvider::previousEventDataReady(Array<dataType> & _t1, Array<int> & _t2, QObject * _t3, QString _t4, long _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void EventsProvider::newEventDescriptionCreated(QString _t1, QMap<int,int> _t2, QMap<int,int> _t3, QString _t4)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void EventsProvider::eventDescriptionRemoved(QString _t1, QMap<int,int> _t2, QMap<int,int> _t3, int _t4, QString _t5)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t4))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t5))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
