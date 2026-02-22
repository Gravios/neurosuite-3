/****************************************************************************
** Meta object code from reading C++ file 'viewwidget.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../src/viewwidget.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'viewwidget.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_ViewWidget_t {
    uint offsetsAndSizes[54];
    char stringdata0[11];
    char stringdata1[18];
    char stringdata2[1];
    char stringdata3[11];
    char stringdata4[11];
    char stringdata5[18];
    char stringdata6[10];
    char stringdata7[7];
    char stringdata8[17];
    char stringdata9[22];
    char stringdata10[20];
    char stringdata11[12];
    char stringdata12[13];
    char stringdata13[26];
    char stringdata14[21];
    char stringdata15[15];
    char stringdata16[15];
    char stringdata17[17];
    char stringdata18[21];
    char stringdata19[19];
    char stringdata20[17];
    char stringdata21[6];
    char stringdata22[10];
    char stringdata23[13];
    char stringdata24[6];
    char stringdata25[7];
    char stringdata26[16];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_ViewWidget_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_ViewWidget_t qt_meta_stringdata_ViewWidget = {
    {
        QT_MOC_LITERAL(0, 10),  // "ViewWidget"
        QT_MOC_LITERAL(11, 17),  // "updatedDimensions"
        QT_MOC_LITERAL(29, 0),  // ""
        QT_MOC_LITERAL(30, 10),  // "dimensionX"
        QT_MOC_LITERAL(41, 10),  // "dimensionY"
        QT_MOC_LITERAL(52, 17),  // "singleColorUpdate"
        QT_MOC_LITERAL(70, 9),  // "clusterId"
        QT_MOC_LITERAL(80, 6),  // "active"
        QT_MOC_LITERAL(87, 16),  // "addClusterToView"
        QT_MOC_LITERAL(104, 21),  // "removeClusterFromView"
        QT_MOC_LITERAL(126, 19),  // "addNewClusterToView"
        QT_MOC_LITERAL(146, 11),  // "QList<int>&"
        QT_MOC_LITERAL(158, 12),  // "fromClusters"
        QT_MOC_LITERAL(171, 25),  // "spikesRemovedFromClusters"
        QT_MOC_LITERAL(197, 20),  // "spikesAddedToCluster"
        QT_MOC_LITERAL(218, 14),  // "emptySelection"
        QT_MOC_LITERAL(233, 14),  // "updateClusters"
        QT_MOC_LITERAL(248, 16),  // "modifiedClusters"
        QT_MOC_LITERAL(265, 20),  // "isModifiedByDeletion"
        QT_MOC_LITERAL(286, 18),  // "undoUpdateClusters"
        QT_MOC_LITERAL(305, 16),  // "isThreadsRunning"
        QT_MOC_LITERAL(322, 5),  // "print"
        QT_MOC_LITERAL(328, 9),  // "QPainter&"
        QT_MOC_LITERAL(338, 12),  // "printPainter"
        QT_MOC_LITERAL(351, 5),  // "width"
        QT_MOC_LITERAL(357, 6),  // "height"
        QT_MOC_LITERAL(364, 15)   // "whiteBackground"
    },
    "ViewWidget",
    "updatedDimensions",
    "",
    "dimensionX",
    "dimensionY",
    "singleColorUpdate",
    "clusterId",
    "active",
    "addClusterToView",
    "removeClusterFromView",
    "addNewClusterToView",
    "QList<int>&",
    "fromClusters",
    "spikesRemovedFromClusters",
    "spikesAddedToCluster",
    "emptySelection",
    "updateClusters",
    "modifiedClusters",
    "isModifiedByDeletion",
    "undoUpdateClusters",
    "isThreadsRunning",
    "print",
    "QPainter&",
    "printPainter",
    "width",
    "height",
    "whiteBackground"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_ViewWidget[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      13,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    2,   92,    2, 0x0a,    1 /* Public */,
       5,    2,   97,    2, 0x0a,    4 /* Public */,
       8,    2,  102,    2, 0x0a,    7 /* Public */,
       9,    2,  107,    2, 0x0a,   10 /* Public */,
      10,    3,  112,    2, 0x0a,   13 /* Public */,
      10,    2,  119,    2, 0x0a,   17 /* Public */,
      13,    2,  124,    2, 0x0a,   20 /* Public */,
      14,    2,  129,    2, 0x0a,   23 /* Public */,
      15,    0,  134,    2, 0x0a,   26 /* Public */,
      16,    3,  135,    2, 0x0a,   27 /* Public */,
      19,    2,  142,    2, 0x0a,   31 /* Public */,
      20,    0,  147,    2, 0x10a,   34 /* Public | MethodIsConst  */,
      21,    4,  148,    2, 0x0a,   35 /* Public */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int, QMetaType::Int,    3,    4,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    6,    7,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    6,    7,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    6,    7,
    QMetaType::Void, 0x80000000 | 11, QMetaType::Int, QMetaType::Bool,   12,    6,    7,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    6,    7,
    QMetaType::Void, 0x80000000 | 11, QMetaType::Bool,   12,    7,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    6,    7,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 11, QMetaType::Bool, QMetaType::Bool,   17,    7,   18,
    QMetaType::Void, 0x80000000 | 11, QMetaType::Bool,   17,    7,
    QMetaType::Bool,
    QMetaType::Void, 0x80000000 | 22, QMetaType::Int, QMetaType::Int, QMetaType::Bool,   23,   24,   25,   26,

       0        // eod
};

Q_CONSTINIT const QMetaObject ViewWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<BaseFrame::staticMetaObject>(),
    qt_meta_stringdata_ViewWidget.offsetsAndSizes,
    qt_meta_data_ViewWidget,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_ViewWidget_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ViewWidget, std::true_type>,
        // method 'updatedDimensions'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'singleColorUpdate'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'addClusterToView'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'removeClusterFromView'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'addNewClusterToView'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'addNewClusterToView'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'spikesRemovedFromClusters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'spikesAddedToCluster'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'emptySelection'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'updateClusters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'undoUpdateClusters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'isThreadsRunning'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'print'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QPainter &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>
    >,
    nullptr
} };

void ViewWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ViewWidget *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->updatedDimensions((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 1: _t->singleColorUpdate((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 2: _t->addClusterToView((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 3: _t->removeClusterFromView((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 4: _t->addNewClusterToView((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[3]))); break;
        case 5: _t->addNewClusterToView((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 6: _t->spikesRemovedFromClusters((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 7: _t->spikesAddedToCluster((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 8: _t->emptySelection(); break;
        case 9: _t->updateClusters((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[3]))); break;
        case 10: _t->undoUpdateClusters((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 11: { bool _r = _t->isThreadsRunning();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 12: _t->print((*reinterpret_cast< std::add_pointer_t<QPainter&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[4]))); break;
        default: ;
        }
    }
}

const QMetaObject *ViewWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ViewWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ViewWidget.stringdata0))
        return static_cast<void*>(this);
    return BaseFrame::qt_metacast(_clname);
}

int ViewWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = BaseFrame::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 13)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 13;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 13)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 13;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
