
#include "RequestRouter.h"

#include "ReceiverServer.h"
#include "WebPages.h"
#include "TransferUtils.h"
#include "HttpUtils.h"
#include "PairingManager.h"

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

namespace {
constexpr qint64 MaxUploadSize = 100LL * 1024 * 1024;
constexpr qint64 MaxBatchSize = 100LL * 1024 * 1024;
constexpr int MaxBatchFiles = 20;
constexpr int RequestLifetimeSeconds = 120;
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

    } else if (method == "GET" && path == "/health") {
        sendJson("200 OK", {
            {"status", "ok"},
            {"app", "CachyDrop"}
        });

    } else if (method == "GET" && path == "/session") {
        QByteArray auth;

        for (const QByteArray &line : lines) {
            const QByteArray trimmed = line.trimmed();

            if (trimmed.toLower().startsWith("authorization:")) {
                auth = trimmed.mid(
                    trimmed.indexOf(':') + 1).trimmed();
            }
        }

        const QString prefix = QStringLiteral("Bearer ");
        const QString authString = QString::fromUtf8(auth);

        const QString token = authString.startsWith(prefix)
            ? authString.mid(prefix.size())
            : QString();

        sendJson("200 OK", {
            {"valid", authorized(token)}
        });

    } else if (method == "GET" && path == "/outgoing") {
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

            QString requestId;

            const auto result = server.m_pairingManager.createRequest(
                token,
                device,
                RequestLifetimeSeconds,
                &requestId);

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
        const QString token = query.queryItemValue("session");
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
                    const QByteArray data = file.readAll();
                    file.close();

                    if (data.size() != it->size) {
                        socket->write(HttpUtils::response(
                            "500 Internal Server Error",
                            "text/plain",
                            "Could not read the complete file"));

                    } else {
                        const QByteArray encodedName =
                            QUrl::toPercentEncoding(it->filename);

                        QByteArray reply =
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Type: application/octet-stream\r\n"
                            "Content-Length: " +
                            QByteArray::number(data.size()) +
                            "\r\n"
                            "Content-Disposition: attachment; "
                            "filename*=UTF-8''" +
                            encodedName +
                            "\r\n"
                            "Cache-Control: no-store\r\n"
                            "Connection: close\r\n"
                            "X-Content-Type-Options: nosniff\r\n\r\n";

                        reply += data;
                        socket->write(reply);
                    }
                }
            }
        }

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

            } else if (filesArray.isEmpty() ||
                       filesArray.size() > MaxBatchFiles) {
                sendJson("400 Bad Request", {
                    {"error", "Select between 1 and 20 files"}
                });

            } else if (
                server.hasPendingRequest() ||
                (server.m_requests.contains(server.m_activeRequestId) &&
                 server.m_requests.value(server.m_activeRequestId).state
                     == "approved")) {
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

                    const double rawSize = sizeValue.toDouble(-1);

                    if (rawSize < 0 || rawSize > MaxBatchSize) {
                        valid = false;
                        break;
                    }

                    const qint64 size =
                        static_cast<qint64>(rawSize);

                    if (static_cast<double>(size) != rawSize ||
                        size > MaxBatchSize - transferRequest.totalSize) {
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
                        transferRequest.id, transferRequest);
                    server.m_activeRequestId = transferRequest.id;

                    emit server.pendingRequestChanged();

                    sendJson("200 OK", {
                        {"id", transferRequest.id}
                    });
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
            QByteArray contentType;

            for (const QByteArray &line : lines) {
                const QByteArray trimmed = line.trimmed();
                const qsizetype colon = trimmed.indexOf(':');

                if (colon >= 0 &&
                    trimmed.left(colon).trimmed().toLower()
                        == "content-type") {
                    contentType =
                        trimmed.mid(colon + 1).trimmed();
                }
            }

            QVector<TransferUtils::IncomingFile> incoming;
            QString parseError;

            if (!TransferUtils::parseMultipart(
                    contentType, body, incoming, &parseError)) {
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

                for (int i = 0; i < incoming.size(); ++i) {
                    const auto &file = incoming[i];

                    if (file.filename != it->files[i].filename ||
                        file.data.size() != it->files[i].size ||
                        file.data.size() > MaxBatchSize - total) {
                        valid = false;
                        break;
                    }

                    total += file.data.size();
                }

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

    } else {
        socket->write(HttpUtils::response(
            "404 Not Found",
            "text/plain",
            "Not found"));
    }

    socket->disconnectFromHost();
}
