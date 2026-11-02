
#include "PairingManager.h"

#include <QRandomGenerator>
#include <QSettings>
#include <QCryptographicHash>
#include <QByteArray>
#include <QDateTime>
#include <QVariantMap>

#include <sodium.h>

namespace {
constexpr int Ed25519PublicKeySize = crypto_sign_PUBLICKEYBYTES;
constexpr int EncryptionPublicKeySize = crypto_box_PUBLICKEYBYTES;

QString toHexString(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex());
}

QByteArray fromHexString(const QString &hex)
{
    return QByteArray::fromHex(hex.toLatin1());
}
}

PairingManager::PairingManager()
{
    if (sodium_init() < 0) {
        return;
    }

    loadTrustedDevices();
}

QString PairingManager::newToken()
{
    QByteArray randomBytes(32, Qt::Uninitialized);

    randombytes_buf(randomBytes.data(),
                    static_cast<size_t>(randomBytes.size()));

    return QString::fromLatin1(
        randomBytes.toHex());
}

QString PairingManager::startNewSession()
{
    m_pairingToken = newToken();
    m_activePairingId.clear();
    m_pairings.clear();
    m_sessions.clear();

    return m_pairingToken;
}

void PairingManager::stopSession()
{
    m_pairingToken.clear();
    m_activePairingId.clear();
    m_pairings.clear();
    m_sessions.clear();
}

QString PairingManager::pairingToken() const
{
    return m_pairingToken;
}

bool PairingManager::hasPendingPairing() const
{
    const auto it = m_pairings.constFind(m_activePairingId);

    return it != m_pairings.cend()
        && it->state == QStringLiteral("pending")
        && it->expiresAt > QDateTime::currentDateTimeUtc();
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

void PairingManager::loadTrustedDevices()
{
    QSettings settings;

    QHash<QString, QString> loadedDevices;
    QHash<QString, QByteArray> loadedSigningKeys;
    QHash<QString, QByteArray> loadedEncryptionKeys;

    // Load device names first.
    settings.beginGroup(QStringLiteral("trustedDevices"));

    for (const QString &token : settings.childKeys()) {
        loadedDevices.insert(
            token,
            settings.value(token).toString());
    }

    settings.endGroup();

    // Load signing public keys only for known devices.
    settings.beginGroup(
        QStringLiteral("trustedDeviceSigningKeys"));

    for (const QString &token : settings.childKeys()) {
        if (!loadedDevices.contains(token)) {
            continue;
        }

        const QByteArray key = fromHexString(
            settings.value(token).toString());

        if (key.size() == Ed25519PublicKeySize) {
            loadedSigningKeys.insert(token, key);
        }
    }

    settings.endGroup();

    // Load encryption public keys only for known devices.
    settings.beginGroup(
        QStringLiteral("trustedDeviceEncryptionKeys"));

    for (const QString &token : settings.childKeys()) {
        if (!loadedDevices.contains(token)) {
            continue;
        }

        const QByteArray key = fromHexString(
            settings.value(token).toString());

        if (key.size() == EncryptionPublicKeySize) {
            loadedEncryptionKeys.insert(token, key);
        }
    }

    settings.endGroup();

    m_trustedDevices = loadedDevices;
    m_trustedSigningKeys = loadedSigningKeys;
    m_trustedEncryptionKeys = loadedEncryptionKeys;
}

bool PairingManager::saveTrustedDevice(
    const QString &token,
    const QString &device,
    const QByteArray &signingPublicKey,
    const QByteArray &encryptionPublicKey)
{
    if (token.isEmpty() || device.isEmpty()) {
        return false;
    }

    if (!signingPublicKey.isEmpty()
        && signingPublicKey.size() != Ed25519PublicKeySize) {
        return false;
    }

    if (encryptionPublicKey.size() != EncryptionPublicKeySize) {
        return false;
    }

    QSettings settings;

    settings.beginGroup(QStringLiteral("trustedDevices"));
    settings.setValue(token, device);
    settings.endGroup();

    settings.beginGroup(
        QStringLiteral("trustedDeviceSigningKeys"));

    if (signingPublicKey.isEmpty()) {
        settings.remove(token);
    } else {
        settings.setValue(
            token,
            toHexString(signingPublicKey));
    }

    settings.endGroup();

    settings.beginGroup(
        QStringLiteral("trustedDeviceEncryptionKeys"));

    settings.setValue(
        token,
        toHexString(encryptionPublicKey));

    settings.endGroup();

    settings.sync();

    return settings.status() == QSettings::NoError;
}

PairingManager::CreateResult PairingManager::createRequest(
    const QString &presentedToken,
    const QString &device,
    int lifetimeSeconds,
    QString *requestId,
    const QByteArray &signingPublicKey,
    const QByteArray &encryptionPublicKey)
{
    if (presentedToken.isEmpty()
        || presentedToken != m_pairingToken) {
        return CreateResult::InvalidToken;
    }

    if (hasPendingPairing()) {
        return CreateResult::AlreadyPending;
    }

    const QString normalizedDevice = device.trimmed();

if (normalizedDevice.isEmpty() || normalizedDevice.size() > 128) {
    return CreateResult::InvalidToken;
}

    if (!signingPublicKey.isEmpty()
        && signingPublicKey.size() != Ed25519PublicKeySize) {
        return CreateResult::InvalidToken;
    }

    if (encryptionPublicKey.size() != EncryptionPublicKeySize) {
        return CreateResult::InvalidToken;
    }

    if (lifetimeSeconds <= 0) {
        return CreateResult::InvalidToken;
    }

    PairingRequest request;
    request.id = newToken();
    request.device = normalizedDevice;
    request.sessionToken = newToken();
    request.state = QStringLiteral("pending");
    request.expiresAt = QDateTime::currentDateTimeUtc()
        .addSecs(lifetimeSeconds);
    request.signingPublicKey = signingPublicKey;
    request.encryptionPublicKey = encryptionPublicKey;

    m_activePairingId = request.id;
    m_pairings.insert(request.id, request);

    if (requestId) {
        *requestId = request.id;
    }

    return CreateResult::Created;
}


bool PairingManager::approvePairing(const QString &deviceName)
{
    auto it = m_pairings.find(m_activePairingId);

    if (it == m_pairings.end()
        || it->state != QStringLiteral("pending")
        || it->expiresAt <= QDateTime::currentDateTimeUtc()) {
        return false;
    }

    const QString normalizedName = deviceName.trimmed();

    if (normalizedName.isEmpty() || normalizedName.size() > 128) {
        return false;
    }

    // Persist the custom device name and public keys before authorizing it.
    if (!saveTrustedDevice(
            it->sessionToken,
            normalizedName,
            it->signingPublicKey,
            it->encryptionPublicKey)) {
        return false;
    }

    m_trustedDevices.insert(
        it->sessionToken,
        normalizedName);

    if (it->signingPublicKey.isEmpty()) {
        m_trustedSigningKeys.remove(it->sessionToken);
    } else {
        m_trustedSigningKeys.insert(
            it->sessionToken,
            it->signingPublicKey);
    }

    m_trustedEncryptionKeys.insert(
        it->sessionToken,
        it->encryptionPublicKey);

    m_sessions.insert(
        it->sessionToken,
        normalizedName);

    it->state = QStringLiteral("approved");

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
    return true;
}

bool PairingManager::pairingStatus(
    const QString &id,
    QString *state,
    QString *sessionToken) const
{
    const auto it = m_pairings.constFind(id);

    if (it == m_pairings.cend()) {
        return false;
    }

    if (it->state == QStringLiteral("pending")
        && it->expiresAt <= QDateTime::currentDateTimeUtc()) {
        if (state) {
            *state = QStringLiteral("expired");
        }

        if (sessionToken) {
            sessionToken->clear();
        }

        return true;
    }

    if (state) {
        *state = it->state;
    }

    if (sessionToken) {
        if (it->state == QStringLiteral("approved")) {
            *sessionToken = it->sessionToken;
        } else {
            sessionToken->clear();
        }
    }

    return true;
}

bool PairingManager::expireRequests(const QDateTime &now)
{
    bool expiredAny = false;

    for (auto it = m_pairings.begin();
         it != m_pairings.end();
         ++it) {
        if (it->state == QStringLiteral("pending")
            && it->expiresAt <= now) {
            it->state = QStringLiteral("expired");
            expiredAny = true;
        }
    }

    return expiredAny;
}

bool PairingManager::isAuthorized(const QString &token) const
{
    return !token.isEmpty()
        && m_trustedDevices.contains(token);
}

QString PairingManager::sessionDevice(const QString &token) const
{
    return m_trustedDevices.value(token);
}

QByteArray PairingManager::trustedDeviceSigningKey(
    const QString &token) const
{
    return m_trustedSigningKeys.value(token);
}

QByteArray PairingManager::trustedDeviceEncryptionKey(
    const QString &token) const
{
    return m_trustedEncryptionKeys.value(token);
}

QVariantList PairingManager::trustedDeviceList() const
{
    QVariantList devices;

    for (auto it = m_trustedDevices.cbegin();
         it != m_trustedDevices.cend();
         ++it) {
        QVariantMap item;

        item.insert(QStringLiteral("token"), it.key());
        item.insert(QStringLiteral("name"), it.value());

        devices.append(item);
    }

    return devices;
}
bool PairingManager::removeTrustedDevice(const QString &token)
{
    if (!m_trustedDevices.contains(token)) {
        return false;
    }

    QSettings settings;

    settings.beginGroup(QStringLiteral("trustedDevices"));
    settings.remove(token);
    settings.endGroup();

    settings.beginGroup(
        QStringLiteral("trustedDeviceSigningKeys"));
    settings.remove(token);
    settings.endGroup();

    settings.beginGroup(
        QStringLiteral("trustedDeviceEncryptionKeys"));
    settings.remove(token);
    settings.endGroup();

   settings.sync();

const bool saved = settings.status() == QSettings::NoError;

// Revoke the device in memory regardless of persistence success.
m_trustedDevices.remove(token);
m_trustedSigningKeys.remove(token);
m_trustedEncryptionKeys.remove(token);
m_sessions.remove(token);

return saved;
}
