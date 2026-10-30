
#pragma once

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

    // Start/stop the pairing session without deleting trusted devices.
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
        QString *requestId = nullptr);

    bool approvePairing();
    bool rejectPairing();

    // Returns false when the request has expired or does not exist.
    bool pairingStatus(
        const QString &id,
        QString *state,
        QString *sessionToken = nullptr) const;

    // Returns true if at least one request changed due to expiry.
    bool expireRequests(const QDateTime &now);

    bool isAuthorized(const QString &token) const;
    QString sessionDevice(const QString &token) const;

    QVariantList trustedDeviceList() const;
    bool removeTrustedDevice(const QString &token);

private:
    struct PairingRequest {
        QString id;
        QString device;
        QString sessionToken;
        QString state;
        QDateTime expiresAt;
    };

    QString m_pairingToken;
    QString m_activePairingId;

    QHash<QString, PairingRequest> m_pairings;
    QHash<QString, QString> m_sessions;
    QHash<QString, QString> m_trustedDevices;

    static QString newToken();
    void loadTrustedDevices();
    void saveTrustedDevice(const QString &token, const QString &device);
};
