
#pragma once

#include <QObject>
#include <QTcpServer>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QDateTime>

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
    Q_PROPERTY(qint64 pendingSize READ pendingSize
               NOTIFY pendingRequestChanged)
    Q_PROPERTY(QString pendingSender READ pendingSender
               NOTIFY pendingRequestChanged)

    Q_PROPERTY(bool hasPendingPairing READ hasPendingPairing
               NOTIFY pairingRequestChanged)
    Q_PROPERTY(QString pendingDevice READ pendingDevice
               NOTIFY pairingRequestChanged)

    Q_PROPERTY(int sharedFileCount READ sharedFileCount
               NOTIFY sharedFilesChanged)

public:
    explicit ReceiverServer(QObject *parent = nullptr);

    bool running() const;
    QString address() const;
    QString pairingCode() const;
    QStringList qrRows() const;

    bool hasPendingRequest() const;
    QString pendingFilename() const;
    qint64 pendingSize() const;
    QString pendingSender() const;

    bool hasPendingPairing() const;
    QString pendingDevice() const;

    int sharedFileCount() const;

    Q_INVOKABLE int shareFiles(const QStringList &paths);
    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();

    // Incoming file-transfer approval.
    Q_INVOKABLE void approveRequest();
    Q_INVOKABLE void rejectRequest();

    // First-time device-pairing approval.
    Q_INVOKABLE void approvePairing();
    Q_INVOKABLE void rejectPairing();

signals:
    void runningChanged();
    void pendingRequestChanged();
    void pairingRequestChanged();
    void sharedFilesChanged();

private:
    struct TransferRequest {
        QString id;
        QString filename;
        QString sender;
        QString sessionToken;
        qint64 size = 0;
        QString state;
        QDateTime expiresAt;
    };

    struct PairingRequest {
        QString id;
        QString device;
        QString sessionToken;
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

    QString m_address;
    QString m_pairingCode;
    QString m_pairingToken;

    QString m_activeRequestId;
    QString m_activePairingId;

    QStringList m_qrRows;

    QHash<QString, TransferRequest> m_requests;
    QHash<QString, PairingRequest> m_pairings;
    QHash<QString, QString> m_sessions;
    QHash<QString, SharedFile> m_sharedFiles;

    void handleConnection();
    void expireRequests();
};
