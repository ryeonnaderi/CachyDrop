
#include "CryptoManager.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <limits>

#include <sodium.h>

namespace
{
constexpr auto KeyFileName = "identity.key";
constexpr auto PublicKeySize = crypto_box_PUBLICKEYBYTES;
constexpr auto PrivateKeySize = crypto_box_SECRETKEYBYTES;
constexpr auto KeyFileSize = PublicKeySize + PrivateKeySize;

constexpr auto PrivateFilePermissions =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner;
}

bool CryptoManager::initialize(QString *error)
{
    if (sodium_init() < 0) {
        if (error) {
            *error = QStringLiteral("Failed to initialize libsodium.");
        }
        return false;
    }

    return loadOrCreateKeyPair(error);
}

bool CryptoManager::loadOrCreateKeyPair(QString *error)
{
    const QString dataDirectory =
        QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);

    if (dataDirectory.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "Could not determine the application data directory.");
        }
        return false;
    }

    QDir dir;
    if (!dir.mkpath(dataDirectory)) {
        if (error) {
            *error = QStringLiteral(
                "Could not create the application data directory.");
        }
        return false;
    }

    const QString keyPath =
        dataDirectory + QLatin1Char('/') + KeyFileName;

    QFile keyFile(keyPath);

    if (keyFile.exists()) {
        if (!QFile::setPermissions(keyPath, PrivateFilePermissions)) {
            if (error) {
                *error = QStringLiteral(
                    "Could not restrict permissions on the identity key file.");
            }
            return false;
        }

        if (!keyFile.open(QIODevice::ReadOnly)) {
            if (error) {
                *error = QStringLiteral(
                    "Could not read the existing identity key file.");
            }
            return false;
        }

        QByteArray keyData = keyFile.readAll();
        keyFile.close();

        if (keyData.size() != KeyFileSize) {
            sodium_memzero(
                keyData.data(),
                static_cast<size_t>(keyData.size()));

            if (error) {
                *error = QStringLiteral(
                    "The identity key file has an invalid size.");
            }
            return false;
        }

        QByteArray publicKey = keyData.left(PublicKeySize);
        QByteArray privateKey =
            keyData.mid(PublicKeySize, PrivateKeySize);

        QByteArray derivedPublicKey(
            PublicKeySize, Qt::Uninitialized);

        const int deriveResult = crypto_scalarmult_base(
            reinterpret_cast<unsigned char *>(derivedPublicKey.data()),
            reinterpret_cast<const unsigned char *>(privateKey.constData()));

        const bool keysMatch =
            deriveResult == 0 && publicKey == derivedPublicKey;

        sodium_memzero(
            keyData.data(),
            static_cast<size_t>(keyData.size()));

        sodium_memzero(
            derivedPublicKey.data(),
            static_cast<size_t>(derivedPublicKey.size()));

        if (!keysMatch) {
            sodium_memzero(
                privateKey.data(),
                static_cast<size_t>(privateKey.size()));

            if (error) {
                *error = QStringLiteral(
                    "The identity key file contains an invalid or "
                    "mismatched key pair.");
            }
            return false;
        }

        m_publicKey = publicKey;
        m_privateKey = privateKey;

        sodium_memzero(
            privateKey.data(),
            static_cast<size_t>(privateKey.size()));

        return true;
    }

    QByteArray publicKey(PublicKeySize, Qt::Uninitialized);
    QByteArray privateKey(PrivateKeySize, Qt::Uninitialized);

    if (crypto_box_keypair(
            reinterpret_cast<unsigned char *>(publicKey.data()),
            reinterpret_cast<unsigned char *>(privateKey.data())) != 0) {
        sodium_memzero(
            privateKey.data(),
            static_cast<size_t>(privateKey.size()));

        if (error) {
            *error = QStringLiteral(
                "Failed to generate the identity key pair.");
        }
        return false;
    }

    QSaveFile saveFile(keyPath);

    if (!saveFile.open(QIODevice::WriteOnly)) {
        sodium_memzero(
            privateKey.data(),
            static_cast<size_t>(privateKey.size()));

        if (error) {
            *error = QStringLiteral(
                "Could not create the identity key file.");
        }
        return false;
    }

    if (!saveFile.setPermissions(PrivateFilePermissions)) {
        saveFile.cancelWriting();

        sodium_memzero(
            privateKey.data(),
            static_cast<size_t>(privateKey.size()));

        if (error) {
            *error = QStringLiteral(
                "Could not restrict permissions on the new identity key file.");
        }
        return false;
    }

    const bool writeSucceeded =
        saveFile.write(publicKey) == publicKey.size()
        && saveFile.write(privateKey) == privateKey.size();

    if (!writeSucceeded || !saveFile.commit()) {
        sodium_memzero(
            privateKey.data(),
            static_cast<size_t>(privateKey.size()));

        if (error) {
            *error = QStringLiteral(
                "Could not save the identity key pair.");
        }
        return false;
    }

    m_publicKey = publicKey;
    m_privateKey = privateKey;

    sodium_memzero(
        privateKey.data(),
        static_cast<size_t>(privateKey.size()));

    return true;
}

QByteArray CryptoManager::publicKey() const
{
    return m_publicKey;
}

QString CryptoManager::publicKeyHex() const
{
    return QString::fromLatin1(m_publicKey.toHex());
}

bool CryptoManager::generateEphemeralKeyPair(QString *error)
{
    if (sodium_init() < 0) {
        if (error) {
            *error = QStringLiteral("Failed to initialize libsodium.");
        }
        return false;
    }

    QByteArray publicKey(
        crypto_box_PUBLICKEYBYTES, Qt::Uninitialized);
    QByteArray privateKey(
        crypto_box_SECRETKEYBYTES, Qt::Uninitialized);

    if (crypto_box_keypair(
            reinterpret_cast<unsigned char *>(publicKey.data()),
            reinterpret_cast<unsigned char *>(privateKey.data())) != 0) {
        sodium_memzero(
            privateKey.data(),
            static_cast<size_t>(privateKey.size()));

        if (error) {
            *error = QStringLiteral(
                "Failed to generate temporary pairing keys.");
        }
        return false;
    }

    clearEphemeralKeyPair();

    m_ephemeralPublicKey = publicKey;
    m_ephemeralPrivateKey = privateKey;

    sodium_memzero(
        privateKey.data(),
        static_cast<size_t>(privateKey.size()));

    return true;
}

QByteArray CryptoManager::ephemeralPublicKey() const
{
    return m_ephemeralPublicKey;
}

QString CryptoManager::ephemeralPublicKeyHex() const
{
    return QString::fromLatin1(m_ephemeralPublicKey.toHex());
}

void CryptoManager::clearEphemeralKeyPair()
{
    if (!m_ephemeralPrivateKey.isEmpty()) {
        sodium_memzero(
            m_ephemeralPrivateKey.data(),
            static_cast<size_t>(m_ephemeralPrivateKey.size()));
    }

    m_ephemeralPrivateKey.clear();
    m_ephemeralPublicKey.clear();
}

QByteArray CryptoManager::deriveSharedSecret(
    const QByteArray &peerPublicKey,
    QString *error) const
{
    
    if (m_ephemeralPrivateKey.size() != crypto_box_SECRETKEYBYTES
        || m_ephemeralPublicKey.size() != crypto_box_PUBLICKEYBYTES) {
        if (error) {
            *error = QStringLiteral("No active ephemeral key pair.");
        }
        return {};
    }

    if (peerPublicKey.size() != crypto_box_PUBLICKEYBYTES) {
        if (error) {
            *error = QStringLiteral(
                "Peer public key has an invalid size.");
        }
        return {};
    }

    QByteArray sharedSecret(
        crypto_box_BEFORENMBYTES, Qt::Uninitialized);

    if (crypto_box_beforenm(
            reinterpret_cast<unsigned char *>(sharedSecret.data()),
            reinterpret_cast<const unsigned char *>(
                peerPublicKey.constData()),
            reinterpret_cast<const unsigned char *>(
                m_ephemeralPrivateKey.constData())) != 0) {
        sodium_memzero(
            sharedSecret.data(),
            static_cast<size_t>(sharedSecret.size()));

        if (error) {
            *error = QStringLiteral(
                "Failed to derive the shared secret.");
        }
        return {};
    }

    return sharedSecret;
}

QByteArray CryptoManager::deriveIdentitySharedSecret(
    const QByteArray &peerPublicKey,
    QString *error) const
{
    if (m_privateKey.size() != crypto_box_SECRETKEYBYTES
        || m_publicKey.size() != crypto_box_PUBLICKEYBYTES) {
        if (error) {
            *error = QStringLiteral(
                "Persistent desktop identity key pair is not initialized.");
        }
        return {};
    }

    if (peerPublicKey.size() != crypto_box_PUBLICKEYBYTES) {
        if (error) {
            *error = QStringLiteral(
                "Peer encryption public key has an invalid size.");
        }
        return {};
    }

    QByteArray sharedSecret(
        crypto_box_BEFORENMBYTES,
        Qt::Uninitialized);

    if (crypto_box_beforenm(
            reinterpret_cast<unsigned char *>(sharedSecret.data()),
            reinterpret_cast<const unsigned char *>(
                peerPublicKey.constData()),
            reinterpret_cast<const unsigned char *>(
                m_privateKey.constData())) != 0) {
        sodium_memzero(
            sharedSecret.data(),
            static_cast<size_t>(sharedSecret.size()));

        if (error) {
            *error = QStringLiteral(
                "Failed to derive the identity shared secret.");
        }
        return {};
    }

    return sharedSecret;
}

QByteArray CryptoManager::deriveTransferKey(
    const QByteArray &sharedSecret,
    const QByteArray &context,
    QString *error)
{
    if (sodium_init() < 0) {
        if (error) {
            *error = QStringLiteral(
                "Failed to initialize libsodium.");
        }
        return {};
    }

    if (sharedSecret.size() != crypto_box_BEFORENMBYTES) {
        if (error) {
            *error = QStringLiteral(
                "Shared secret has an invalid size.");
        }
        return {};
    }

       if (context.isEmpty() || context.size() > 128) {
        if (error) {
            *error = QStringLiteral(
                "Key derivation context has an invalid size.");
        }
        return {};
    }

    QByteArray key(
        crypto_generichash_KEYBYTES, Qt::Uninitialized);

    const QByteArray input =
        QByteArrayLiteral("CachyDrop transfer key v1:") + context;

    if (crypto_generichash(
            reinterpret_cast<unsigned char *>(key.data()),
            static_cast<size_t>(key.size()),
            reinterpret_cast<const unsigned char *>(input.constData()),
            static_cast<unsigned long long>(input.size()),
            reinterpret_cast<const unsigned char *>(sharedSecret.constData()),
            static_cast<size_t>(sharedSecret.size())) != 0) {
        sodium_memzero(
            key.data(),
            static_cast<size_t>(key.size()));

        if (error) {
            *error = QStringLiteral(
                "Failed to derive the transfer key.");
        }
        return {};
    }

    return key;
}


QByteArray CryptoManager::encrypt(
    const QByteArray &key,
    const QByteArray &plaintext,
    const QByteArray &associatedData,
    QString *error)
{
    if (sodium_init() < 0) {
        if (error)
            *error = QStringLiteral("Failed to initialize libsodium.");
        return {};
    }

    if (key.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        if (error)
            *error = QStringLiteral("Encryption key must be 32 bytes.");
        return {};
    }

    const qsizetype nonceSize =
        crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    const qsizetype tagSize =
        crypto_aead_xchacha20poly1305_ietf_ABYTES;

    if (plaintext.size() >
        std::numeric_limits<qsizetype>::max() - nonceSize - tagSize) {
        if (error)
            *error = QStringLiteral("Plaintext is too large.");
        return {};
    }

    QByteArray result;
    result.resize(nonceSize + plaintext.size() + tagSize);

    unsigned char *nonce =
        reinterpret_cast<unsigned char *>(result.data());

    randombytes_buf(nonce, static_cast<size_t>(nonceSize));

    unsigned long long ciphertextLength = 0;

    unsigned char *ciphertext = nonce + nonceSize;

    const int status = crypto_aead_xchacha20poly1305_ietf_encrypt(
        ciphertext,
        &ciphertextLength,
        reinterpret_cast<const unsigned char *>(plaintext.constData()),
        static_cast<unsigned long long>(plaintext.size()),
        reinterpret_cast<const unsigned char *>(associatedData.constData()),
        static_cast<unsigned long long>(associatedData.size()),
        nullptr,
        nonce,
        reinterpret_cast<const unsigned char *>(key.constData()));

    if (status != 0) {
        sodium_memzero(
            result.data(),
            static_cast<size_t>(result.size()));

        if (error)
            *error = QStringLiteral("File encryption failed.");

        return {};
    }

    result.resize(
        nonceSize + static_cast<qsizetype>(ciphertextLength));

    return result;
}

QByteArray CryptoManager::decrypt(
    const QByteArray &key,
    const QByteArray &encryptedData,
    const QByteArray &associatedData,
    QString *error)
{
    if (sodium_init() < 0) {
        if (error)
            *error = QStringLiteral("Failed to initialize libsodium.");
        return {};
    }

    if (key.size() != crypto_aead_xchacha20poly1305_ietf_KEYBYTES) {
        if (error)
            *error = QStringLiteral("Decryption key must be 32 bytes.");
        return {};
    }

    const qsizetype nonceSize =
        crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    const qsizetype tagSize =
        crypto_aead_xchacha20poly1305_ietf_ABYTES;

    if (encryptedData.size() < nonceSize + tagSize) {
        if (error)
            *error = QStringLiteral("Encrypted data is too short.");
        return {};
    }

    const unsigned char *nonce =
        reinterpret_cast<const unsigned char *>(
            encryptedData.constData());

    const unsigned char *ciphertext = nonce + nonceSize;

    const qsizetype ciphertextSize =
        encryptedData.size() - nonceSize;

    const qsizetype plaintextCapacity = ciphertextSize - tagSize;

    // Allocate at least one byte so empty plaintext can be handled.
    QByteArray plaintext(
        plaintextCapacity > 0 ? plaintextCapacity : 1, '\0');

    unsigned long long plaintextLength = 0;

    const int status = crypto_aead_xchacha20poly1305_ietf_decrypt(
        reinterpret_cast<unsigned char *>(plaintext.data()),
        &plaintextLength,
        nullptr,
        ciphertext,
        static_cast<unsigned long long>(ciphertextSize),
        reinterpret_cast<const unsigned char *>(associatedData.constData()),
        static_cast<unsigned long long>(associatedData.size()),
        nonce,
        reinterpret_cast<const unsigned char *>(key.constData()));

    if (status != 0) {
        sodium_memzero(
            plaintext.data(),
            static_cast<size_t>(plaintext.size()));
        plaintext.clear();

        if (error)
            *error = QStringLiteral(
                "Decryption failed: authentication check failed.");

        return {};
    }

    plaintext.resize(static_cast<qsizetype>(plaintextLength));
    return plaintext;
}




QByteArray CryptoManager::generateSigningKeyPair(
    QByteArray *publicKey,
    QString *error)
{
    if (publicKey == nullptr) {
        if (error) {
            *error = QStringLiteral("Public-key output is required.");
        }
        return {};
    }

    if (sodium_init() < 0) {
        if (error) {
            *error = QStringLiteral("Failed to initialize libsodium.");
        }
        return {};
    }

    QByteArray secretKey(
        crypto_sign_SECRETKEYBYTES, Qt::Uninitialized);

    publicKey->resize(crypto_sign_PUBLICKEYBYTES);

    if (crypto_sign_keypair(
            reinterpret_cast<unsigned char *>(publicKey->data()),
            reinterpret_cast<unsigned char *>(secretKey.data())) != 0) {
        sodium_memzero(
            secretKey.data(),
            static_cast<size_t>(secretKey.size()));
        publicKey->clear();

        if (error) {
            *error = QStringLiteral("Failed to generate signing keys.");
        }
        return {};
    }

    return secretKey;
}

QByteArray CryptoManager::signMessage(
    const QByteArray &message,
    const QByteArray &secretKey,
    QString *error)
{
    if (sodium_init() < 0) {
        if (error) {
            *error = QStringLiteral("Failed to initialize libsodium.");
        }
        return {};
    }

    if (secretKey.size() != crypto_sign_SECRETKEYBYTES) {
        if (error) {
            *error = QStringLiteral("Signing secret key has an invalid size.");
        }
        return {};
    }

    QByteArray signature(crypto_sign_BYTES, Qt::Uninitialized);
    unsigned long long signatureLength = 0;

    if (crypto_sign_detached(
            reinterpret_cast<unsigned char *>(signature.data()),
            &signatureLength,
            reinterpret_cast<const unsigned char *>(message.constData()),
            static_cast<unsigned long long>(message.size()),
            reinterpret_cast<const unsigned char *>(secretKey.constData())) != 0
        || signatureLength != crypto_sign_BYTES) {
        sodium_memzero(
            signature.data(),
            static_cast<size_t>(signature.size()));

        if (error) {
            *error = QStringLiteral("Failed to sign the message.");
        }
        return {};
    }

    return signature;
}

bool CryptoManager::verifySignature(
    const QByteArray &message,
    const QByteArray &signature,
    const QByteArray &publicKey)
{
    if (sodium_init() < 0
        || signature.size() != crypto_sign_BYTES
        || publicKey.size() != crypto_sign_PUBLICKEYBYTES) {
        return false;
    }

    return crypto_sign_verify_detached(
        reinterpret_cast<const unsigned char *>(signature.constData()),
        reinterpret_cast<const unsigned char *>(message.constData()),
        static_cast<unsigned long long>(message.size()),
        reinterpret_cast<const unsigned char *>(publicKey.constData())) == 0;
}
