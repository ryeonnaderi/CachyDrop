#include "ReceiverServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QTcpSocket>
#include <QTimer>

namespace {

constexpr qint64 MaxUploadSize = 100LL * 1024 * 1024;
constexpr int RequestLifetimeSeconds = 120;

QByteArray response(const QByteArray &status,
                    const QByteArray &contentType,
                    const QByteArray &body)
{
    return "HTTP/1.1 " + status + "\r\n"
           "Content-Type: " + contentType + "\r\n"
           "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
           "Cache-Control: no-store\r\n"
           "Connection: close\r\n"
           "X-Content-Type-Options: nosniff\r\n"
           "\r\n" + body;
}

QByteArray jsonResponse(const QByteArray &status,
                        const QJsonObject &object)
{
    return response(status, "application/json; charset=utf-8",
                    QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QString safeFilename(QString filename)
{
    filename = QFileInfo(filename).fileName();
    filename.replace(QRegularExpression("[\\x00-\\x1f/\\\\]"), "_");
    filename = filename.trimmed();

    if (filename.isEmpty() || filename == "." || filename == "..")
        return {};

    return filename.left(240);
}

QByteArray htmlPage()
{
    return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CachyDrop</title>
<style>
body{font:16px system-ui;max-width:540px;margin:8vh auto;padding:16px}
main{border:1px solid #8885;border-radius:20px;padding:24px}
input,button{box-sizing:border-box;width:100%;margin-top:14px;padding:13px}
button{cursor:pointer}
progress{width:100%;margin-top:12px}
#status{overflow-wrap:anywhere}
</style>
</head>
<body>
<main>
<h1>CachyDrop</h1>
<p>Enter the pairing code shown in the CachyDrop desktop app.</p>
<form id="form">
<label for="code">Pairing code</label>
<input id="code" inputmode="numeric" pattern="[0-9]{6}"
       maxlength="6" required autocomplete="one-time-code">
<label for="file">File to send</label>
<input id="file" type="file" required>
<button id="send">Request transfer</button>
</form>
<p id="status" role="status" aria-live="polite">Ready.</p>
<p><a href="/outgoing-page">Download files from this computer</a></p>
<progress id="progress" max="100" value="0"></progress>
</main>
<script>
const form = document.querySelector('#form');
const code = document.querySelector('#code');
const fileInput = document.querySelector('#file');
const send = document.querySelector('#send');
const status = document.querySelector('#status');
const progress = document.querySelector('#progress');

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

form.addEventListener('submit', async event => {
    event.preventDefault();
    const file = fileInput.files[0];
    if (!file) return;

    send.disabled = true;
    progress.value = 0;

    try {
        status.textContent = 'Requesting desktop approval…';

        const requestResponse = await fetch('/request', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                code: code.value.trim(),
                filename: file.name,
                size: file.size
            })
        });

        const request = await requestResponse.json();
        if (!requestResponse.ok)
            throw new Error(request.error || 'Request rejected');

        let approved = false;
        for (let i = 0; i < 120; i++) {
            await sleep(1000);
            const check = await fetch(
                '/status?id=' + encodeURIComponent(request.id),
                {cache: 'no-store'}
            );
            const state = await check.json();

            if (state.state === 'approved') {
                approved = true;
                break;
            }
            if (state.state !== 'pending')
                throw new Error(state.message || 'Request expired or rejected');

            status.textContent = 'Waiting for approval on your computer…';
        }

        if (!approved) throw new Error('Approval timed out');
        status.textContent = 'Approved. Uploading file…';

        const data = new FormData();
        data.append('file', file, file.name);
        const xhr = new XMLHttpRequest();

        const upload = new Promise((resolve, reject) => {
            xhr.open('POST', '/upload?id=' + encodeURIComponent(request.id));
            xhr.upload.addEventListener('progress', event => {
                if (event.lengthComputable)
                    progress.value = Math.round(event.loaded / event.total * 100);
            });
            xhr.addEventListener('load', () => {
                if (xhr.status >= 200 && xhr.status < 300) resolve();
                else reject(new Error(xhr.responseText || 'Upload failed'));
            });
            xhr.addEventListener('error', () =>
                reject(new Error('Network connection failed')));
            xhr.send(data);
        });

        await upload;
        status.textContent = 'File received successfully!';
        progress.value = 100;
        fileInput.value = '';
    } catch (error) {
        status.textContent = 'Transfer failed: ' + error.message;
    } finally {
        send.disabled = false;
    }
});
</script>
</body>
</html>)HTML";
}

QByteArray outgoingHtmlPage()
{
    return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CachyDrop — Get files</title>
<style>
body{font:16px system-ui;max-width:540px;margin:8vh auto;padding:16px}
main{border:1px solid #8885;border-radius:20px;padding:24px}
input,button{box-sizing:border-box;width:100%;margin-top:14px;padding:13px}
button{cursor:pointer}
li{margin:18px 0;overflow-wrap:anywhere}
a{overflow-wrap:anywhere}
#status{overflow-wrap:anywhere}
</style>
</head>
<body>
<main>
<h1>Get files from CachyDrop</h1>
<p>Enter the pairing code displayed on your computer.</p>
<form id="form">
<label for="code">Pairing code</label>
<input id="code" inputmode="numeric" pattern="[0-9]{6}"
       maxlength="6" required autocomplete="one-time-code">
<button id="load">Show available files</button>
</form>
<p id="status" role="status" aria-live="polite">Ready.</p>
<ul id="files"></ul>
<p><a href="/">Back to sending files to the computer</a></p>
</main>
<script>
const form = document.querySelector('#form');
const code = document.querySelector('#code');
const load = document.querySelector('#load');
const status = document.querySelector('#status');
const list = document.querySelector('#files');

form.addEventListener('submit', async event => {
    event.preventDefault();
    load.disabled = true;
    list.replaceChildren();
    status.textContent = 'Loading available files…';

    try {
        const response = await fetch('/outgoing', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            cache: 'no-store',
            body: JSON.stringify({code: code.value.trim()})
        });
        const result = await response.json();
        if (!response.ok)
            throw new Error(result.error || 'Could not list files');

        if (!result.files.length) {
            status.textContent =
                'No shared files yet. Select files in the desktop app first.';
            return;
        }

        status.textContent = 'Tap a file to download it to your phone.';
        for (const file of result.files) {
            const item = document.createElement('li');
            const link = document.createElement('a');
            const details = document.createElement('div');
            link.href = '/download?id=' + encodeURIComponent(file.id);
            link.textContent = file.filename;
            details.textContent =
                (file.size / (1024 * 1024)).toFixed(2) + ' MiB';
            item.append(link, details);
            list.append(item);
        }
    } catch (error) {
        status.textContent = 'Error: ' + error.message;
    } finally {
        load.disabled = false;
    }
});
</script>
</body>
</html>)HTML";
}

} // namespace

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

bool ReceiverServer::hasPendingRequest() const
{
    return m_requests.contains(m_activeRequestId) &&
           m_requests.value(m_activeRequestId).state == "pending";
}

QString ReceiverServer::pendingFilename() const
{
    return m_requests.value(m_activeRequestId).filename;
}

qint64 ReceiverServer::pendingSize() const
{
    return m_requests.value(m_activeRequestId).size;
}

QString ReceiverServer::pendingSender() const
{
    return m_requests.value(m_activeRequestId).sender;
}

int ReceiverServer::sharedFileCount() const
{
    return m_sharedFiles.size();
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
        file.filename = safeFilename(info.fileName());
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

    m_requests.clear();
    m_activeRequestId.clear();
    m_address.clear();

    for (const QNetworkInterface &interface :
         QNetworkInterface::allInterfaces()) {
        const auto flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;

        for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol &&
                ip != QHostAddress::LocalHost) {
                m_address = QStringLiteral("http://%1:8765").arg(ip.toString());
                break;
            }
        }
        if (!m_address.isEmpty())
            break;
    }

    emit runningChanged();
    emit pendingRequestChanged();
    return true;
}

void ReceiverServer::stop()
{
    if (!running())
        return;

    m_server.close();
    m_address.clear();
    m_pairingCode.clear();
    m_requests.clear();
    m_activeRequestId.clear();
    m_sharedFiles.clear();

    emit sharedFilesChanged();
    emit runningChanged();
    emit pendingRequestChanged();
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

void ReceiverServer::expireRequests()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool changed = false;

    for (auto it = m_requests.begin(); it != m_requests.end();) {
        if (it->expiresAt <= now) {
            if (it.key() == m_activeRequestId)
                m_activeRequestId.clear();
            it = m_requests.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        emit pendingRequestChanged();
}

void ReceiverServer::handleConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        socket->setProperty("requestBuffer", QByteArray());

        connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
            if (socket->property("requestProcessed").toBool())
                return;

            QByteArray buffer =
                socket->property("requestBuffer").toByteArray();
            buffer += socket->readAll();

            const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                if (buffer.size() > 16 * 1024) {
                    socket->setProperty("requestProcessed", true);
                    socket->write(response("431 Request Header Fields Too Large",
                                            "text/plain", "Headers too large"));
                    socket->disconnectFromHost();
                } else {
                    socket->setProperty("requestBuffer", buffer);
                }
                return;
            }

            const QByteArray headers = buffer.left(headerEnd);
            qint64 contentLength = 0;
            for (const QByteArray &line : headers.split('\n')) {
                const QByteArray trimmed = line.trimmed();
                if (trimmed.toLower().startsWith("content-length:")) {
                    bool ok = false;
                    contentLength =
                        trimmed.mid(trimmed.indexOf(':') + 1)
                            .trimmed().toLongLong(&ok);
                    if (!ok)
                        contentLength = -1;
                }
            }

            if (contentLength < 0 ||
                contentLength > MaxUploadSize + 1024 * 1024) {
                socket->setProperty("requestProcessed", true);
                socket->write(response("413 Content Too Large",
                                       "text/plain", "Upload too large"));
                socket->disconnectFromHost();
                return;
            }

            if (buffer.size() < headerEnd + 4 + contentLength) {
                socket->setProperty("requestBuffer", buffer);
                return;
            }

            socket->setProperty("requestProcessed", true);
            const QByteArray body = buffer.mid(headerEnd + 4, contentLength);
            const QList<QByteArray> lines = headers.split('\n');
            if (lines.isEmpty()) {
                socket->write(response("400 Bad Request",
                                       "text/plain", "Bad request"));
                socket->disconnectFromHost();
                return;
            }

            const QList<QByteArray> firstLine =
                lines.first().trimmed().split(' ');
            if (firstLine.size() < 2) {
                socket->write(response("400 Bad Request",
                                       "text/plain", "Bad request"));
                socket->disconnectFromHost();
                return;
            }

            const QByteArray method = firstLine.at(0);
            const QUrl url(QStringLiteral("http://localhost") +
                           QString::fromUtf8(firstLine.at(1)));
            const QString path = url.path();
            const QUrlQuery query(url);

            auto sendJson = [socket](const QByteArray &status,
                                     const QJsonObject &object) {
                socket->write(jsonResponse(status, object));
            };

            if (method == "GET" &&
                (path == "/" || path == "/index.html")) {
                socket->write(response("200 OK",
                    "text/html; charset=utf-8", htmlPage()));

            } else if (method == "GET" && path == "/health") {
                sendJson("200 OK", {
                    {"status", "ok"}, {"app", "CachyDrop"}
                });

            } else if (method == "GET" && path == "/outgoing-page") {
                socket->write(response("200 OK",
                    "text/html; charset=utf-8", outgoingHtmlPage()));

            } else if (method == "POST" && path == "/outgoing") {
                const QJsonDocument document = QJsonDocument::fromJson(body);
                if (!document.isObject()) {
                    sendJson("400 Bad Request", {
                        {"error", "Invalid request"}
                    });
                } else if (m_pairingCode.isEmpty() ||
                           document.object().value("code").toString()
                               != m_pairingCode) {
                    sendJson("403 Forbidden", {
                        {"error", "Incorrect pairing code"}
                    });
                } else {
                    QJsonArray files;
                    for (const SharedFile &file : m_sharedFiles) {
                        const QFileInfo info(file.path);
                        if (!info.exists() || !info.isFile() ||
                            info.size() != file.size)
                            continue;
                        files.append(QJsonObject{
                            {"id", file.id},
                            {"filename", file.filename},
                            {"size", file.size}
                        });
                    }
                    sendJson("200 OK", {{"files", files}});
                }

            } else if (method == "GET" && path == "/download") {
                const QString id = query.queryItemValue("id");
                const auto it = m_sharedFiles.constFind(id);
                if (it == m_sharedFiles.cend()) {
                    socket->write(response("404 Not Found", "text/plain",
                                           "File not found or no longer shared"));
                } else {
                    const QFileInfo info(it->path);
                    if (!info.exists() || !info.isFile() ||
                        info.size() != it->size ||
                        info.size() > MaxUploadSize) {
                        socket->write(response("404 Not Found", "text/plain",
                                               "File is no longer available"));
                    } else {
                        QFile file(it->path);
                        if (!file.open(QIODevice::ReadOnly)) {
                            socket->write(response("500 Internal Server Error",
                                                   "text/plain",
                                                   "Could not open file"));
                        } else {
                            const QByteArray data = file.readAll();
                            file.close();
                            if (data.size() != it->size) {
                                socket->write(response(
                                    "500 Internal Server Error", "text/plain",
                                    "Could not read the complete file"));
                            } else {
                                const QByteArray encodedName =
                                    QUrl::toPercentEncoding(it->filename);
                                QByteArray reply =
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Type: application/octet-stream\r\n"
                                    "Content-Length: " +
                                    QByteArray::number(data.size()) + "\r\n"
                                    "Content-Disposition: attachment; "
                                    "filename*=UTF-8''" + encodedName + "\r\n"
                                    "Cache-Control: no-store\r\n"
                                    "Connection: close\r\n"
                                    "X-Content-Type-Options: nosniff\r\n"
                                    "\r\n";
                                reply += data;
                                socket->write(reply);
                            }
                        }
                    }
                }

            } else if (method == "POST" && path == "/request") {
                const QJsonDocument document = QJsonDocument::fromJson(body);
                if (!document.isObject()) {
                    sendJson("400 Bad Request", {
                        {"error", "Invalid request"}
                    });
                } else {
                    const QJsonObject obj = document.object();
                    const QString code = obj.value("code").toString();
                    const QString filename =
                        safeFilename(obj.value("filename").toString());
                    const double rawSize = obj.value("size").toDouble(-1);
                    const qint64 size = static_cast<qint64>(rawSize);

                    if (code != m_pairingCode || m_pairingCode.isEmpty()) {
                        sendJson("403 Forbidden", {
                            {"error", "Incorrect pairing code"}
                        });
                    } else if (filename.isEmpty() || rawSize < 0 ||
                               rawSize > MaxUploadSize ||
                               static_cast<double>(size) != rawSize) {
                        sendJson("400 Bad Request", {
                            {"error", "Invalid filename or file size"}
                        });
                    } else if (hasPendingRequest() ||
                               (m_requests.contains(m_activeRequestId) &&
                                m_requests.value(m_activeRequestId).state
                                    == "approved")) {
                        sendJson("409 Conflict", {
                            {"error", "Another transfer is in progress"}
                        });
                    } else {
                        const QString id =
                            QUuid::createUuid().toString(QUuid::WithoutBraces);
                        TransferRequest request;
                        request.id = id;
                        request.filename = filename;
                        request.size = size;
                        request.sender = socket->peerAddress().toString();
                        request.state = "pending";
                        request.expiresAt =
                            QDateTime::currentDateTimeUtc()
                                .addSecs(RequestLifetimeSeconds);

                        m_requests.insert(id, request);
                        m_activeRequestId = id;
                        emit pendingRequestChanged();
                        sendJson("200 OK", {{"id", id}});
                    }
                }

            } else if (method == "GET" && path == "/status") {
                const QString id = query.queryItemValue("id");
                const auto it = m_requests.constFind(id);
                if (it == m_requests.cend()) {
                    sendJson("404 Not Found", {
                        {"state", "expired"}, {"message", "Request expired"}
                    });
                } else {
                    sendJson("200 OK", {
                        {"state", it->state},
                        {"message", it->state == "rejected"
                            ? "Transfer rejected on computer" : ""}
                    });
                }

            } else if (method == "POST" && path == "/upload") {
                const QString id = query.queryItemValue("id");
                auto it = m_requests.find(id);
                if (it == m_requests.end() || it->state != "approved" ||
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

                    const QRegularExpression boundaryRegex(
                        R"regex(boundary=(?:"([^"]+)"|([^;\s]+)))regex");
                    const auto match = boundaryRegex.match(
                        QString::fromLatin1(contentType));

                    if (!contentType.toLower().startsWith(
                            "multipart/form-data") || !match.hasMatch()) {
                        sendJson("400 Bad Request", {
                            {"error", "Expected multipart upload"}
                        });
                    } else {
                        const QByteArray boundary =
                            (match.captured(1).isEmpty()
                                ? match.captured(2)
                                : match.captured(1)).toLatin1();
                        const QByteArray marker = "\r\n--" + boundary;
                        const qsizetype partHeadersEnd =
                            body.indexOf("\r\n\r\n");
                        const qsizetype dataEnd = body.lastIndexOf(marker);
                        const QByteArray partHeaders =
                            partHeadersEnd >= 0
                                ? body.left(partHeadersEnd) : QByteArray();

                        const QRegularExpression filenameRegex(
                            R"regex(filename="([^"]*)")regex");
                        const auto filenameMatch = filenameRegex.match(
                            QString::fromLatin1(partHeaders));

                        if (partHeadersEnd < 0 ||
                            dataEnd < partHeadersEnd + 4 ||
                            !partHeaders.contains("name=\"file\"") ||
                            !filenameMatch.hasMatch()) {
                            sendJson("400 Bad Request", {
                                {"error", "Invalid file upload"}
                            });
                        } else {
                            const QString filename = safeFilename(
                                QUrl::fromPercentEncoding(
                                    filenameMatch.captured(1).toUtf8()));
                            const QByteArray fileData = body.mid(
                                partHeadersEnd + 4,
                                dataEnd - partHeadersEnd - 4);

                            if (filename != it->filename ||
                                fileData.size() != it->size ||
                                fileData.size() > MaxUploadSize) {
                                sendJson("400 Bad Request", {
                                    {"error",
                                     "File details did not match approval"}
                                });
                            } else {
                                const QString folder = QDir::homePath() +
                                    QStringLiteral("/Downloads/CachyDrop");
                                if (!QDir().mkpath(folder)) {
                                    sendJson("500 Internal Server Error", {
                                        {"error",
                                         "Cannot create destination folder"}
                                    });
                                } else {
                                    QString destination = folder + "/" + filename;
                                    if (QFileInfo::exists(destination)) {
                                        const QFileInfo info(filename);
                                        const QString base =
                                            info.completeBaseName();
                                        const QString suffix =
                                            info.completeSuffix().isEmpty()
                                                ? QString()
                                                : "." + info.completeSuffix();
                                        int index = 1;
                                        do {
                                            destination = folder + "/" + base +
                                                QStringLiteral(" (%1)").arg(index++) +
                                                suffix;
                                        } while (QFileInfo::exists(destination));
                                    }

                                    QFile output(destination);
                                    if (!output.open(QIODevice::WriteOnly) ||
                                        output.write(fileData) != fileData.size()) {
                                        output.close();
                                        QFile::remove(destination);
                                        sendJson("500 Internal Server Error", {
                                            {"error", "Could not save file"}
                                        });
                                    } else {
                                        output.close();
                                        it->state = "uploaded";
                                        it->expiresAt =
                                            QDateTime::currentDateTimeUtc()
                                                .addSecs(30);
                                        socket->write(response(
                                            "200 OK",
                                            "text/html; charset=utf-8",
                                            "<!doctype html><meta charset=utf-8>"
                                            "<h1>File received!</h1><p>" +
                                            filename.toHtmlEscaped().toUtf8() +
                                            "</p><a href=\"/\">Send another file</a>"));
                                    }
                                }
                            }
                        }
                    }
                }

            } else {
                socket->write(response("404 Not Found",
                                       "text/plain", "Not found"));
            }

            socket->disconnectFromHost();
        });

        connect(socket, &QTcpSocket::disconnected,
                socket, &QObject::deleteLater);
    }
}
