
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVariantList>

class PairingManager
{
public:
    enum class CreateResult {
        Created,
        InvalidToken,
        AlreadyPending
    };

    PairingManager();

    QString startNewSession();
    void stopSession();

    QString pairingToken() const;
    bool hasPendingPairing() const;
    QString pendingDevice() const;
    QString activePairingId() const;

    CreateResult createRequest(
        const QString &presentedToken,
        const QString &device,
        int lifetimeSeconds,
        QString *requestId = nullptr,
        const QByteArray &signingPublicKey = QByteArray(),
        const QByteArray &encryptionPublicKey = QByteArray());

    bool approvePairing();
    bool rejectPairing();

    bool pairingStatus(
        const QString &id,
        QString *state,
        QString *sessionToken = nullptr) const;

    bool expireRequests(const QDateTime &now);

    bool isAuthorized(const QString &token) const;
    QString sessionDevice(const QString &token) const;

    // Returns the stored Ed25519 signing public key.
    QByteArray trustedDeviceSigningKey(const QString &token) const;

    // Returns the stored Curve25519 encryption public key.
    // Empty means the device must pair again before encrypted transfers.
    QByteArray trustedDeviceEncryptionKey(const QString &token) const;

    QVariantList trustedDeviceList() const;
    bool removeTrustedDevice(const QString &token);

private:
    struct PairingRequest {
        QString id;
        QString device;
        QString sessionToken;
        QString state;
        QDateTime expiresAt;
        QByteArray signingPublicKey;
        QByteArray encryptionPublicKey;
    };

    QString m_pairingToken;
    QString m_activePairingId;

    QHash<QString, PairingRequest> m_pairings;
    QHash<QString, QString> m_sessions;
    QHash<QString, QString> m_trustedDevices;
    QHash<QString, QByteArray> m_trustedSigningKeys;
    QHash<QString, QByteArray> m_trustedEncryptionKeys;

    static QString newToken();

    void loadTrustedDevices();

    bool saveTrustedDevice(
        const QString &token,
        const QString &device,
        const QByteArray &signingPublicKey,
        const QByteArray &encryptionPublicKey);
};
