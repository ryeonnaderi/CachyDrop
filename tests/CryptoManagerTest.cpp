
#include "CryptoManager.h"

#include <QCoreApplication>
#include <QDebug>

#include <sodium.h>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        qCritical() << "FAIL:" << message;
    else
        qInfo() << "PASS:" << message;

    return condition;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (sodium_init() < 0) {
        qCritical() << "Could not initialize libsodium.";
        return 1;
    }

    bool success = true;

    QByteArray key(
        crypto_aead_xchacha20poly1305_ietf_KEYBYTES,
        Qt::Uninitialized);

    randombytes_buf(
        key.data(),
        static_cast<size_t>(key.size()));

    QByteArray wrongKey(
        crypto_aead_xchacha20poly1305_ietf_KEYBYTES,
        Qt::Uninitialized);

    randombytes_buf(
        wrongKey.data(),
        static_cast<size_t>(wrongKey.size()));

    const QByteArray plaintext =
        QByteArrayLiteral("CachyDrop encryption test payload");

    const QByteArray associatedData =
        QByteArrayLiteral("upload-test-v1");

    QString error;

    // Test encryption.
    const QByteArray encrypted = CryptoManager::encrypt(
        key, plaintext, associatedData, &error);

    success &= check(
        !encrypted.isEmpty() && error.isEmpty(),
        "Encryption succeeds");

    if (encrypted.isEmpty()) {
        qCritical() << error;
        sodium_memzero(key.data(), static_cast<size_t>(key.size()));
        sodium_memzero(wrongKey.data(), static_cast<size_t>(wrongKey.size()));
        return 1;
    }

    success &= check(
        encrypted.size()
            == static_cast<qsizetype>(
                crypto_aead_xchacha20poly1305_ietf_NPUBBYTES)
                + plaintext.size()
                + static_cast<qsizetype>(
                    crypto_aead_xchacha20poly1305_ietf_ABYTES),
        "Encrypted payload has the expected nonce and tag overhead");

    success &= check(
        encrypted != plaintext,
        "Encrypted data differs from plaintext");

    // Test decryption.
    error.clear();

    const QByteArray decrypted = CryptoManager::decrypt(
        key, encrypted, associatedData, &error);

    success &= check(
        decrypted == plaintext && error.isEmpty(),
        "Decryption recovers the original plaintext");

    // Repeated encryption must generate a different random nonce.
    error.clear();

    const QByteArray encryptedAgain = CryptoManager::encrypt(
        key, plaintext, associatedData, &error);

    success &= check(
        !encryptedAgain.isEmpty()
            && error.isEmpty()
            && encryptedAgain != encrypted,
        "Repeated encryption produces different encrypted output");

    if (!encryptedAgain.isEmpty()) {
        const QByteArray nonce1 = encrypted.left(
            crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);

        const QByteArray nonce2 = encryptedAgain.left(
            crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);

        success &= check(
            nonce1 != nonce2,
            "Repeated encryption produces different nonces");
    }

    // A wrong key must not decrypt the ciphertext.
    error.clear();

    const QByteArray wrongKeyResult = CryptoManager::decrypt(
        wrongKey, encrypted, associatedData, &error);

    success &= check(
        wrongKeyResult.isEmpty() && !error.isEmpty(),
        "Incorrect key is rejected");

    // Modify one byte of ciphertext while preserving the nonce.
    QByteArray tampered = encrypted;

    tampered[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES]
        ^= static_cast<char>(0x01);

    error.clear();

    const QByteArray tamperedResult = CryptoManager::decrypt(
        key, tampered, associatedData, &error);

    success &= check(
        tamperedResult.isEmpty() && !error.isEmpty(),
        "Tampered ciphertext is rejected");

    // Incorrect associated data must fail authentication.
    error.clear();

    const QByteArray wrongAssociatedData = CryptoManager::decrypt(
        key,
        encrypted,
        QByteArrayLiteral("different-context"),
        &error);

    success &= check(
        wrongAssociatedData.isEmpty() && !error.isEmpty(),
        "Incorrect associated data is rejected");

    // Truncated input must be rejected.
    error.clear();

    const QByteArray truncated = CryptoManager::decrypt(
        key,
        encrypted.left(
            crypto_aead_xchacha20poly1305_ietf_NPUBBYTES),
        associatedData,
        &error);

    success &= check(
        truncated.isEmpty() && !error.isEmpty(),
        "Truncated encrypted data is rejected");

    // Empty plaintext is valid; empty output alone is not an error.
    error.clear();

    const QByteArray encryptedEmpty = CryptoManager::encrypt(
        key, QByteArray(), associatedData, &error);

    success &= check(
        !encryptedEmpty.isEmpty() && error.isEmpty(),
        "Empty plaintext encrypts successfully");

    error.clear();

    const QByteArray decryptedEmpty = CryptoManager::decrypt(
        key, encryptedEmpty, associatedData, &error);

    success &= check(
        !encryptedEmpty.isEmpty()
            && decryptedEmpty.isEmpty()
            && error.isEmpty(),
        "Empty plaintext decrypts successfully");

    sodium_memzero(key.data(), static_cast<size_t>(key.size()));
    sodium_memzero(wrongKey.data(), static_cast<size_t>(wrongKey.size()));

    if (!success) {
        qCritical() << "Encryption tests failed.";
        return 1;
    }

    qInfo() << "All encryption tests passed.";
    return 0;
}
