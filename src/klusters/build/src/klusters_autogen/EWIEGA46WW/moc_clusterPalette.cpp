/****************************************************************************
** Meta object code from reading C++ file 'clusterPalette.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../src/clusterPalette.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'clusterPalette.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_ClusterPaletteWidget_t {
    uint offsetsAndSizes[12];
    char stringdata0[21];
    char stringdata1[12];
    char stringdata2[1];
    char stringdata3[17];
    char stringdata4[5];
    char stringdata5[7];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_ClusterPaletteWidget_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_ClusterPaletteWidget_t qt_meta_stringdata_ClusterPaletteWidget = {
    {
        QT_MOC_LITERAL(0, 20),  // "ClusterPaletteWidget"
        QT_MOC_LITERAL(21, 11),  // "changeColor"
        QT_MOC_LITERAL(33, 0),  // ""
        QT_MOC_LITERAL(34, 16),  // "QListWidgetItem*"
        QT_MOC_LITERAL(51, 4),  // "item"
        QT_MOC_LITERAL(56, 6)   // "onItem"
    },
    "ClusterPaletteWidget",
    "changeColor",
    "",
    "QListWidgetItem*",
    "item",
    "onItem"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_ClusterPaletteWidget[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       2,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   26,    2, 0x06,    1 /* Public */,
       5,    1,   29,    2, 0x06,    3 /* Public */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 3,    4,

       0        // eod
};

Q_CONSTINIT const QMetaObject ClusterPaletteWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QListWidget::staticMetaObject>(),
    qt_meta_stringdata_ClusterPaletteWidget.offsetsAndSizes,
    qt_meta_data_ClusterPaletteWidget,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_ClusterPaletteWidget_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ClusterPaletteWidget, std::true_type>,
        // method 'changeColor'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QListWidgetItem *, std::false_type>,
        // method 'onItem'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QListWidgetItem *, std::false_type>
    >,
    nullptr
} };

void ClusterPaletteWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ClusterPaletteWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->changeColor((*reinterpret_cast< std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        case 1: _t->onItem((*reinterpret_cast< std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ClusterPaletteWidget::*)(QListWidgetItem * );
            if (_t _q_method = &ClusterPaletteWidget::changeColor; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ClusterPaletteWidget::*)(QListWidgetItem * );
            if (_t _q_method = &ClusterPaletteWidget::onItem; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
    }
}

const QMetaObject *ClusterPaletteWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ClusterPaletteWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ClusterPaletteWidget.stringdata0))
        return static_cast<void*>(this);
    return QListWidget::qt_metacast(_clname);
}

int ClusterPaletteWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QListWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 2)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void ClusterPaletteWidget::changeColor(QListWidgetItem * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void ClusterPaletteWidget::onItem(QListWidgetItem * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}
namespace {
struct qt_meta_stringdata_ClusterPalette_t {
    uint offsetsAndSizes[38];
    char stringdata0[15];
    char stringdata1[18];
    char stringdata2[1];
    char stringdata3[16];
    char stringdata4[20];
    char stringdata5[11];
    char stringdata6[17];
    char stringdata7[14];
    char stringdata8[20];
    char stringdata9[23];
    char stringdata10[27];
    char stringdata11[12];
    char stringdata12[17];
    char stringdata13[5];
    char stringdata14[15];
    char stringdata15[16];
    char stringdata16[15];
    char stringdata17[11];
    char stringdata18[31];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_ClusterPalette_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_ClusterPalette_t qt_meta_stringdata_ClusterPalette = {
    {
        QT_MOC_LITERAL(0, 14),  // "ClusterPalette"
        QT_MOC_LITERAL(15, 17),  // "singleChangeColor"
        QT_MOC_LITERAL(33, 0),  // ""
        QT_MOC_LITERAL(34, 15),  // "selectedCluster"
        QT_MOC_LITERAL(50, 19),  // "updateShownClusters"
        QT_MOC_LITERAL(70, 10),  // "QList<int>"
        QT_MOC_LITERAL(81, 16),  // "selectedClusters"
        QT_MOC_LITERAL(98, 13),  // "groupClusters"
        QT_MOC_LITERAL(112, 19),  // "moveClustersToNoise"
        QT_MOC_LITERAL(132, 22),  // "moveClustersToArtefact"
        QT_MOC_LITERAL(155, 26),  // "clusterInformationModified"
        QT_MOC_LITERAL(182, 11),  // "changeColor"
        QT_MOC_LITERAL(194, 16),  // "QListWidgetItem*"
        QT_MOC_LITERAL(211, 4),  // "item"
        QT_MOC_LITERAL(216, 14),  // "updateClusters"
        QT_MOC_LITERAL(231, 15),  // "slotClickRedraw"
        QT_MOC_LITERAL(247, 14),  // "languageChange"
        QT_MOC_LITERAL(262, 10),  // "slotOnItem"
        QT_MOC_LITERAL(273, 30)   // "slotCustomContextMenuRequested"
    },
    "ClusterPalette",
    "singleChangeColor",
    "",
    "selectedCluster",
    "updateShownClusters",
    "QList<int>",
    "selectedClusters",
    "groupClusters",
    "moveClustersToNoise",
    "moveClustersToArtefact",
    "clusterInformationModified",
    "changeColor",
    "QListWidgetItem*",
    "item",
    "updateClusters",
    "slotClickRedraw",
    "languageChange",
    "slotOnItem",
    "slotCustomContextMenuRequested"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_ClusterPalette[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      15,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       6,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,  104,    2, 0x06,    1 /* Public */,
       4,    1,  107,    2, 0x06,    3 /* Public */,
       7,    1,  110,    2, 0x06,    5 /* Public */,
       8,    1,  113,    2, 0x06,    7 /* Public */,
       9,    1,  116,    2, 0x06,    9 /* Public */,
      10,    0,  119,    2, 0x06,   11 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      11,    1,  120,    2, 0x0a,   12 /* Public */,
       8,    0,  123,    2, 0x0a,   14 /* Public */,
       9,    0,  124,    2, 0x0a,   15 /* Public */,
       7,    0,  125,    2, 0x0a,   16 /* Public */,
      14,    0,  126,    2, 0x0a,   17 /* Public */,
      15,    0,  127,    2, 0x09,   18 /* Protected */,
      16,    0,  128,    2, 0x09,   19 /* Protected */,
      17,    1,  129,    2, 0x09,   20 /* Protected */,
      18,    1,  132,    2, 0x09,   22 /* Protected */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int,    3,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void, 0x80000000 | 5,    6,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 12,   13,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 12,   13,
    QMetaType::Void, QMetaType::QPoint,    2,

       0        // eod
};

Q_CONSTINIT const QMetaObject ClusterPalette::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_ClusterPalette.offsetsAndSizes,
    qt_meta_data_ClusterPalette,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_ClusterPalette_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ClusterPalette, std::true_type>,
        // method 'singleChangeColor'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'updateShownClusters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QList<int> &, std::false_type>,
        // method 'groupClusters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QList<int> &, std::false_type>,
        // method 'moveClustersToNoise'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QList<int> &, std::false_type>,
        // method 'moveClustersToArtefact'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QList<int> &, std::false_type>,
        // method 'clusterInformationModified'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'changeColor'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QListWidgetItem *, std::false_type>,
        // method 'moveClustersToNoise'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'moveClustersToArtefact'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'groupClusters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateClusters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'slotClickRedraw'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'languageChange'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'slotOnItem'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QListWidgetItem *, std::false_type>,
        // method 'slotCustomContextMenuRequested'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QPoint &, std::false_type>
    >,
    nullptr
} };

void ClusterPalette::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ClusterPalette *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->singleChangeColor((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->updateShownClusters((*reinterpret_cast< std::add_pointer_t<QList<int>>>(_a[1]))); break;
        case 2: _t->groupClusters((*reinterpret_cast< std::add_pointer_t<QList<int>>>(_a[1]))); break;
        case 3: _t->moveClustersToNoise((*reinterpret_cast< std::add_pointer_t<QList<int>>>(_a[1]))); break;
        case 4: _t->moveClustersToArtefact((*reinterpret_cast< std::add_pointer_t<QList<int>>>(_a[1]))); break;
        case 5: _t->clusterInformationModified(); break;
        case 6: _t->changeColor((*reinterpret_cast< std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        case 7: _t->moveClustersToNoise(); break;
        case 8: _t->moveClustersToArtefact(); break;
        case 9: _t->groupClusters(); break;
        case 10: _t->updateClusters(); break;
        case 11: _t->slotClickRedraw(); break;
        case 12: _t->languageChange(); break;
        case 13: _t->slotOnItem((*reinterpret_cast< std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        case 14: _t->slotCustomContextMenuRequested((*reinterpret_cast< std::add_pointer_t<QPoint>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<int> >(); break;
            }
            break;
        case 2:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<int> >(); break;
            }
            break;
        case 3:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<int> >(); break;
            }
            break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QList<int> >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ClusterPalette::*)(int );
            if (_t _q_method = &ClusterPalette::singleChangeColor; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ClusterPalette::*)(const QList<int> & );
            if (_t _q_method = &ClusterPalette::updateShownClusters; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (ClusterPalette::*)(const QList<int> & );
            if (_t _q_method = &ClusterPalette::groupClusters; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (ClusterPalette::*)(const QList<int> & );
            if (_t _q_method = &ClusterPalette::moveClustersToNoise; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
        {
            using _t = void (ClusterPalette::*)(const QList<int> & );
            if (_t _q_method = &ClusterPalette::moveClustersToArtefact; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 4;
                return;
            }
        }
        {
            using _t = void (ClusterPalette::*)();
            if (_t _q_method = &ClusterPalette::clusterInformationModified; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 5;
                return;
            }
        }
    }
}

const QMetaObject *ClusterPalette::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ClusterPalette::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ClusterPalette.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int ClusterPalette::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 15)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 15;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 15)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 15;
    }
    return _id;
}

// SIGNAL 0
void ClusterPalette::singleChangeColor(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void ClusterPalette::updateShownClusters(const QList<int> & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void ClusterPalette::groupClusters(const QList<int> & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void ClusterPalette::moveClustersToNoise(const QList<int> & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}

// SIGNAL 4
void ClusterPalette::moveClustersToArtefact(const QList<int> & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void ClusterPalette::clusterInformationModified()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
