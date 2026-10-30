
#include "ReceiverServer.h"
#include "WebPages.h"
#include "TransferUtils.h"
#include "HttpUtils.h"
#include "PairingManager.h"
#include "Network.h"
#include "QrCodeUtils.h"
#include "HttpRequestParser.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QTcpSocket>
#include <QTcpServer>
#include <QTimer>
#include <QStringList>
#include <QDateTime>
#include <QDialog>
#include <QAbstractItemView>

namespace {

constexpr qint64 MaxUploadSize = 100LL * 1024 * 1024;
constexpr qint64 MaxBatchSize = 100LL * 1024 * 1024;
constexpr int MaxBatchFiles = 20;
constexpr int RequestLifetimeSeconds = 120;

}

ReceiverServer::ReceiverServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection,
            this, &ReceiverServer::handleConnection);

    auto *timer = new QTimer(this);
    timer->setInterval(1000);

    connect(timer, &QTimer::timeout,
            this, &ReceiverServer::expireRequests);

    timer->start();
}

bool ReceiverServer::running() const
{
    return m_server.isListening();
}

QString ReceiverServer::address() const
{
    return m_address;
}

QString ReceiverServer::pairingCode() const
{
    return m_pairingCode;
}

QStringList ReceiverServer::qrRows() const
{
    return m_qrRows;
}

bool ReceiverServer::hasPendingRequest() const
{
    return m_requests.contains(m_activeRequestId) &&
           m_requests.value(m_activeRequestId).state == "pending";
}

QString ReceiverServer::pendingFilename() const
{
    const auto it = m_requests.constFind(m_activeRequestId);

    if (it == m_requests.cend() || it->files.isEmpty())
        return {};

    return it->files.first().filename;
}

QStringList ReceiverServer::pendingFilenames() const
{
    QStringList names;

    const auto it = m_requests.constFind(m_activeRequestId);

    if (it == m_requests.cend())
        return names;

    for (const TransferFile &file : it->files)
        names.append(file.filename);

    return names;
}

int ReceiverServer::pendingFileCount() const
{
    const auto it = m_requests.constFind(m_activeRequestId);

    return it == m_requests.cend() ? 0 : it->files.size();
}

qint64 ReceiverServer::pendingSize() const
{
    return m_requests.value(m_activeRequestId).totalSize;
}

QString ReceiverServer::pendingSender() const
{
    return m_requests.value(m_activeRequestId).sender;
}

bool ReceiverServer::hasPendingPairing() const
{
    return m_pairingManager.hasPendingPairing();
}

QString ReceiverServer::pendingDevice() const
{
    return m_pairingManager.pendingDevice();
}

int ReceiverServer::sharedFileCount() const
{
    return m_sharedFiles.size();
}

QVariantList ReceiverServer::trustedDeviceList() const
{
    return m_pairingManager.trustedDeviceList();
}

bool ReceiverServer::removeTrustedDevice(const QString &token)
{
    if (!m_pairingManager.removeTrustedDevice(token))
        return false;

    emit trustedDevicesChanged();
    return true;
}

int ReceiverServer::shareFiles(const QStringList &paths)
{
    if (!running())
        return 0;

    QHash<QString, SharedFile> selected;

    for (const QString &path : paths) {
        const QFileInfo info(path);

        if (!info.exists() || !info.isFile() || !info.isReadable())
            continue;

        if (info.size() < 0 || info.size() > MaxUploadSize)
            continue;

        SharedFile file;
        file.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        file.path = info.absoluteFilePath();
        file.filename = TransferUtils::safeFilename(info.fileName());
        file.size = info.size();

        if (!file.filename.isEmpty())
            selected.insert(file.id, file);
    }

    m_sharedFiles = selected;

    emit sharedFilesChanged();

    return m_sharedFiles.size();
}

bool ReceiverServer::start()
{
    if (running())
        return true;

    if (!m_server.listen(QHostAddress::AnyIPv4, 8765))
        return false;

    m_pairingCode = QString("%1")
        .arg(QRandomGenerator::global()->bounded(1000000),
             6, 10, QChar('0'));

    const QString pairingToken = m_pairingManager.startNewSession();

    m_requests.clear();
    m_activeRequestId.clear();
    m_address.clear();
    m_qrRows.clear();

    // Get the receiver URL using the Network helper.
    m_address = Network::serverUrl(8765);

    if (!m_address.isEmpty()) {
        const QByteArray pairingUrl =
            (m_address + "/?pair=" + pairingToken).toUtf8();

        m_qrRows = QrCodeUtils::generateRows(pairingUrl);
    }

    emit runningChanged();
    emit pendingRequestChanged();
    emit pairingRequestChanged();

    return true;
}

void ReceiverServer::stop()
{
    if (!running())
        return;

    m_server.close();

    m_address.clear();
    m_pairingCode.clear();
    m_qrRows.clear();
    m_requests.clear();
    m_activeRequestId.clear();
    m_sharedFiles.clear();

    m_pairingManager.stopSession();

    emit sharedFilesChanged();
    emit runningChanged();
    emit pendingRequestChanged();
    emit pairingRequestChanged();
}

void ReceiverServer::approveRequest()
{
    auto it = m_requests.find(m_activeRequestId);

    if (it == m_requests.end() || it->state != "pending")
        return;

    it->state = "approved";

    emit pendingRequestChanged();
}

void ReceiverServer::rejectRequest()
{
    auto it = m_requests.find(m_activeRequestId);

    if (it == m_requests.end() || it->state != "pending")
        return;

    it->state = "rejected";
    it->expiresAt = QDateTime::currentDateTimeUtc().addSecs(30);

    emit pendingRequestChanged();
}

void ReceiverServer::approvePairing()
{
    if (!m_pairingManager.approvePairing())
        return;

    emit pairingRequestChanged();
    emit trustedDevicesChanged();
}

void ReceiverServer::rejectPairing()
{
    if (!m_pairingManager.rejectPairing())
        return;

    emit pairingRequestChanged();
}

void ReceiverServer::expireRequests()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool transfersChanged = false;

    // Expire pending file-transfer requests.
    for (auto it = m_requests.begin(); it != m_requests.end();) {
        if (it->expiresAt <= now) {
            if (it.key() == m_activeRequestId)
                m_activeRequestId.clear();

            it = m_requests.erase(it);
            transfersChanged = true;
        } else {
            ++it;
        }
    }

    if (transfersChanged)
        emit pendingRequestChanged();

    // Pairing request expiration is handled by PairingManager.
    if (m_pairingManager.expireRequests(now))
        emit pairingRequestChanged();
}

void ReceiverServer::openMultiFilePicker()
{
    QFileDialog dialog(nullptr, "Select files to send to your phone");

    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setLabelText(QFileDialog::Accept, "Select files");
    dialog.setViewMode(QFileDialog::Detail);

    // Allow selecting and deselecting files with ordinary clicks.
    // Ctrl and Shift should not be required for multi-selection.
    for (auto *view : dialog.findChildren<QAbstractItemView *>())
        view->setSelectionMode(QAbstractItemView::MultiSelection);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QStringList paths = dialog.selectedFiles();

    if (!paths.isEmpty())
        shareFiles(paths);
}

void ReceiverServer::handleConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();

        socket->setProperty("requestBuffer", QByteArray());
        socket->setProperty("requestProcessed", false);

        connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
            if (socket->property("requestProcessed").toBool())
                return;

            QByteArray buffer =
                socket->property("requestBuffer").toByteArray();

            buffer += socket->readAll();

            // Parse the request in the extracted HTTP parser.
            const HttpRequestParser::Result parsed =
                HttpRequestParser::parse(buffer);

            if (parsed.status ==
                HttpRequestParser::Status::NeedMoreData) {
                socket->setProperty("requestBuffer", buffer);
                return;
            }

            if (parsed.status == HttpRequestParser::Status::Error) {
                socket->setProperty("requestProcessed", true);

                socket->write(HttpUtils::response(
                    parsed.errorStatus,
                    "text/plain",
                    parsed.errorMessage));

                socket->disconnectFromHost();
                return;
            }

            socket->setProperty("requestProcessed", true);

            const QByteArray body = parsed.request.body;
            const QList<QByteArray> lines = parsed.request.headerLines;
            const QByteArray method = parsed.request.method;
            const QString path = parsed.request.url.path();
            const QUrlQuery query = parsed.request.query;

            auto sendJson = [socket](
                                const QByteArray &status,
                                const QJsonObject &object) {
                socket->write(HttpUtils::jsonResponse(status, object));
            };

            auto readJson = [&body]() {
                return QJsonDocument::fromJson(body);
            };

            auto authorized = [this](const QString &token) {
                return m_pairingManager.isAuthorized(token);
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

                    const QString token =
                        obj.value("token").toString();

                    const QString device =
                        obj.value("device").toString();

                    QString requestId;

                    const auto result = m_pairingManager.createRequest(
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
                        emit pairingRequestChanged();

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

                if (!m_pairingManager.pairingStatus(
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

                        for (const SharedFile &file : m_sharedFiles) {
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
                const QString token =
                    query.queryItemValue("session");

                const QString id = query.queryItemValue("id");
                const auto it = m_sharedFiles.constFind(id);

                if (!authorized(token)) {
                    socket->write(HttpUtils::response(
                        "403 Forbidden",
                        "text/plain",
                        "Device is not paired"));

                } else if (it == m_sharedFiles.cend()) {
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

                    const QString token =
                        obj.value("session").toString();

                    const QJsonArray filesArray =
                        obj.value("files").toArray();

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
                        hasPendingRequest() ||
                        (m_requests.contains(m_activeRequestId) &&
                         m_requests.value(m_activeRequestId).state ==
                             "approved")) {
                        sendJson("409 Conflict", {
                            {"error", "Another transfer is in progress"}
                        });

                    } else {
                        TransferRequest request;

                        request.id =
                            QUuid::createUuid().toString(
                                QUuid::WithoutBraces);

                        request.sender =
                            m_pairingManager.sessionDevice(token);

                        request.sessionToken = token;
                        request.totalSize = 0;
                        request.state = "pending";

                        request.expiresAt =
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
                                size > MaxBatchSize - request.totalSize) {
                                valid = false;
                                break;
                            }

                            TransferFile file;
                            file.filename = filename;
                            file.size = size;

                            request.files.append(file);
                            request.totalSize += size;
                        }

                        if (!valid) {
                            sendJson("400 Bad Request", {
                                {"error",
                                 "Invalid file list or batch exceeds 100 MiB"}
                            });
                        } else {
                            m_requests.insert(request.id, request);
                            m_activeRequestId = request.id;

                            emit pendingRequestChanged();

                            sendJson("200 OK", {
                                {"id", request.id}
                            });
                        }
                    }
                }

            } else if (method == "GET" && path == "/status") {
                const QString id = query.queryItemValue("id");
                const QString token = query.queryItemValue("session");

                const auto it = m_requests.constFind(id);

                if (!authorized(token) ||
                    it == m_requests.cend() ||
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

                auto it = m_requests.find(id);

                if (!authorized(token) ||
                    it == m_requests.end() ||
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
                            trimmed.left(colon).trimmed().toLower() ==
                                "content-type") {
                            contentType =
                                trimmed.mid(colon + 1).trimmed();
                        }
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
                                    QDateTime::currentDateTimeUtc()
                                        .addSecs(30);

                                QStringList names;

                                for (const auto &file : incoming)
                                    names.append(file.filename);

                                socket->write(HttpUtils::response(
                                    "200 OK",
                                    "text/html; charset=utf-8",
                                    "<!doctype html><meta charset=utf-8>"
                                    "<h1>Files received!</h1><p>" +
                                    names.join(", ")
                                        .toHtmlEscaped().toUtf8() +
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
        });

        connect(socket, &QTcpSocket::disconnected,
                socket, &QObject::deleteLater);
    }
}
