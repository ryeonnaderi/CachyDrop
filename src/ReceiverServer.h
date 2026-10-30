
#pragma once

#include <QObject>
#include <QTcpServer>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QDateTime>
#include <QVector>
#include <QVariant>
#include "PairingManager.h"

class ReceiverServer : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString address READ address NOTIFY runningChanged)
    Q_PROPERTY(QString pairingCode READ pairingCode NOTIFY runningChanged)
    Q_PROPERTY(QStringList qrRows READ qrRows NOTIFY runningChanged)

    Q_PROPERTY(bool hasPendingRequest READ hasPendingRequest
               NOTIFY pendingRequestChanged)
    Q_PROPERTY(QString pendingFilename READ pendingFilename
               NOTIFY pendingRequestChanged)
    Q_PROPERTY(QStringList pendingFilenames READ pendingFilenames
               NOTIFY pendingRequestChanged)
    Q_PROPERTY(qint64 pendingSize READ pendingSize
               NOTIFY pendingRequestChanged)
    Q_PROPERTY(int pendingFileCount READ pendingFileCount
               NOTIFY pendingRequestChanged)
    Q_PROPERTY(QString pendingSender READ pendingSender
               NOTIFY pendingRequestChanged)

    Q_PROPERTY(bool hasPendingPairing READ hasPendingPairing
               NOTIFY pairingRequestChanged)
    Q_PROPERTY(QString pendingDevice READ pendingDevice
               NOTIFY pairingRequestChanged)

    Q_PROPERTY(int sharedFileCount READ sharedFileCount
               NOTIFY sharedFilesChanged)
    Q_PROPERTY(QVariantList trustedDeviceList READ trustedDeviceList
           NOTIFY trustedDevicesChanged)

public:
    explicit ReceiverServer(QObject *parent = nullptr);
    Q_INVOKABLE void openMultiFilePicker();

    bool running() const;
    QString address() const;
    QString pairingCode() const;
    QStringList qrRows() const;

    bool hasPendingRequest() const;
    QString pendingFilename() const;
    QStringList pendingFilenames() const;
    qint64 pendingSize() const;
    int pendingFileCount() const;
    QString pendingSender() const;

    bool hasPendingPairing() const;
    QString pendingDevice() const;

    int sharedFileCount() const;
    QVariantList trustedDeviceList() const;

    Q_INVOKABLE bool removeTrustedDevice(const QString &token);

    Q_INVOKABLE int shareFiles(const QStringList &paths);
    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();

    Q_INVOKABLE void approveRequest();
    Q_INVOKABLE void rejectRequest();

    Q_INVOKABLE void approvePairing();
    Q_INVOKABLE void rejectPairing();

signals:
    void runningChanged();
    void pendingRequestChanged();
    void pairingRequestChanged();
    void sharedFilesChanged();
    void trustedDevicesChanged();

private:
    struct TransferFile {
        QString filename;
        qint64 size = 0;
    };

    struct TransferRequest {
        QString id;
        QVector<TransferFile> files;
        QString sender;
        QString sessionToken;
        qint64 totalSize = 0;
        QString state;
        QDateTime expiresAt;
    };

    struct SharedFile {
        QString id;
        QString path;
        QString filename;
        qint64 size = 0;
    };

    QTcpServer m_server;
    PairingManager m_pairingManager;

    QString m_address;
    QString m_pairingCode;
    QString m_activeRequestId;
    QStringList m_qrRows;

    QHash<QString, TransferRequest> m_requests;
    QHash<QString, SharedFile> m_sharedFiles;

    void handleConnection();
    void expireRequests();
};

