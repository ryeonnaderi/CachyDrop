/****************************************************************************
** Meta object code from reading C++ file 'ReceiverServer.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.12.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../CachyDrop/src/ReceiverServer.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ReceiverServer.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.12.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN14ReceiverServerE_t {};
} // unnamed namespace

template <> constexpr inline auto ReceiverServer::qt_create_metaobjectdata<qt_meta_tag_ZN14ReceiverServerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ReceiverServer",
        "runningChanged",
        "",
        "pendingRequestChanged",
        "pairingRequestChanged",
        "sharedFilesChanged",
        "trustedDevicesChanged",
        "openMultiFilePicker",
        "removeTrustedDevice",
        "token",
        "shareFiles",
        "paths",
        "start",
        "stop",
        "approveRequest",
        "rejectRequest",
        "approvePairing",
        "deviceName",
        "rejectPairing",
        "running",
        "address",
        "pairingUrl",
        "pairingCode",
        "qrRows",
        "hasPendingRequest",
        "pendingFilename",
        "pendingFilenames",
        "pendingSize",
        "pendingFileCount",
        "pendingSender",
        "hasPendingPairing",
        "pendingDevice",
        "sharedFileCount",
        "trustedDeviceList",
        "QVariantList"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'runningChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'pendingRequestChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'pairingRequestChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'sharedFilesChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'trustedDevicesChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'openMultiFilePicker'
        QtMocHelpers::MethodData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'removeTrustedDevice'
        QtMocHelpers::MethodData<bool(const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 9 },
        }}),
        // Method 'shareFiles'
        QtMocHelpers::MethodData<int(const QStringList &)>(10, 2, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::QStringList, 11 },
        }}),
        // Method 'start'
        QtMocHelpers::MethodData<bool()>(12, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'stop'
        QtMocHelpers::MethodData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'approveRequest'
        QtMocHelpers::MethodData<void()>(14, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'rejectRequest'
        QtMocHelpers::MethodData<void()>(15, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'approvePairing'
        QtMocHelpers::MethodData<void(const QString &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 17 },
        }}),
        // Method 'rejectPairing'
        QtMocHelpers::MethodData<void()>(18, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'running'
        QtMocHelpers::PropertyData<bool>(19, QMetaType::Bool, QMC::DefaultPropertyFlags, 0),
        // property 'address'
        QtMocHelpers::PropertyData<QString>(20, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'pairingUrl'
        QtMocHelpers::PropertyData<QString>(21, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'pairingCode'
        QtMocHelpers::PropertyData<QString>(22, QMetaType::QString, QMC::DefaultPropertyFlags, 0),
        // property 'qrRows'
        QtMocHelpers::PropertyData<QStringList>(23, QMetaType::QStringList, QMC::DefaultPropertyFlags, 0),
        // property 'hasPendingRequest'
        QtMocHelpers::PropertyData<bool>(24, QMetaType::Bool, QMC::DefaultPropertyFlags, 1),
        // property 'pendingFilename'
        QtMocHelpers::PropertyData<QString>(25, QMetaType::QString, QMC::DefaultPropertyFlags, 1),
        // property 'pendingFilenames'
        QtMocHelpers::PropertyData<QStringList>(26, QMetaType::QStringList, QMC::DefaultPropertyFlags, 1),
        // property 'pendingSize'
        QtMocHelpers::PropertyData<qint64>(27, QMetaType::LongLong, QMC::DefaultPropertyFlags, 1),
        // property 'pendingFileCount'
        QtMocHelpers::PropertyData<int>(28, QMetaType::Int, QMC::DefaultPropertyFlags, 1),
        // property 'pendingSender'
        QtMocHelpers::PropertyData<QString>(29, QMetaType::QString, QMC::DefaultPropertyFlags, 1),
        // property 'hasPendingPairing'
        QtMocHelpers::PropertyData<bool>(30, QMetaType::Bool, QMC::DefaultPropertyFlags, 2),
        // property 'pendingDevice'
        QtMocHelpers::PropertyData<QString>(31, QMetaType::QString, QMC::DefaultPropertyFlags, 2),
        // property 'sharedFileCount'
        QtMocHelpers::PropertyData<int>(32, QMetaType::Int, QMC::DefaultPropertyFlags, 3),
        // property 'trustedDeviceList'
        QtMocHelpers::PropertyData<QVariantList>(33, 0x80000000 | 34, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 4),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ReceiverServer, qt_meta_tag_ZN14ReceiverServerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ReceiverServer::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14ReceiverServerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14ReceiverServerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14ReceiverServerE_t>.metaTypes,
    nullptr
} };

void ReceiverServer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ReceiverServer *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->runningChanged(); break;
        case 1: _t->pendingRequestChanged(); break;
        case 2: _t->pairingRequestChanged(); break;
        case 3: _t->sharedFilesChanged(); break;
        case 4: _t->trustedDevicesChanged(); break;
        case 5: _t->openMultiFilePicker(); break;
        case 6: { bool _r = _t->removeTrustedDevice((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 7: { int _r = _t->shareFiles((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 8: { bool _r = _t->start();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 9: _t->stop(); break;
        case 10: _t->approveRequest(); break;
        case 11: _t->rejectRequest(); break;
        case 12: _t->approvePairing((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->rejectPairing(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (ReceiverServer::*)()>(_a, &ReceiverServer::runningChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (ReceiverServer::*)()>(_a, &ReceiverServer::pendingRequestChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (ReceiverServer::*)()>(_a, &ReceiverServer::pairingRequestChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (ReceiverServer::*)()>(_a, &ReceiverServer::sharedFilesChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (ReceiverServer::*)()>(_a, &ReceiverServer::trustedDevicesChanged, 4))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<bool*>(_v) = _t->running(); break;
        case 1: *reinterpret_cast<QString*>(_v) = _t->address(); break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->pairingUrl(); break;
        case 3: *reinterpret_cast<QString*>(_v) = _t->pairingCode(); break;
        case 4: *reinterpret_cast<QStringList*>(_v) = _t->qrRows(); break;
        case 5: *reinterpret_cast<bool*>(_v) = _t->hasPendingRequest(); break;
        case 6: *reinterpret_cast<QString*>(_v) = _t->pendingFilename(); break;
        case 7: *reinterpret_cast<QStringList*>(_v) = _t->pendingFilenames(); break;
        case 8: *reinterpret_cast<qint64*>(_v) = _t->pendingSize(); break;
        case 9: *reinterpret_cast<int*>(_v) = _t->pendingFileCount(); break;
        case 10: *reinterpret_cast<QString*>(_v) = _t->pendingSender(); break;
        case 11: *reinterpret_cast<bool*>(_v) = _t->hasPendingPairing(); break;
        case 12: *reinterpret_cast<QString*>(_v) = _t->pendingDevice(); break;
        case 13: *reinterpret_cast<int*>(_v) = _t->sharedFileCount(); break;
        case 14: *reinterpret_cast<QVariantList*>(_v) = _t->trustedDeviceList(); break;
        default: break;
        }
    }
}

const QMetaObject *ReceiverServer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ReceiverServer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14ReceiverServerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int ReceiverServer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 14;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 15;
    }
    return _id;
}

// SIGNAL 0
void ReceiverServer::runningChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void ReceiverServer::pendingRequestChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void ReceiverServer::pairingRequestChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void ReceiverServer::sharedFilesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void ReceiverServer::trustedDevicesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
