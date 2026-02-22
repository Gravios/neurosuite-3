/****************************************************************************
** Meta object code from reading C++ file 'errormatrixview.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../src/errormatrixview.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'errormatrixview.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_ErrorMatrixView_t {
    uint offsetsAndSizes[86];
    char stringdata0[16];
    char stringdata1[17];
    char stringdata2[1];
    char stringdata3[21];
    char stringdata4[16];
    char stringdata5[12];
    char stringdata6[16];
    char stringdata7[13];
    char stringdata8[16];
    char stringdata9[16];
    char stringdata10[19];
    char stringdata11[25];
    char stringdata12[13];
    char stringdata13[21];
    char stringdata14[16];
    char stringdata15[16];
    char stringdata16[10];
    char stringdata17[17];
    char stringdata18[15];
    char stringdata19[20];
    char stringdata20[20];
    char stringdata21[9];
    char stringdata22[17];
    char stringdata23[16];
    char stringdata24[17];
    char stringdata25[25];
    char stringdata26[14];
    char stringdata27[16];
    char stringdata28[13];
    char stringdata29[17];
    char stringdata30[16];
    char stringdata31[25];
    char stringdata32[17];
    char stringdata33[21];
    char stringdata34[13];
    char stringdata35[17];
    char stringdata36[13];
    char stringdata37[6];
    char stringdata38[10];
    char stringdata39[13];
    char stringdata40[6];
    char stringdata41[7];
    char stringdata42[16];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_ErrorMatrixView_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_ErrorMatrixView_t qt_meta_stringdata_ErrorMatrixView = {
    {
        QT_MOC_LITERAL(0, 15),  // "ErrorMatrixView"
        QT_MOC_LITERAL(16, 16),  // "isThreadsRunning"
        QT_MOC_LITERAL(33, 0),  // ""
        QT_MOC_LITERAL(34, 20),  // "updateMatrixContents"
        QT_MOC_LITERAL(55, 15),  // "clustersGrouped"
        QT_MOC_LITERAL(71, 11),  // "QList<int>&"
        QT_MOC_LITERAL(83, 15),  // "groupedClusters"
        QT_MOC_LITERAL(99, 12),  // "newClusterId"
        QT_MOC_LITERAL(112, 15),  // "clustersDeleted"
        QT_MOC_LITERAL(128, 15),  // "deletedClusters"
        QT_MOC_LITERAL(144, 18),  // "destinationCluster"
        QT_MOC_LITERAL(163, 24),  // "removeSpikesFromClusters"
        QT_MOC_LITERAL(188, 12),  // "fromClusters"
        QT_MOC_LITERAL(201, 20),  // "destinationClusterId"
        QT_MOC_LITERAL(222, 15),  // "emptiedClusters"
        QT_MOC_LITERAL(238, 15),  // "newClusterAdded"
        QT_MOC_LITERAL(254, 9),  // "clusterId"
        QT_MOC_LITERAL(264, 16),  // "newClustersAdded"
        QT_MOC_LITERAL(281, 14),  // "QMap<int,int>&"
        QT_MOC_LITERAL(296, 19),  // "fromToNewClusterIds"
        QT_MOC_LITERAL(316, 19),  // "clustersToRecluster"
        QT_MOC_LITERAL(336, 8),  // "renumber"
        QT_MOC_LITERAL(345, 16),  // "clusterIdsOldNew"
        QT_MOC_LITERAL(362, 15),  // "undoRenumbering"
        QT_MOC_LITERAL(378, 16),  // "clusterIdsNewOld"
        QT_MOC_LITERAL(395, 24),  // "undoAdditionModification"
        QT_MOC_LITERAL(420, 13),  // "addedClusters"
        QT_MOC_LITERAL(434, 15),  // "updatedClusters"
        QT_MOC_LITERAL(450, 12),  // "undoAddition"
        QT_MOC_LITERAL(463, 16),  // "undoModification"
        QT_MOC_LITERAL(480, 15),  // "redoRenumbering"
        QT_MOC_LITERAL(496, 24),  // "redoAdditionModification"
        QT_MOC_LITERAL(521, 16),  // "modifiedClusters"
        QT_MOC_LITERAL(538, 20),  // "isModifiedByDeletion"
        QT_MOC_LITERAL(559, 12),  // "redoAddition"
        QT_MOC_LITERAL(572, 16),  // "redoModification"
        QT_MOC_LITERAL(589, 12),  // "redoDeletion"
        QT_MOC_LITERAL(602, 5),  // "print"
        QT_MOC_LITERAL(608, 9),  // "QPainter&"
        QT_MOC_LITERAL(618, 12),  // "printPainter"
        QT_MOC_LITERAL(631, 5),  // "width"
        QT_MOC_LITERAL(637, 6),  // "height"
        QT_MOC_LITERAL(644, 15)   // "whiteBackground"
    },
    "ErrorMatrixView",
    "isThreadsRunning",
    "",
    "updateMatrixContents",
    "clustersGrouped",
    "QList<int>&",
    "groupedClusters",
    "newClusterId",
    "clustersDeleted",
    "deletedClusters",
    "destinationCluster",
    "removeSpikesFromClusters",
    "fromClusters",
    "destinationClusterId",
    "emptiedClusters",
    "newClusterAdded",
    "clusterId",
    "newClustersAdded",
    "QMap<int,int>&",
    "fromToNewClusterIds",
    "clustersToRecluster",
    "renumber",
    "clusterIdsOldNew",
    "undoRenumbering",
    "clusterIdsNewOld",
    "undoAdditionModification",
    "addedClusters",
    "updatedClusters",
    "undoAddition",
    "undoModification",
    "redoRenumbering",
    "redoAdditionModification",
    "modifiedClusters",
    "isModifiedByDeletion",
    "redoAddition",
    "redoModification",
    "redoDeletion",
    "print",
    "QPainter&",
    "printPainter",
    "width",
    "height",
    "whiteBackground"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_ErrorMatrixView[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      19,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    0,  128,    2, 0x10a,    1 /* Public | MethodIsConst  */,
       3,    0,  129,    2, 0x0a,    2 /* Public */,
       4,    2,  130,    2, 0x0a,    3 /* Public */,
       8,    2,  135,    2, 0x0a,    6 /* Public */,
      11,    3,  140,    2, 0x0a,    9 /* Public */,
      15,    3,  147,    2, 0x0a,   13 /* Public */,
      17,    2,  154,    2, 0x0a,   17 /* Public */,
      17,    1,  159,    2, 0x0a,   20 /* Public */,
      21,    1,  162,    2, 0x0a,   22 /* Public */,
      23,    1,  165,    2, 0x0a,   24 /* Public */,
      25,    2,  168,    2, 0x0a,   26 /* Public */,
      28,    1,  173,    2, 0x0a,   29 /* Public */,
      29,    1,  176,    2, 0x0a,   31 /* Public */,
      30,    1,  179,    2, 0x0a,   33 /* Public */,
      31,    4,  182,    2, 0x0a,   35 /* Public */,
      34,    2,  191,    2, 0x0a,   40 /* Public */,
      35,    3,  196,    2, 0x0a,   43 /* Public */,
      36,    1,  203,    2, 0x0a,   47 /* Public */,
      37,    4,  206,    2, 0x0a,   49 /* Public */,

 // slots: parameters
    QMetaType::Bool,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 5, QMetaType::Int,    6,    7,
    QMetaType::Void, 0x80000000 | 5, QMetaType::Int,    9,   10,
    QMetaType::Void, 0x80000000 | 5, QMetaType::Int, 0x80000000 | 5,   12,   13,   14,
    QMetaType::Void, 0x80000000 | 5, QMetaType::Int, 0x80000000 | 5,   12,   16,   14,
    QMetaType::Void, 0x80000000 | 18, 0x80000000 | 5,   19,   14,
    QMetaType::Void, 0x80000000 | 5,   20,
    QMetaType::Void, 0x80000000 | 18,   22,
    QMetaType::Void, 0x80000000 | 18,   24,
    QMetaType::Void, 0x80000000 | 5, 0x80000000 | 5,   26,   27,
    QMetaType::Void, 0x80000000 | 5,   26,
    QMetaType::Void, 0x80000000 | 5,   27,
    QMetaType::Void, 0x80000000 | 18,   22,
    QMetaType::Void, 0x80000000 | 5, 0x80000000 | 5, QMetaType::Bool, 0x80000000 | 5,   26,   32,   33,    9,
    QMetaType::Void, 0x80000000 | 5, 0x80000000 | 5,   26,    9,
    QMetaType::Void, 0x80000000 | 5, QMetaType::Bool, 0x80000000 | 5,   27,   33,    9,
    QMetaType::Void, 0x80000000 | 5,    9,
    QMetaType::Void, 0x80000000 | 38, QMetaType::Int, QMetaType::Int, QMetaType::Bool,   39,   40,   41,   42,

       0        // eod
};

Q_CONSTINIT const QMetaObject ErrorMatrixView::staticMetaObject = { {
    QMetaObject::SuperData::link<ViewWidget::staticMetaObject>(),
    qt_meta_stringdata_ErrorMatrixView.offsetsAndSizes,
    qt_meta_data_ErrorMatrixView,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_ErrorMatrixView_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ErrorMatrixView, std::true_type>,
        // method 'isThreadsRunning'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'updateMatrixContents'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'clustersGrouped'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'clustersDeleted'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'removeSpikesFromClusters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'newClusterAdded'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'newClustersAdded'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMap<int,int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'newClustersAdded'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'renumber'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMap<int,int> &, std::false_type>,
        // method 'undoRenumbering'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMap<int,int> &, std::false_type>,
        // method 'undoAdditionModification'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'undoAddition'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'undoModification'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'redoRenumbering'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QMap<int,int> &, std::false_type>,
        // method 'redoAdditionModification'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'redoAddition'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'redoModification'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'redoDeletion'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'print'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QPainter &, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>
    >,
    nullptr
} };

void ErrorMatrixView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ErrorMatrixView *>(_o);
        (void)_t;
        switch (_id) {
        case 0: { bool _r = _t->isThreadsRunning();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 1: _t->updateMatrixContents(); break;
        case 2: _t->clustersGrouped((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 3: _t->clustersDeleted((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 4: _t->removeSpikesFromClusters((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[3]))); break;
        case 5: _t->newClusterAdded((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[3]))); break;
        case 6: _t->newClustersAdded((*reinterpret_cast< std::add_pointer_t<QMap<int,int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[2]))); break;
        case 7: _t->newClustersAdded((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1]))); break;
        case 8: _t->renumber((*reinterpret_cast< std::add_pointer_t<QMap<int,int>&>>(_a[1]))); break;
        case 9: _t->undoRenumbering((*reinterpret_cast< std::add_pointer_t<QMap<int,int>&>>(_a[1]))); break;
        case 10: _t->undoAdditionModification((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[2]))); break;
        case 11: _t->undoAddition((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1]))); break;
        case 12: _t->undoModification((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1]))); break;
        case 13: _t->redoRenumbering((*reinterpret_cast< std::add_pointer_t<QMap<int,int>&>>(_a[1]))); break;
        case 14: _t->redoAdditionModification((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[4]))); break;
        case 15: _t->redoAddition((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[2]))); break;
        case 16: _t->redoModification((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[3]))); break;
        case 17: _t->redoDeletion((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1]))); break;
        case 18: _t->print((*reinterpret_cast< std::add_pointer_t<QPainter&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[4]))); break;
        default: ;
        }
    }
}

const QMetaObject *ErrorMatrixView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ErrorMatrixView::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ErrorMatrixView.stringdata0))
        return static_cast<void*>(this);
    return ViewWidget::qt_metacast(_clname);
}

int ErrorMatrixView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = ViewWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 19)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 19;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 19)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 19;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
