
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
#include <qrencode.h>

namespace {

constexpr qint64 MaxUploadSize = 100LL * 1024 * 1024;
constexpr int RequestLifetimeSeconds = 120;

QString newToken()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

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
<p id="status" role="status" aria-live="polite">Connecting to CachyDrop…</p>

<form id="form" hidden>
<label for="file">File to send</label>
<input id="file" type="file" required>
<button id="send">Request transfer</button>
</form>

<p><a id="outgoing" href="/outgoing-page" hidden>
Download files from this computer</a></p>
<progress id="progress" max="100" value="0"></progress>
</main>
<script>
const form = document.querySelector('#form');
const fileInput = document.querySelector('#file');
const send = document.querySelector('#send');
const status = document.querySelector('#status');
const progress = document.querySelector('#progress');
const outgoing = document.querySelector('#outgoing');

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const params = new URLSearchParams(location.search);
const pairingToken = params.get('pair');
let session = sessionStorage.getItem('cachydrop-session');

async function jsonFetch(url, options) {
    const r = await fetch(url, options);
    const data = await r.json();
    if (!r.ok) throw new Error(data.error || data.message || 'Request failed');
    return data;
}

async function pairDevice() {
    if (session) {
        const result = await jsonFetch('/session', {
            headers: {'Authorization': 'Bearer ' + session},
            cache: 'no-store'
        }).catch(() => null);

        if (result && result.valid) {
            form.hidden = false;
            outgoing.hidden = false;
            status.textContent = 'Connected to CachyDrop.';
            return;
        }

        sessionStorage.removeItem('cachydrop-session');
        session = null;
    }

    if (!pairingToken) {
        status.textContent =
            'Scan the QR code shown in the CachyDrop desktop app to pair.';
        return;
    }

    try {
        status.textContent = 'Requesting permission to pair…';

        const request = await jsonFetch('/pair', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                token: pairingToken,
                device: /iPhone|iPad/.test(navigator.userAgent)
                    ? 'iPhone or iPad'
                    : /Android/.test(navigator.userAgent)
                        ? 'Android device' : 'Mobile or web device'
            })
        });

        status.textContent =
            'Approve this device in the CachyDrop desktop app.';

        for (let i = 0; i < 120; i++) {
            await sleep(1000);

            const state = await jsonFetch(
                '/pair-status?id=' + encodeURIComponent(request.id),
                {cache: 'no-store'}
            );

            if (state.state === 'approved') {
                session = state.session;
                sessionStorage.setItem('cachydrop-session', session);
                history.replaceState(null, '', '/');
                form.hidden = false;
                outgoing.hidden = false;
                status.textContent = 'Device paired successfully.';
                return;
            }

            if (state.state !== 'pending')
                throw new Error(state.message || 'Pairing rejected or expired');
        }

        throw new Error('Pairing timed out. Scan the QR code again.');
    } catch (error) {
        status.textContent = 'Pairing failed: ' + error.message;
    }
}

form.addEventListener('submit', async event => {
    event.preventDefault();

    const file = fileInput.files[0];
    if (!file || !session) return;

    send.disabled = true;
    progress.value = 0;

    try {
        status.textContent = 'Requesting desktop approval…';

        const request = await jsonFetch('/request', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                session,
                filename: file.name,
                size: file.size
            })
        });

        let approved = false;

        for (let i = 0; i < 120; i++) {
            await sleep(1000);

            const state = await jsonFetch(
                '/status?id=' + encodeURIComponent(request.id) +
                '&session=' + encodeURIComponent(session),
                {cache: 'no-store'}
            );

            if (state.state === 'approved') {
                approved = true;
                break;
            }

            if (state.state !== 'pending')
                throw new Error(state.message || 'Transfer rejected or expired');

            status.textContent = 'Waiting for approval on your computer…';
        }

        if (!approved) throw new Error('Approval timed out');

        status.textContent = 'Approved. Uploading file…';

        const data = new FormData();
        data.append('file', file, file.name);

        await new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();

            xhr.open('POST',
                '/upload?id=' + encodeURIComponent(request.id) +
                '&session=' + encodeURIComponent(session));

            xhr.upload.addEventListener('progress', event => {
                if (event.lengthComputable)
                    progress.value =
                        Math.round(event.loaded / event.total * 100);
            });

            xhr.addEventListener('load', () => {
                if (xhr.status >= 200 && xhr.status < 300) resolve();
                else reject(new Error(xhr.responseText || 'Upload failed'));
            });

            xhr.addEventListener('error', () =>
                reject(new Error('Network connection failed')));

            xhr.send(data);
        });

        status.textContent = 'File received successfully!';
        progress.value = 100;
        fileInput.value = '';
    } catch (error) {
        status.textContent = 'Transfer failed: ' + error.message;
    } finally {
        send.disabled = false;
    }
});

pairDevice();
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
button{box-sizing:border-box;width:100%;margin-top:14px;padding:13px}
li{margin:18px 0;overflow-wrap:anywhere}
a{overflow-wrap:anywhere}
#status{overflow-wrap:anywhere}
</style>
</head>
<body>
<main>
<h1>Get files from CachyDrop</h1>
<p id="status">Checking device pairing…</p>
<button id="load" hidden>Show available files</button>
<ul id="files"></ul>
<p><a href="/">Back to sending files to the computer</a></p>
</main>
<script>
const load = document.querySelector('#load');
const status = document.querySelector('#status');
const list = document.querySelector('#files');
const session = sessionStorage.getItem('cachydrop-session');

load.addEventListener('click', async () => {
    load.disabled = true;
    list.replaceChildren();
    status.textContent = 'Loading available files…';

    try {
        const r = await fetch('/outgoing', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            cache: 'no-store',
            body: JSON.stringify({session})
        });

        const result = await r.json();
        if (!r.ok) throw new Error(result.error || 'Could not list files');

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

            link.href = '/download?id=' + encodeURIComponent(file.id) +
                '&session=' + encodeURIComponent(session);
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

if (session) {
    fetch('/session', {
        headers: {'Authorization': 'Bearer ' + session},
        cache: 'no-store'
    }).then(r => {
        if (!r.ok) throw new Error('Device is not paired');
        return r.json();
    }).then(data => {
        if (!data.valid) throw new Error('Device is not paired');
        status.textContent = 'Connected to CachyDrop.';
        load.hidden = false;
    }).catch(error => {
        status.textContent = error.message + '. Return to the main page and pair again.';
    });
} else {
    status.textContent = 'No paired device. Scan the desktop QR code first.';
}
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

bool ReceiverServer::hasPendingPairing() const
{
    return m_pairings.contains(m_activePairingId) &&
           m_pairings.value(m_activePairingId).state == "pending";
}

QString ReceiverServer::pendingDevice() const
{
    return m_pairings.value(m_activePairingId).device;
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

    m_pairingToken = newToken();
    m_requests.clear();
    m_pairings.clear();
    m_sessions.clear();
    m_activeRequestId.clear();
    m_activePairingId.clear();
    m_address.clear();
    m_qrRows.clear();

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

    if (!m_address.isEmpty()) {
        const QByteArray url =
            (m_address + "/?pair=" + m_pairingToken).toUtf8();

        QRcode *qr = QRcode_encodeString(
            url.constData(), 0, QR_ECLEVEL_M, QR_MODE_8, 1);

        if (qr != nullptr) {
            for (int y = 0; y < qr->width; ++y) {
                QString row;
                row.reserve(qr->width);

                for (int x = 0; x < qr->width; ++x) {
                    const unsigned char module = qr->data[y * qr->width + x];
                    row.append((module & 1) ? QLatin1Char('1')
                                            : QLatin1Char('0'));
                }

                m_qrRows.append(row);
            }

            QRcode_free(qr);
        }
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
    m_pairingToken.clear();
    m_qrRows.clear();
    m_requests.clear();
    m_pairings.clear();
    m_sessions.clear();
    m_activeRequestId.clear();
    m_activePairingId.clear();
    m_sharedFiles.clear();

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
    auto it = m_pairings.find(m_activePairingId);

    if (it == m_pairings.end() || it->state != "pending")
        return;

    it->state = "approved";
    m_sessions.insert(it->sessionToken, it->device);
    emit pairingRequestChanged();
}

void ReceiverServer::rejectPairing()
{
    auto it = m_pairings.find(m_activePairingId);

    if (it == m_pairings.end() || it->state != "pending")
        return;

    it->state = "rejected";
    it->expiresAt = QDateTime::currentDateTimeUtc().addSecs(30);

    emit pairingRequestChanged();
}

void ReceiverServer::expireRequests()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool transfersChanged = false;
    bool pairingsChanged = false;

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

    for (auto it = m_pairings.begin(); it != m_pairings.end();) {
        if (it->expiresAt <= now) {
            if (it.key() == m_activePairingId)
                m_activePairingId.clear();

            it = m_pairings.erase(it);
            pairingsChanged = true;
        } else {
            ++it;
        }
    }

    if (transfersChanged)
        emit pendingRequestChanged();

    if (pairingsChanged)
        emit pairingRequestChanged();
}

void ReceiverServer::handleConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        socket->setProperty("requestBuffer", QByteArray());

        connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
            if (socket->property("requestProcessed").toBool())
                return;

            QByteArray buffer = socket->property("requestBuffer").toByteArray();
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
                    contentLength = trimmed.mid(trimmed.indexOf(':') + 1)
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
                socket->write(response("400 Bad Request", "text/plain", "Bad request"));
                socket->disconnectFromHost();
                return;
            }

            const QList<QByteArray> firstLine = lines.first().trimmed().split(' ');
            if (firstLine.size() < 2) {
                socket->write(response("400 Bad Request", "text/plain", "Bad request"));
                socket->disconnectFromHost();
                return;
            }

            const QByteArray method = firstLine.at(0);
            const QUrl url(QStringLiteral("http://localhost")
                           + QString::fromUtf8(firstLine.at(1)));
            const QString path = url.path();
            const QUrlQuery query(url);

            auto sendJson = [socket](const QByteArray &status,
                                     const QJsonObject &object) {
                socket->write(jsonResponse(status, object));
            };

            auto readJson = [&body]() {
                return QJsonDocument::fromJson(body);
            };

            auto authorized = [this](const QString &token) {
                return !token.isEmpty() && m_sessions.contains(token);
            };

            if (method == "GET" && (path == "/" || path == "/index.html")) {
                socket->write(response("200 OK", "text/html; charset=utf-8",
                                       htmlPage()));

            } else if (method == "GET" && path == "/health") {
                sendJson("200 OK", {{"status", "ok"}, {"app", "CachyDrop"}});

            } else if (method == "GET" && path == "/session") {
                QByteArray auth;
                for (const QByteArray &line : lines) {
                    const QByteArray trimmed = line.trimmed();
                    if (trimmed.toLower().startsWith("authorization:"))
                        auth = trimmed.mid(trimmed.indexOf(':') + 1).trimmed();
                }

                const QString prefix = QStringLiteral("Bearer ");
                const QString token = QString::fromUtf8(auth).startsWith(prefix)
                    ? QString::fromUtf8(auth).mid(prefix.size()) : QString();

                sendJson("200 OK", {{"valid", authorized(token)}});

            } else if (method == "GET" && path == "/outgoing-page") {
                socket->write(response("200 OK", "text/html; charset=utf-8",
                                       outgoingHtmlPage()));

            } else if (method == "POST" && path == "/pair") {
                const QJsonDocument doc = readJson();

                if (!doc.isObject()) {
                    sendJson("400 Bad Request", {{"error", "Invalid request"}});
                } else {
                    const QJsonObject obj = doc.object();
                    const QString token = obj.value("token").toString();
                    QString device = obj.value("device").toString().trimmed();

                    if (token.isEmpty() || token != m_pairingToken ||
                        m_pairingToken.isEmpty()) {
                        sendJson("403 Forbidden", {{"error", "Invalid QR pairing token"}});
                    } else if (hasPendingPairing()) {
                        sendJson("409 Conflict", {{"error", "Another pairing request is pending"}});
                    } else {
                        if (device.isEmpty())
                            device = "Unknown device";
                        device = device.left(100);

                        PairingRequest request;
                        request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        request.device = device;
                        request.sessionToken = newToken();
                        request.state = "pending";
                        request.expiresAt = QDateTime::currentDateTimeUtc()
                                                 .addSecs(RequestLifetimeSeconds);

                        m_pairings.insert(request.id, request);
                        m_activePairingId = request.id;

                        emit pairingRequestChanged();
                        sendJson("200 OK", {{"id", request.id}});
                    }
                }

            } else if (method == "GET" && path == "/pair-status") {
                const QString id = query.queryItemValue("id");
                const auto it = m_pairings.constFind(id);

                if (it == m_pairings.cend()) {
                    sendJson("404 Not Found", {
                        {"state", "expired"},
                        {"message", "Pairing request expired"}
                    });
                } else if (it->state == "approved") {
                    sendJson("200 OK", {
                        {"state", "approved"},
                        {"session", it->sessionToken}
                    });
                } else {
                    sendJson("200 OK", {
                        {"state", it->state},
                        {"message", it->state == "rejected"
                            ? "Pairing rejected on computer" : ""}
                    });
                }

            } else if (method == "POST" && path == "/outgoing") {
                const QJsonDocument doc = readJson();

                if (!doc.isObject()) {
                    sendJson("400 Bad Request", {{"error", "Invalid request"}});
                } else {
                    const QString token = doc.object().value("session").toString();

                    if (!authorized(token)) {
                        sendJson("403 Forbidden", {{"error", "Device is not paired"}});
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
                }

            } else if (method == "GET" && path == "/download") {
                const QString token = query.queryItemValue("session");
                const QString id = query.queryItemValue("id");
                const auto it = m_sharedFiles.constFind(id);

                if (!authorized(token)) {
                    socket->write(response("403 Forbidden", "text/plain",
                                            "Device is not paired"));
                } else if (it == m_sharedFiles.cend()) {
                    socket->write(response("404 Not Found", "text/plain",
                                            "File not found or no longer shared"));
                } else {
                    const QFileInfo info(it->path);

                    if (!info.exists() || !info.isFile() ||
                        info.size() != it->size || info.size() > MaxUploadSize) {
                        socket->write(response("404 Not Found", "text/plain",
                                                "File is no longer available"));
                    } else {
                        QFile file(it->path);

                        if (!file.open(QIODevice::ReadOnly)) {
                            socket->write(response("500 Internal Server Error",
                                                    "text/plain", "Could not open file"));
                        } else {
                            const QByteArray data = file.readAll();
                            file.close();

                            if (data.size() != it->size) {
                                socket->write(response("500 Internal Server Error",
                                                        "text/plain",
                                                        "Could not read the complete file"));
                            } else {
                                const QByteArray encodedName =
                                    QUrl::toPercentEncoding(it->filename);

                                QByteArray reply =
                                    "HTTP/1.1 200 OK\r\n"
                                    "Content-Type: application/octet-stream\r\n"
                                    "Content-Length: " + QByteArray::number(data.size()) + "\r\n"
                                    "Content-Disposition: attachment; filename*=UTF-8''"
                                    + encodedName + "\r\n"
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
                    sendJson("400 Bad Request", {{"error", "Invalid request"}});
                } else {
                    const QJsonObject obj = doc.object();
                    const QString token = obj.value("session").toString();
                    const QString filename = safeFilename(obj.value("filename").toString());
                    const double rawSize = obj.value("size").toDouble(-1);
                    const qint64 size = static_cast<qint64>(rawSize);

                    if (!authorized(token)) {
                        sendJson("403 Forbidden", {{"error", "Device is not paired"}});
                    } else if (filename.isEmpty() || rawSize < 0 ||
                               rawSize > MaxUploadSize ||
                               static_cast<double>(size) != rawSize) {
                        sendJson("400 Bad Request", {{"error", "Invalid filename or file size"}});
                    } else if (hasPendingRequest() ||
                               (m_requests.contains(m_activeRequestId) &&
                                m_requests.value(m_activeRequestId).state == "approved")) {
                        sendJson("409 Conflict", {{"error", "Another transfer is in progress"}});
                    } else {
                        TransferRequest request;
                        request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        request.filename = filename;
                        request.sender = m_sessions.value(token);
                        request.sessionToken = token;
                        request.size = size;
                        request.state = "pending";
                        request.expiresAt = QDateTime::currentDateTimeUtc()
                                                 .addSecs(RequestLifetimeSeconds);

                        m_requests.insert(request.id, request);
                        m_activeRequestId = request.id;

                        emit pendingRequestChanged();
                        sendJson("200 OK", {{"id", request.id}});
                    }
                }

            } else if (method == "GET" && path == "/status") {
                const QString id = query.queryItemValue("id");
                const QString token = query.queryItemValue("session");
                const auto it = m_requests.constFind(id);

                if (!authorized(token) || it == m_requests.cend() ||
                    it->sessionToken != token) {
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
                const QString token = query.queryItemValue("session");
                auto it = m_requests.find(id);

                if (!authorized(token) || it == m_requests.end() ||
                    it->sessionToken != token || it->state != "approved" ||
                    it->expiresAt <= QDateTime::currentDateTimeUtc()) {
                    sendJson("403 Forbidden", {{"error", "Transfer was not approved"}});
                } else {
                    QByteArray contentType;

                    for (const QByteArray &line : lines) {
                        const QByteArray trimmed = line.trimmed();
                        const qsizetype colon = trimmed.indexOf(':');

                        if (colon >= 0 &&
                            trimmed.left(colon).trimmed().toLower() == "content-type")
                            contentType = trimmed.mid(colon + 1).trimmed();
                    }

                    const QRegularExpression boundaryRegex(
                        R"regex(boundary=(?:"([^"]+)"|([^;\s]+)))regex");
                    const auto match = boundaryRegex.match(QString::fromLatin1(contentType));

                    if (!contentType.toLower().startsWith("multipart/form-data") ||
                        !match.hasMatch()) {
                        sendJson("400 Bad Request", {{"error", "Expected multipart upload"}});
                    } else {
                        const QByteArray boundary =
                            (match.captured(1).isEmpty()
                                ? match.captured(2) : match.captured(1)).toLatin1();

                        const QByteArray marker = "\r\n--" + boundary;
                        const qsizetype partHeadersEnd = body.indexOf("\r\n\r\n");
                        const qsizetype dataEnd = body.lastIndexOf(marker);

                        const QByteArray partHeaders = partHeadersEnd >= 0
                            ? body.left(partHeadersEnd) : QByteArray();

                        const QRegularExpression filenameRegex(
                            R"regex(filename="([^"]*)")regex");
                        const auto filenameMatch = filenameRegex.match(
                            QString::fromLatin1(partHeaders));

                        if (partHeadersEnd < 0 ||
                            dataEnd < partHeadersEnd + 4 ||
                            !partHeaders.contains("name=\"file\"") ||
                            !filenameMatch.hasMatch()) {
                            sendJson("400 Bad Request", {{"error", "Invalid file upload"}});
                        } else {
                            const QString filename = safeFilename(
                                QUrl::fromPercentEncoding(filenameMatch.captured(1).toUtf8()));

                            const QByteArray fileData = body.mid(
                                partHeadersEnd + 4,
                                dataEnd - partHeadersEnd - 4);

                            if (filename != it->filename ||
                                fileData.size() != it->size ||
                                fileData.size() > MaxUploadSize) {
                                sendJson("400 Bad Request", {
                                    {"error", "File details did not match approval"}
                                });
                            } else {
                                const QString folder = QDir::homePath()
                                    + QStringLiteral("/Downloads/CachyDrop");

                                if (!QDir().mkpath(folder)) {
                                    sendJson("500 Internal Server Error", {
                                        {"error", "Cannot create destination folder"}
                                    });
                                } else {
                                    QString destination = folder + "/" + filename;

                                    if (QFileInfo::exists(destination)) {
                                        const QFileInfo info(filename);
                                        const QString base = info.completeBaseName();
                                        const QString suffix = info.completeSuffix().isEmpty()
                                            ? QString() : "." + info.completeSuffix();
                                        int index = 1;

                                        do {
                                            destination = folder + "/" + base
                                                + QStringLiteral(" (%1)").arg(index++)
                                                + suffix;
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
                                        it->expiresAt = QDateTime::currentDateTimeUtc()
                                                            .addSecs(30);

                                        socket->write(response("200 OK",
                                            "text/html; charset=utf-8",
                                            "<!doctype html><meta charset=utf-8>"
                                            "<h1>File received!</h1><p>"
                                            + filename.toHtmlEscaped().toUtf8()
                                            + "</p><a href=\"/\">Send another file</a>"));
                                    }
                                }
                            }
                        }
                    }
                }

            } else {
                socket->write(response("404 Not Found", "text/plain", "Not found"));
            }

            socket->disconnectFromHost();
        });

        connect(socket, &QTcpSocket::disconnected,
                socket, &QObject::deleteLater);
    }
}
