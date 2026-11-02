
#include "RequestRouter.h"

#include "ReceiverServer.h"
#include "WebPages.h"
#include "TransferUtils.h"
#include "HttpUtils.h"
#include "PairingManager.h"
#include "CryptoManager.h"
#include <utility>

#include <QResource>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QTcpSocket>
#include <QDateTime>
#include <QStringList>

#include <sodium.h>

namespace {

constexpr qint64 RequestTimestampWindowSeconds = 300;
constexpr qsizetype MaxReplayIdsPerSession = 1000;
constexpr qint64 ReplayIdLifetimeSeconds = 600;
constexpr qint64 RequestLifetimeSeconds = 120;
constexpr qint64 MaxUploadSize = 100LL * 1024 * 1024;
constexpr qint64 MaxBatchSize = 100LL * 1024 * 1024;
constexpr int MaxBatchFiles = 20;

bool readUniqueHeader(
    const QList<QByteArray> &headerLines,
    const QByteArray &headerName,
    QByteArray *value)
{
    if (!value)
        return false;

    bool found = false;
    const QByteArray wanted = headerName.toLower();

    for (const QByteArray &line : headerLines) {
        const qsizetype colon = line.indexOf(':');

        if (colon < 0)
            continue;

        const QByteArray name =
            line.left(colon).trimmed().toLower();

        if (name != wanted)
            continue;

        // Reject duplicate security-sensitive headers.
        if (found)
            return false;

        *value = line.mid(colon + 1).trimmed();
        found = true;
    }

    return found;
}

} // namespace

bool RequestRouter::verifySignedRequest(
    ReceiverServer &server,
    const HttpRequestParser::Request &request,
    const QString &sessionToken,
    QString *failure)
{
    auto fail = [failure](const QString &reason) {
        if (failure)
            *failure = reason;
        return false;
    };

    QByteArray timestampHeader;
    QByteArray requestIdHeader;
    QByteArray signatureHeader;

    if (!readUniqueHeader(
            request.headerLines,
            "X-CachyDrop-Timestamp",
            &timestampHeader)) {
        return fail(QStringLiteral("Missing or duplicate timestamp"));
    }

    if (!readUniqueHeader(
            request.headerLines,
            "X-CachyDrop-Request-Id",
            &requestIdHeader)) {
        return fail(QStringLiteral("Missing or duplicate request ID"));
    }

    if (!readUniqueHeader(
            request.headerLines,
            "X-CachyDrop-Signature",
            &signatureHeader)) {
        return fail(QStringLiteral("Missing or duplicate signature"));
    }

    bool timestampOk = false;
    const qint64 timestamp =
        timestampHeader.toLongLong(&timestampOk);

    if (!timestampOk)
        return fail(QStringLiteral("Invalid timestamp"));

    const qint64 now =
        QDateTime::currentSecsSinceEpoch();

    if (timestamp < now - RequestTimestampWindowSeconds ||
        timestamp > now + RequestTimestampWindowSeconds) {
        return fail(QStringLiteral("Timestamp outside allowed window"));
    }

    // Require a 16-byte request ID encoded as exactly 32 hex characters.
    if (requestIdHeader.size() != 32)
        return fail(QStringLiteral("Invalid request ID length"));

    const QByteArray requestId =
        QByteArray::fromHex(requestIdHeader);

    if (requestId.size() != 16 ||
        requestId.toHex() != requestIdHeader.toLower()) {
        return fail(QStringLiteral("Invalid request ID encoding"));
    }

    // Require an Ed25519 detached signature encoded as hex.
    const QByteArray signature =
        QByteArray::fromHex(signatureHeader);

    if (signatureHeader.size() != crypto_sign_BYTES * 2 ||
        signature.size() != crypto_sign_BYTES ||
        signature.toHex() != signatureHeader.toLower()) {
        return fail(QStringLiteral("Invalid signature encoding"));
    }

    const QByteArray publicKey =
        server.m_pairingManager.trustedDeviceSigningKey(sessionToken);

    if (publicKey.size() != crypto_sign_PUBLICKEYBYTES) {
        return fail(QStringLiteral("No trusted signing key for session"));
    }

    // Hash the exact HTTP request body received by the router.
    QByteArray bodyHash(crypto_generichash_BYTES, '\0');

    if (crypto_generichash(
            reinterpret_cast<unsigned char *>(bodyHash.data()),
            static_cast<size_t>(bodyHash.size()),
            reinterpret_cast<const unsigned char *>(request.body.constData()),
            static_cast<unsigned long long>(request.body.size()),
            nullptr,
            0) != 0) {
        return fail(QStringLiteral("Could not hash request body"));
    }

    const QByteArray bodyHashHex = bodyHash.toHex();

    // This canonical format must match the browser's signedFetch() exactly.
    const QByteArray canonical =
        QByteArrayLiteral("CachyDrop-Request-v1\n") +
        request.method + '\n' +
        request.rawTarget + '\n' +
        timestampHeader + '\n' +
        requestIdHeader + '\n' +
        bodyHashHex;

    if (!CryptoManager::verifySignature(
            canonical,
            signature,
            publicKey)) {
        return fail(QStringLiteral("Signature verification failed"));
    }

    // Reject request IDs that have already been used in this session.
    auto &replayState = server.m_replayProtection[sessionToken];

    if (!replayState.lastCleanup.isValid() ||
        replayState.lastCleanup.secsTo(
            QDateTime::currentDateTimeUtc()) >= 60) {
        const qint64 cutoff =
            QDateTime::currentSecsSinceEpoch()
            - ReplayIdLifetimeSeconds;

        for (auto it = replayState.requestIds.begin();
             it != replayState.requestIds.end();) {
            if (it.value() < cutoff)
                it = replayState.requestIds.erase(it);
            else
                ++it;
        }

        replayState.lastCleanup =
            QDateTime::currentDateTimeUtc();
    }

    const QString requestIdKey =
        QString::fromLatin1(requestIdHeader.toLower());

    if (replayState.requestIds.contains(requestIdKey))
        return fail(QStringLiteral("Request ID has already been used"));

    // Keep memory use bounded per paired session.
    if (replayState.requestIds.size() >= MaxReplayIdsPerSession)
        return fail(QStringLiteral("Replay protection limit reached"));

    replayState.requestIds.insert(requestIdKey, now);

    if (failure)
        failure->clear();

    return true;
}

void RequestRouter::handle(
    ReceiverServer &server,
    QTcpSocket *socket,
    const HttpRequestParser::Request &httpRequest)
{
    if (!socket)
        return;

    const QByteArray body = httpRequest.body;
    const QList<QByteArray> lines = httpRequest.headerLines;
    const QByteArray method = httpRequest.method;
    const QString path = httpRequest.url.path();
    const QUrlQuery query = httpRequest.query;

    auto sendJson = [socket](
                        const QByteArray &status,
                        const QJsonObject &object) {
        socket->write(HttpUtils::jsonResponse(status, object));
    };

    auto readJson = [&body]() {
        return QJsonDocument::fromJson(body);
    };

    auto authorized = [&server](const QString &token) {
        return server.m_pairingManager.isAuthorized(token);
    };

    if (method == "GET" &&
        (path == "/" || path == "/index.html")) {
        socket->write(HttpUtils::response(
            "200 OK",
            "text/html; charset=utf-8",
            WebPages::htmlPage()));

    } else if (method == "GET" &&
               path == "/libsodium-bundle.mjs") {
        const QResource resource(
            QStringLiteral(":/libsodium-bundle.mjs"));

        if (!resource.isValid()) {
            socket->write(HttpUtils::response(
                "404 Not Found",
                "text/plain; charset=utf-8",
                "Cryptography library not found"));
        } else {
            socket->write(HttpUtils::response(
                "200 OK",
                "text/javascript; charset=utf-8",
                QByteArray(
                    reinterpret_cast<const char *>(resource.data()),
                    static_cast<qsizetype>(resource.size()))));
        }

    
    } else if (method == "GET" && path == "/health") {
        sendJson("200 OK", {
            {"status", "ok"},
            {"app", "CachyDrop"}
        });

    } else if (method == "GET" && path == "/session") {
        QByteArray auth;

const bool validAuthHeader =
    readUniqueHeader(lines, "Authorization", &auth);

        if (!validAuthHeader) {
    sendJson("400 Bad Request", {
        {"error", "Missing or duplicate Authorization header"}
    });
} else {
   const QByteArray prefix("Bearer ");

if (!auth.startsWith(prefix) ||
    auth.size() == prefix.size()) {
    sendJson("401 Unauthorized", {
        {"error", "Invalid Authorization header"}
    });
} else {
    const QString token =
        QString::fromLatin1(auth.mid(prefix.size()));

    const bool isAuthorized = authorized(token);

    sendJson(isAuthorized
             ? "200 OK"
             : "401 Unauthorized",
         {
             {"valid", isAuthorized}
         });
}
}

    } else if (method == "GET" && (path == "/outgoing" || path == "/outgoing-page")) {
        socket->write(HttpUtils::response(
            "200 OK",
            "text/html; charset=utf-8",
            WebPages::outgoingHtmlPage()));

    } else if (method == "POST" && path == "/pair") {
        const QJsonDocument doc = readJson();

        if (!doc.isObject()) {
            sendJson("400 Bad Request", {
                {"error", "Invalid request"}
            });
        } else {
            const QJsonObject obj = doc.object();

            const QString token = obj.value("token").toString();    
            const QString device = obj.value("device").toString();

            const QString publicKeyHex = obj.value("publicKey").toString();

            const QString signingPublicKeyHex = obj.value("signingPublicKey").toString();

            const QByteArray signingPublicKey = QByteArray::fromHex(signingPublicKeyHex.toLatin1());

            if (signingPublicKeyHex.size() != crypto_sign_PUBLICKEYBYTES * 2 ||
                signingPublicKey.size() != crypto_sign_PUBLICKEYBYTES ||
                signingPublicKey.toHex() != signingPublicKeyHex.toLower().toLatin1()) {
                sendJson("400 Bad Request", {
                    {"error", "Invalid Ed25519 signing public key"}
                });
                socket->disconnectFromHost();
                return;
            }

            const QByteArray peerPublicKey =
                QByteArray::fromHex(publicKeyHex.toLatin1());

            if (publicKeyHex.size() != 64 ||
                peerPublicKey.size() != 32 ||
                QString::fromLatin1(peerPublicKey.toHex()) !=
                    publicKeyHex.toLower()) {
                sendJson("400 Bad Request", {
                    {"error", "Invalid browser public key"}
                });
                socket->disconnectFromHost();
                return;
            }

            QString requestId;
          
            const auto result =
                server.m_pairingManager.createRequest(
                token,
                device,
                RequestLifetimeSeconds,
                &requestId,
                signingPublicKey,
                peerPublicKey
    );

            switch (result) {
            case PairingManager::CreateResult::InvalidToken:
                sendJson("403 Forbidden", {
                    {"error", "Invalid QR pairing token"}
                });
                break;

            case PairingManager::CreateResult::AlreadyPending:
                sendJson("409 Conflict", {
                    {"error", "Another pairing request is pending"}
                });
                break;

            case PairingManager::CreateResult::Created:
                emit server.pairingRequestChanged();

                sendJson("200 OK", {
                    {"id", requestId}
                });
                break;
            }
        }

    } else if (method == "GET" && path == "/pair-status") {
        const QString id = query.queryItemValue("id");

        if (id.size() != 64 ||
            !QByteArray::fromHex(id.toLatin1()).size() ||
            QByteArray::fromHex(id.toLatin1()).toHex() !=
            id.toLatin1().toLower()) {
            sendJson("400 Bad Request", {
                {"state", "invalid"},
                {"message", "Invalid pairing request ID"}
        });
            return;
}

        QString state;
        QString sessionToken;

        if (!server.m_pairingManager.pairingStatus(
                id, &state, &sessionToken)) {
            sendJson("404 Not Found", {
                {"state", "expired"},
                {"message", "Pairing request expired"}
            });

        } else if (state == "approved") {
            sendJson("200 OK", {
                {"state", "approved"},
                {"session", sessionToken}
            });

        } else {
            sendJson("200 OK", {
                {"state", state},
                {"message", state == "rejected"
                    ? "Pairing rejected on computer"
                    : ""}
            });
        }

    } else if (method == "POST" && path == "/outgoing") {
        const QJsonDocument doc = readJson();

        if (!doc.isObject()) {
            sendJson("400 Bad Request", {
                {"error", "Invalid request"}
            });
        } else {
            const QString token =
                doc.object().value("session").toString();

            if (!authorized(token)) {
                sendJson("403 Forbidden", {
                    {"error", "Device is not paired"}
                });
            } else {
                QJsonArray files;

                for (const ReceiverServer::SharedFile &file
                     : server.m_sharedFiles) {
                    const QFileInfo info(file.path);

                    if (!info.exists() ||
                        !info.isFile() ||
                        info.size() != file.size) {
                        continue;
                    }

                    files.append(QJsonObject{
                        {"id", file.id},
                        {"filename", file.filename},
                        {"size", file.size}
                    });
                }

                sendJson("200 OK", {
                    {"files", files}
                });
            }
        }

    } else if (method == "GET" && path == "/download") {
        QByteArray auth;

if (!readUniqueHeader(lines, "Authorization", &auth)) {
    sendJson("400 Bad Request", {
        {"error", "Missing or duplicate Authorization header"}
    });
    socket->disconnectFromHost();
    return;
}

const QByteArray prefix("Bearer ");

if (!auth.startsWith(prefix) ||
    auth.size() == prefix.size()) {
    sendJson("401 Unauthorized", {
        {"error", "Invalid Authorization header"}
    });
    socket->disconnectFromHost();
    return;
}

const QString token =
    QString::fromLatin1(auth.mid(prefix.size()));
        const QString id = query.queryItemValue("id");
        const auto it = server.m_sharedFiles.constFind(id);

        if (!authorized(token)) {
            socket->write(HttpUtils::response(
                "403 Forbidden",
                "text/plain",
                "Device is not paired"));

        } else if (it == server.m_sharedFiles.cend()) {
            socket->write(HttpUtils::response(
                "404 Not Found",
                "text/plain",
                "File not found or no longer shared"));

        } else {
            const QFileInfo info(it->path);

            if (!info.exists() ||
                !info.isFile() ||
                info.size() != it->size ||
                info.size() > MaxUploadSize) {
                socket->write(HttpUtils::response(
                    "404 Not Found",
                    "text/plain",
                    "File is no longer available"));

            } else {
                QFile file(it->path);

                if (!file.open(QIODevice::ReadOnly)) {
                    socket->write(HttpUtils::response(
                        "500 Internal Server Error",
                        "text/plain",
                        "Could not open file"));

                } else {
                   
QByteArray data = file.readAll();
file.close();

if (data.size() != it->size) {
    sodium_memzero(
        data.data(),
        static_cast<size_t>(data.size())
    );

    socket->write(HttpUtils::response(
        "500 Internal Server Error",
        "text/plain",
        "Could not read the complete file"
    ));
} else {
    QString cryptoError;
    QByteArray sharedSecret;
    QByteArray transferKey;
    QByteArray encryptedData;

    const QByteArray browserPublicKey =
        server.m_pairingManager.trustedDeviceEncryptionKey(token);

    bool valid =
        browserPublicKey.size() == crypto_box_PUBLICKEYBYTES;

    if (valid) {
        sharedSecret =
            server.m_cryptoManager->deriveIdentitySharedSecret(
                browserPublicKey,
                &cryptoError
            );

        valid =
            sharedSecret.size() == crypto_box_BEFORENMBYTES;
    }

    if (valid) {
        const QByteArray context =
            QByteArrayLiteral("download-v1:") + id.toUtf8();

        transferKey = CryptoManager::deriveTransferKey(
            sharedSecret,
            context,
            &cryptoError
        );

        valid =
            transferKey.size() ==
            crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
    }

    if (valid) {
        const QByteArray associatedData =
            QByteArrayLiteral("CachyDrop-File-v1\ndownload\n") +
            id.toUtf8() + '\n' +
            QByteArrayLiteral("0\n") +
            it->filename.toUtf8() + '\n' +
            QByteArray::number(it->size);

        encryptedData = CryptoManager::encrypt(
            transferKey,
            data,
            associatedData,
            &cryptoError
        );

        valid =
            cryptoError.isEmpty() &&
            encryptedData.size() == data.size() + 40;
    }

    sodium_memzero(
        sharedSecret.data(),
        static_cast<size_t>(sharedSecret.size())
    );

    sodium_memzero(
        transferKey.data(),
        static_cast<size_t>(transferKey.size())
    );

    sodium_memzero(
        data.data(),
        static_cast<size_t>(data.size())
    );

    if (!valid) {
        sodium_memzero(
            encryptedData.data(),
            static_cast<size_t>(encryptedData.size())
        );

        socket->write(HttpUtils::response(
            "500 Internal Server Error",
            "text/plain",
            "Could not encrypt file for download"
        ));
    } else {
        const QByteArray encodedName =
            QUrl::toPercentEncoding(it->filename);

        QByteArray reply =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Length: " +
            QByteArray::number(encryptedData.size()) +
            "\r\n"
            "Content-Disposition: attachment; "
            "filename*=UTF-8''" +
            encodedName +
            "\r\n"
            "Cache-Control: no-store\r\n"
            "Connection: close\r\n"
            "X-Content-Type-Options: nosniff\r\n\r\n";

        reply += encryptedData;

        sodium_memzero(
            encryptedData.data(),
            static_cast<size_t>(encryptedData.size())
        );

        socket->write(reply);

                 sodium_memzero(
            reply.data(),
            static_cast<size_t>(reply.size())
        );
   } // closes encryption success/failure
   } // closes data-size check
   } // closes file-open check
   } // closes file validation
   } // closes authorized/file lookup chain
   } else if (method == "POST" && path == "/request") {
        const QJsonDocument doc = readJson();

        if (!doc.isObject()) {
            sendJson("400 Bad Request", {
                {"error", "Invalid request"}
            });
        } else {
            const QJsonObject obj = doc.object();
            const QString token = obj.value("session").toString();
            const QJsonArray filesArray = obj.value("files").toArray();

            if (!authorized(token)) {
                sendJson("403 Forbidden", {
                    {"error", "Device is not paired"}
                });

            } else {
                // Verify the signature before validating or creating a
                // transfer request. The session must have a trusted key.
                QString signatureFailure;

                if (!verifySignedRequest(
                        server,
                        httpRequest,
                        token,
                        &signatureFailure)) {
                    sendJson("401 Unauthorized", {
                        {"error", "Request signature verification failed"}
                    });

                } else if (filesArray.isEmpty() ||
                           filesArray.size() > MaxBatchFiles) {
                    sendJson("400 Bad Request", {
                        {"error", "Select between 1 and 20 files"}
                    });

                } else if (
                    server.hasPendingRequest() ||
                    (server.m_requests.contains(
                         server.m_activeRequestId) &&
                     server.m_requests.value(
                         server.m_activeRequestId).state == "approved")) {
                    sendJson("409 Conflict", {
                        {"error", "Another transfer is in progress"}
                    });

                } else {
                    ReceiverServer::TransferRequest transferRequest;

                    transferRequest.id =
                        QUuid::createUuid().toString(
                            QUuid::WithoutBraces);

                    transferRequest.sender =
                        server.m_pairingManager.sessionDevice(token);

                    transferRequest.sessionToken = token;
                    transferRequest.totalSize = 0;
                    transferRequest.state = "pending";

                    transferRequest.expiresAt =
                        QDateTime::currentDateTimeUtc().addSecs(
                            RequestLifetimeSeconds);

                    bool valid = true;

                    for (const QJsonValue &value : filesArray) {
                        if (!value.isObject()) {
                            valid = false;
                            break;
                        }

                        const QJsonObject fileObj = value.toObject();

                        const QString filename =
                            TransferUtils::safeFilename(
                                fileObj.value("filename").toString());

                        const QJsonValue sizeValue =
                            fileObj.value("size");

                        if (filename.isEmpty() ||
                            !sizeValue.isDouble()) {
                            valid = false;
                            break;
                        }

                        const double rawSize =
                            sizeValue.toDouble(-1);

                        if (rawSize < 0 ||
                            rawSize > MaxBatchSize) {
                            valid = false;
                            break;
                        }

                        const qint64 size =
                            static_cast<qint64>(rawSize);

                        if (static_cast<double>(size) != rawSize ||
                            size > MaxBatchSize -
                                transferRequest.totalSize) {
                            valid = false;
                            break;
                        }

                        ReceiverServer::TransferFile transferFile;
                        transferFile.filename = filename;
                        transferFile.size = size;

                        transferRequest.files.append(transferFile);
                        transferRequest.totalSize += size;
                    }

                    if (!valid) {
                        sendJson("400 Bad Request", {
                            {"error",
                             "Invalid file list or batch exceeds 100 MiB"}
                        });
                    } else {
                       server.m_requests.insert(
    transferRequest.id,
    transferRequest);

server.m_activeRequestId = transferRequest.id;

emit server.pendingRequestChanged();

QJsonArray approvedFiles;

for (const auto &file : transferRequest.files) {
    QJsonObject approvedFile;
    approvedFile.insert("filename", file.filename);
    approvedFile.insert(
        "size",
        static_cast<double>(file.size));

    approvedFiles.append(approvedFile);
}

QJsonObject response;
response.insert("id", transferRequest.id);
response.insert("files", approvedFiles);

sendJson("200 OK", response);
                    }
                }
            }
        }

    } else if (method == "GET" && path == "/status") {
        const QString id = query.queryItemValue("id");
        const QString token = query.queryItemValue("session");

        const auto it = server.m_requests.constFind(id);

        if (!authorized(token) ||
            it == server.m_requests.cend() ||
            it->sessionToken != token) {
            sendJson("404 Not Found", {
                {"state", "expired"},
                {"message", "Request expired"}
            });
        } else {
            sendJson("200 OK", {
                {"state", it->state},
                {"message", it->state == "rejected"
                    ? "Transfer rejected on computer"
                    : ""}
            });
        }

    
    } else if (method == "POST" && path == "/upload") {
        const QString id = query.queryItemValue("id");
        const QString token = query.queryItemValue("session");

        auto it = server.m_requests.find(id);

        if (!authorized(token) ||
            it == server.m_requests.end() ||
            it->sessionToken != token ||
            it->state != "approved" ||
            it->expiresAt <= QDateTime::currentDateTimeUtc()) {
            sendJson("403 Forbidden", {
                {"error", "Transfer was not approved"}
            });

        } else {
            QString signatureFailure;

            if (!verifySignedRequest(
                    server,
                    httpRequest,
                    token,
                    &signatureFailure)) {
                sendJson("401 Unauthorized", {
                    {"error", "Upload signature verification failed"}
                });

            } else {
                QByteArray contentType;
if (!readUniqueHeader(lines, "Content-Type", &contentType)) {
    sendJson("400 Bad Request", {
        {"error", "Missing or duplicate Content-Type"}
    });
    socket->disconnectFromHost();
    return;
}

QVector<TransferUtils::IncomingFile> incoming;
                QString parseError;

                if (!TransferUtils::parseMultipart(
                        contentType,
                        body,
                        incoming,
                        &parseError)) {
                    sendJson("400 Bad Request", {
                        {"error", "Expected multipart upload"}
                    });

                } else if (incoming.size() != it->files.size()) {
                    sendJson("400 Bad Request", {
                        {"error",
                         "Uploaded file list does not match approval"}
                    });

                
} else {
    bool valid = true;
    qint64 total = 0;
    QString cryptoError;

    const QByteArray browserPublicKey =
        server.m_pairingManager.trustedDeviceEncryptionKey(token);

    if (browserPublicKey.size() != crypto_box_PUBLICKEYBYTES) {
        valid = false;
    }

    QByteArray sharedSecret;
    QByteArray transferKey;

    if (valid) {
        sharedSecret =
            server.m_cryptoManager->deriveIdentitySharedSecret(
                browserPublicKey,
                &cryptoError);

        valid = sharedSecret.size() == crypto_box_BEFORENMBYTES;
    }

    if (valid) {
        const QByteArray context =
            QByteArrayLiteral("upload-v1:") + id.toUtf8();

        transferKey = CryptoManager::deriveTransferKey(
            sharedSecret,
            context,
            &cryptoError);

        valid = transferKey.size() == crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
    }

    if (valid) {
        for (int i = 0; i < incoming.size(); ++i) {
            auto &file = incoming[i];
            const auto &approvedFile = it->files[i];

            // Ciphertext contains a 24-byte nonce and a 16-byte tag.
            const qint64 expectedEncryptedSize = approvedFile.size + 40;

            if (file.filename != approvedFile.filename ||
                file.data.size() != expectedEncryptedSize) {
                valid = false;
                break;
            }

            const QByteArray associatedData =
                QByteArrayLiteral("CachyDrop-File-v1\nupload\n") +
                id.toUtf8() + '\n' +
                QByteArray::number(i) + '\n' +
                approvedFile.filename.toUtf8() + '\n' +
                QByteArray::number(approvedFile.size);

            cryptoError.clear();

QByteArray plaintext = CryptoManager::decrypt(
    transferKey,
    file.data,
    associatedData,
    &cryptoError
);

if (!cryptoError.isEmpty() ||
    plaintext.size() != approvedFile.size) {
    sodium_memzero(
        plaintext.data(),
        static_cast<size_t>(plaintext.size())
    );
    valid = false;
    break;
}

            sodium_memzero(
    file.data.data(),
    static_cast<size_t>(file.data.size())
);

file.data = std::move(plaintext);

            total += file.data.size();
        }
    }

    sodium_memzero(sharedSecret.data(),
                   static_cast<size_t>(sharedSecret.size()));
    sodium_memzero(transferKey.data(),
                   static_cast<size_t>(transferKey.size()));


                    if (!valid || total != it->totalSize) {
                        sendJson("400 Bad Request", {
                            {"error",
                             "File details did not match approval"}
                        });

                    } else {
                        const QString folder =
                            QDir::homePath() +
                            QStringLiteral("/Downloads/CachyDrop");

                        QStringList written;
                        QString failedName;

                        if (!TransferUtils::saveBatch(
                                folder,
                                incoming,
                                &written,
                                &failedName)) {
                            sendJson("500 Internal Server Error", {
                                {"error",
                                 "Could not save all files; batch rolled back"},
                                {"filename", failedName}
                            });

                        } else {
                            it->state = "uploaded";
                            it->expiresAt =
                                QDateTime::currentDateTimeUtc().addSecs(30);

                            QStringList names;

                            for (const auto &file : incoming)
                                names.append(file.filename);

                            socket->write(HttpUtils::response(
                                "200 OK",
                                "text/html; charset=utf-8",
                                "<!doctype html><meta charset=utf-8>"
                                "<h1>Files received!</h1><p>" +
                                names.join(", ").toHtmlEscaped().toUtf8() +
                                "</p><a href=\"/\">Send more files</a>"
                            ));
                        }
                    }
                }
            }
        }


    } else {
        socket->write(HttpUtils::response(
            "404 Not Found",
            "text/plain",
            "Not found"));
    }

    socket->disconnectFromHost();
}
