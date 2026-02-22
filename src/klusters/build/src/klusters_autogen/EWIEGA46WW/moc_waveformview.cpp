/****************************************************************************
** Meta object code from reading C++ file 'waveformview.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../src/waveformview.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'waveformview.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_WaveformView_t {
    uint offsetsAndSizes[88];
    char stringdata0[13];
    char stringdata1[18];
    char stringdata2[1];
    char stringdata3[10];
    char stringdata4[7];
    char stringdata5[17];
    char stringdata6[22];
    char stringdata7[20];
    char stringdata8[12];
    char stringdata9[13];
    char stringdata10[26];
    char stringdata11[21];
    char stringdata12[8];
    char stringdata13[16];
    char stringdata14[13];
    char stringdata15[15];
    char stringdata16[17];
    char stringdata17[21];
    char stringdata18[19];
    char stringdata19[20];
    char stringdata20[28];
    char stringdata21[23];
    char stringdata22[26];
    char stringdata23[14];
    char stringdata24[17];
    char stringdata25[13];
    char stringdata26[6];
    char stringdata27[6];
    char stringdata28[8];
    char stringdata29[5];
    char stringdata30[18];
    char stringdata31[18];
    char stringdata32[19];
    char stringdata33[9];
    char stringdata34[17];
    char stringdata35[14];
    char stringdata36[20];
    char stringdata37[10];
    char stringdata38[19];
    char stringdata39[6];
    char stringdata40[10];
    char stringdata41[13];
    char stringdata42[7];
    char stringdata43[16];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_WaveformView_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_WaveformView_t qt_meta_stringdata_WaveformView = {
    {
        QT_MOC_LITERAL(0, 12),  // "WaveformView"
        QT_MOC_LITERAL(13, 17),  // "singleColorUpdate"
        QT_MOC_LITERAL(31, 0),  // ""
        QT_MOC_LITERAL(32, 9),  // "clusterId"
        QT_MOC_LITERAL(42, 6),  // "active"
        QT_MOC_LITERAL(49, 16),  // "addClusterToView"
        QT_MOC_LITERAL(66, 21),  // "removeClusterFromView"
        QT_MOC_LITERAL(88, 19),  // "addNewClusterToView"
        QT_MOC_LITERAL(108, 11),  // "QList<int>&"
        QT_MOC_LITERAL(120, 12),  // "fromClusters"
        QT_MOC_LITERAL(133, 25),  // "spikesRemovedFromClusters"
        QT_MOC_LITERAL(159, 20),  // "spikesAddedToCluster"
        QT_MOC_LITERAL(180, 7),  // "setMode"
        QT_MOC_LITERAL(188, 15),  // "BaseFrame::Mode"
        QT_MOC_LITERAL(204, 12),  // "selectedMode"
        QT_MOC_LITERAL(217, 14),  // "updateClusters"
        QT_MOC_LITERAL(232, 16),  // "modifiedClusters"
        QT_MOC_LITERAL(249, 20),  // "isModifiedByDeletion"
        QT_MOC_LITERAL(270, 18),  // "undoUpdateClusters"
        QT_MOC_LITERAL(289, 19),  // "setMeanPresentation"
        QT_MOC_LITERAL(309, 27),  // "setAllWaveformsPresentation"
        QT_MOC_LITERAL(337, 22),  // "setOverLayPresentation"
        QT_MOC_LITERAL(360, 25),  // "setSideBySidePresentation"
        QT_MOC_LITERAL(386, 13),  // "setSampleMode"
        QT_MOC_LITERAL(400, 16),  // "setTimeFrameMode"
        QT_MOC_LITERAL(417, 12),  // "setTimeFrame"
        QT_MOC_LITERAL(430, 5),  // "start"
        QT_MOC_LITERAL(436, 5),  // "width"
        QT_MOC_LITERAL(442, 7),  // "setGain"
        QT_MOC_LITERAL(450, 4),  // "gain"
        QT_MOC_LITERAL(455, 17),  // "increaseAmplitude"
        QT_MOC_LITERAL(473, 17),  // "decreaseAmplitude"
        QT_MOC_LITERAL(491, 18),  // "setDisplayNbSpikes"
        QT_MOC_LITERAL(510, 8),  // "nbSpikes"
        QT_MOC_LITERAL(519, 16),  // "isThreadsRunning"
        QT_MOC_LITERAL(536, 13),  // "updateDrawing"
        QT_MOC_LITERAL(550, 19),  // "setChannelPositions"
        QT_MOC_LITERAL(570, 9),  // "positions"
        QT_MOC_LITERAL(580, 18),  // "clustersRenumbered"
        QT_MOC_LITERAL(599, 5),  // "print"
        QT_MOC_LITERAL(605, 9),  // "QPainter&"
        QT_MOC_LITERAL(615, 12),  // "printPainter"
        QT_MOC_LITERAL(628, 6),  // "height"
        QT_MOC_LITERAL(635, 15)   // "whiteBackground"
    },
    "WaveformView",
    "singleColorUpdate",
    "",
    "clusterId",
    "active",
    "addClusterToView",
    "removeClusterFromView",
    "addNewClusterToView",
    "QList<int>&",
    "fromClusters",
    "spikesRemovedFromClusters",
    "spikesAddedToCluster",
    "setMode",
    "BaseFrame::Mode",
    "selectedMode",
    "updateClusters",
    "modifiedClusters",
    "isModifiedByDeletion",
    "undoUpdateClusters",
    "setMeanPresentation",
    "setAllWaveformsPresentation",
    "setOverLayPresentation",
    "setSideBySidePresentation",
    "setSampleMode",
    "setTimeFrameMode",
    "setTimeFrame",
    "start",
    "width",
    "setGain",
    "gain",
    "increaseAmplitude",
    "decreaseAmplitude",
    "setDisplayNbSpikes",
    "nbSpikes",
    "isThreadsRunning",
    "updateDrawing",
    "setChannelPositions",
    "positions",
    "clustersRenumbered",
    "print",
    "QPainter&",
    "printPainter",
    "height",
    "whiteBackground"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_WaveformView[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      26,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    2,  170,    2, 0x0a,    1 /* Public */,
       5,    2,  175,    2, 0x0a,    4 /* Public */,
       6,    2,  180,    2, 0x0a,    7 /* Public */,
       7,    3,  185,    2, 0x0a,   10 /* Public */,
       7,    2,  192,    2, 0x0a,   14 /* Public */,
      10,    2,  197,    2, 0x0a,   17 /* Public */,
      11,    2,  202,    2, 0x0a,   20 /* Public */,
      12,    1,  207,    2, 0x0a,   23 /* Public */,
      15,    3,  210,    2, 0x0a,   25 /* Public */,
      18,    2,  217,    2, 0x0a,   29 /* Public */,
      19,    0,  222,    2, 0x0a,   32 /* Public */,
      20,    0,  223,    2, 0x0a,   33 /* Public */,
      21,    0,  224,    2, 0x0a,   34 /* Public */,
      22,    0,  225,    2, 0x0a,   35 /* Public */,
      23,    0,  226,    2, 0x0a,   36 /* Public */,
      24,    0,  227,    2, 0x0a,   37 /* Public */,
      25,    2,  228,    2, 0x0a,   38 /* Public */,
      28,    1,  233,    2, 0x0a,   41 /* Public */,
      30,    0,  236,    2, 0x0a,   43 /* Public */,
      31,    0,  237,    2, 0x0a,   44 /* Public */,
      32,    1,  238,    2, 0x0a,   45 /* Public */,
      34,    0,  241,    2, 0x10a,   47 /* Public | MethodIsConst  */,
      35,    0,  242,    2, 0x0a,   48 /* Public */,
      36,    1,  243,    2, 0x0a,   49 /* Public */,
      38,    1,  246,    2, 0x0a,   51 /* Public */,
      39,    4,  249,    2, 0x0a,   53 /* Public */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    3,    4,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    3,    4,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    3,    4,
    QMetaType::Void, 0x80000000 | 8, QMetaType::Int, QMetaType::Bool,    9,    3,    4,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    3,    4,
    QMetaType::Void, 0x80000000 | 8, QMetaType::Bool,    9,    4,
    QMetaType::Void, QMetaType::Int, QMetaType::Bool,    3,    4,
    QMetaType::Void, 0x80000000 | 13,   14,
    QMetaType::Void, 0x80000000 | 8, QMetaType::Bool, QMetaType::Bool,   16,    4,   17,
    QMetaType::Void, 0x80000000 | 8, QMetaType::Bool,   16,    4,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Long, QMetaType::Long,   26,   27,
    QMetaType::Void, QMetaType::Int,   29,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Long,   33,
    QMetaType::Bool,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 8,   37,
    QMetaType::Void, QMetaType::Bool,    4,
    QMetaType::Void, 0x80000000 | 40, QMetaType::Int, QMetaType::Int, QMetaType::Bool,   41,   27,   42,   43,

       0        // eod
};

Q_CONSTINIT const QMetaObject WaveformView::staticMetaObject = { {
    QMetaObject::SuperData::link<ViewWidget::staticMetaObject>(),
    qt_meta_stringdata_WaveformView.offsetsAndSizes,
    qt_meta_data_WaveformView,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_WaveformView_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<WaveformView, std::true_type>,
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
        // method 'setMode'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<BaseFrame::Mode, std::false_type>,
        // method 'updateClusters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'undoUpdateClusters'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'setMeanPresentation'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setAllWaveformsPresentation'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setOverLayPresentation'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setSideBySidePresentation'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setSampleMode'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setTimeFrameMode'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setTimeFrame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<long, std::false_type>,
        QtPrivate::TypeAndForceComplete<long, std::false_type>,
        // method 'setGain'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'increaseAmplitude'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'decreaseAmplitude'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setDisplayNbSpikes'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<long, std::false_type>,
        // method 'isThreadsRunning'
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'updateDrawing'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setChannelPositions'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QList<int> &, std::false_type>,
        // method 'clustersRenumbered'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
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

void WaveformView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<WaveformView *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->singleColorUpdate((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 1: _t->addClusterToView((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 2: _t->removeClusterFromView((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 3: _t->addNewClusterToView((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[3]))); break;
        case 4: _t->addNewClusterToView((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 5: _t->spikesRemovedFromClusters((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 6: _t->spikesAddedToCluster((*reinterpret_cast< std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 7: _t->setMode((*reinterpret_cast< std::add_pointer_t<BaseFrame::Mode>>(_a[1]))); break;
        case 8: _t->updateClusters((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[3]))); break;
        case 9: _t->undoUpdateClusters((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[2]))); break;
        case 10: _t->setMeanPresentation(); break;
        case 11: _t->setAllWaveformsPresentation(); break;
        case 12: _t->setOverLayPresentation(); break;
        case 13: _t->setSideBySidePresentation(); break;
        case 14: _t->setSampleMode(); break;
        case 15: _t->setTimeFrameMode(); break;
        case 16: _t->setTimeFrame((*reinterpret_cast< std::add_pointer_t<long>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<long>>(_a[2]))); break;
        case 17: _t->setGain((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 18: _t->increaseAmplitude(); break;
        case 19: _t->decreaseAmplitude(); break;
        case 20: _t->setDisplayNbSpikes((*reinterpret_cast< std::add_pointer_t<long>>(_a[1]))); break;
        case 21: { bool _r = _t->isThreadsRunning();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 22: _t->updateDrawing(); break;
        case 23: _t->setChannelPositions((*reinterpret_cast< std::add_pointer_t<QList<int>&>>(_a[1]))); break;
        case 24: _t->clustersRenumbered((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 25: _t->print((*reinterpret_cast< std::add_pointer_t<QPainter&>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[3])),(*reinterpret_cast< std::add_pointer_t<bool>>(_a[4]))); break;
        default: ;
        }
    }
}

const QMetaObject *WaveformView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *WaveformView::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_WaveformView.stringdata0))
        return static_cast<void*>(this);
    return ViewWidget::qt_metacast(_clname);
}

int WaveformView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = ViewWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 26)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 26;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 26)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 26;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
