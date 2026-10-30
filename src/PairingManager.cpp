
#include "PairingManager.h"

#include <QSettings>
#include <QUuid>

namespace {
constexpr int RejectedRequestLifetimeSeconds = 30;
}

PairingManager::PairingManager()
{
    loadTrustedDevices();
}

QString PairingManager::newToken()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void PairingManager::loadTrustedDevices()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("trustedDevices"));

    const QStringList keys = settings.childKeys();
    for (const QString &token : keys) {
        m_trustedDevices.insert(token, settings.value(token).toString());
    }

    settings.endGroup();
}

void PairingManager::saveTrustedDevice(
    const QString &token,
    const QString &device)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("trustedDevices"));
    settings.setValue(token, device);
    settings.endGroup();
    settings.sync();
}

QString PairingManager::startNewSession()
{
    m_pairings.clear();
    m_sessions.clear();
    m_activePairingId.clear();

    m_pairingToken = newToken();
    return m_pairingToken;
}

void PairingManager::stopSession()
{
    m_pairingToken.clear();
    m_pairings.clear();
    m_sessions.clear();
    m_activePairingId.clear();

    // Deliberately preserve m_trustedDevices and its settings.
}

QString PairingManager::pairingToken() const
{
    return m_pairingToken;
}

bool PairingManager::hasPendingPairing() const
{
    const auto it = m_pairings.constFind(m_activePairingId);
    return it != m_pairings.cend() && it->state == QStringLiteral("pending");
}

QString PairingManager::pendingDevice() const
{
    const auto it = m_pairings.constFind(m_activePairingId);
    if (it == m_pairings.cend()
        || it->state != QStringLiteral("pending")) {
        return {};
    }

    return it->device;
}

QString PairingManager::activePairingId() const
{
    return m_activePairingId;
}

PairingManager::CreateResult PairingManager::createRequest(
    const QString &presentedToken,
    const QString &device,
    int lifetimeSeconds,
    QString *requestId)
{
    if (presentedToken.isEmpty()
        || presentedToken != m_pairingToken) {
        return CreateResult::InvalidToken;
    }

    if (hasPendingPairing()) {
        return CreateResult::AlreadyPending;
    }

    PairingRequest request;
    request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    request.device = device.trimmed().left(100);
    if (request.device.isEmpty()) {
        request.device = QStringLiteral("Unknown device");
    }

    request.sessionToken = newToken();
    request.state = QStringLiteral("pending");
    request.expiresAt = QDateTime::currentDateTimeUtc()
        .addSecs(qMax(1, lifetimeSeconds));

    m_activePairingId = request.id;
    m_pairings.insert(request.id, request);

    if (requestId) {
        *requestId = request.id;
    }

    return CreateResult::Created;
}

bool PairingManager::approvePairing()
{
    auto it = m_pairings.find(m_activePairingId);
    if (it == m_pairings.end()
        || it->state != QStringLiteral("pending")
        || it->expiresAt <= QDateTime::currentDateTimeUtc()) {
        return false;
    }

    it->state = QStringLiteral("approved");

    m_sessions.insert(it->sessionToken, it->device);
    m_trustedDevices.insert(it->sessionToken, it->device);
    saveTrustedDevice(it->sessionToken, it->device);

    return true;
}

bool PairingManager::rejectPairing()
{
    auto it = m_pairings.find(m_activePairingId);
    if (it == m_pairings.end()
        || it->state != QStringLiteral("pending")) {
        return false;
    }

    it->state = QStringLiteral("rejected");
    it->expiresAt = QDateTime::currentDateTimeUtc()
        .addSecs(RejectedRequestLifetimeSeconds);

    return true;
}

bool PairingManager::pairingStatus(
    const QString &id,
    QString *state,
    QString *sessionToken) const
{
    const auto it = m_pairings.constFind(id);
    if (it == m_pairings.cend()
        || it->expiresAt <= QDateTime::currentDateTimeUtc()) {
        return false;
    }

    if (state) {
        *state = it->state;
    }

    if (sessionToken) {
        *sessionToken = it->state == QStringLiteral("approved")
            ? it->sessionToken
            : QString();
    }

    return true;
}

bool PairingManager::expireRequests(const QDateTime &now)
{
    bool changed = false;

    for (auto it = m_pairings.begin(); it != m_pairings.end();) {
        if (it->expiresAt <= now) {
            if (it.key() == m_activePairingId) {
                m_activePairingId.clear();
            }
            it = m_pairings.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    return changed;
}

bool PairingManager::isAuthorized(const QString &token) const
{
    return !token.isEmpty()
        && (m_sessions.contains(token)
            || m_trustedDevices.contains(token));
}

QString PairingManager::sessionDevice(const QString &token) const
{
    return m_sessions.value(token, m_trustedDevices.value(token));
}

QVariantList PairingManager::trustedDeviceList() const
{
    QVariantList result;

    for (auto it = m_trustedDevices.cbegin();
         it != m_trustedDevices.cend(); ++it) {
        QVariantMap item;
        item.insert(QStringLiteral("token"), it.key());
        item.insert(QStringLiteral("name"), it.value());
        result.append(item);
    }

    return result;
}

bool PairingManager::removeTrustedDevice(const QString &token)
{
    if (!m_trustedDevices.contains(token)) {
        return false;
    }

    m_trustedDevices.remove(token);
    m_sessions.remove(token);

    QSettings settings;
    settings.beginGroup(QStringLiteral("trustedDevices"));
    settings.remove(token);
    settings.endGroup();
    settings.sync();

    return true;
}
