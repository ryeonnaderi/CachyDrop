
#pragma once

#include <QByteArray>
#include <QString>

class CryptoManager
{
public:
    CryptoManager() = default;

    // Load an existing persistent key pair or generate one if missing.
    bool initialize(QString *error = nullptr);

    // Persistent desktop Curve25519 identity public key.
    QByteArray publicKey() const;
    QString publicKeyHex() const;

    // Generate a temporary Curve25519 key pair for QR pairing.
    bool generateEphemeralKeyPair(QString *error = nullptr);
    QByteArray ephemeralPublicKey() const;
    QString ephemeralPublicKeyHex() const;

    // Derive a shared secret using the temporary pairing key pair.
    QByteArray deriveSharedSecret(
        const QByteArray &peerPublicKey,
        QString *error = nullptr) const;

    // Derive a shared secret using the persistent desktop identity key pair.
    // Intended for encryption with previously paired browser devices.
    QByteArray deriveIdentitySharedSecret(
        const QByteArray &peerPublicKey,
        QString *error = nullptr) const;

    // Derive a purpose-specific 32-byte key from a shared secret.
    static QByteArray deriveTransferKey(
        const QByteArray &sharedSecret,
        const QByteArray &context,
        QString *error = nullptr);

    // Encrypt plaintext using XChaCha20-Poly1305.
    // The key must be 32 bytes.
    // Returns nonce || authenticated ciphertext.
    // associatedData is authenticated but is not encrypted.
    static QByteArray encrypt(
        const QByteArray &key,
        const QByteArray &plaintext,
        const QByteArray &associatedData = {},
        QString *error = nullptr);

    // Decrypt nonce || authenticated ciphertext.
    // Returns empty on failure; use error to distinguish failures.
    // Authentication must succeed before plaintext is returned.
    static QByteArray decrypt(
        const QByteArray &key,
        const QByteArray &encryptedData,
        const QByteArray &associatedData = {},
        QString *error = nullptr);

    // Clear temporary pairing keys from memory.
    void clearEphemeralKeyPair();

    // Ed25519 device-authentication helpers.
    static QByteArray generateSigningKeyPair(
        QByteArray *publicKey,
        QString *error = nullptr);

    static QByteArray signMessage(
        const QByteArray &message,
        const QByteArray &secretKey,
        QString *error = nullptr);

    static bool verifySignature(
        const QByteArray &message,
        const QByteArray &signature,
        const QByteArray &publicKey);

private:
    // Persistent desktop identity keys.
    QByteArray m_publicKey;
    QByteArray m_privateKey;

    // Temporary QR-pairing keys.
    QByteArray m_ephemeralPublicKey;
    QByteArray m_ephemeralPrivateKey;

    // Load the persistent key pair or create a new one.
    bool loadOrCreateKeyPair(QString *error);
};
