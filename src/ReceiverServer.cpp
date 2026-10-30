
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
#include <QStringList>
#include <qrencode.h>
#include <QSettings>

namespace {

constexpr qint64 MaxUploadSize = 100LL * 1024 * 1024;
constexpr qint64 MaxBatchSize = 100LL * 1024 * 1024;
constexpr int MaxBatchFiles = 20;
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
<label for="file">Files to send (up to 20 files, 100 MiB total)</label>
<input id="file" type="file" multiple required>
<button id="send">Request batch approval</button>
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
let session = localStorage.getItem('cachydrop-session');

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

        localStorage.removeItem('cachydrop-session');
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
                localStorage.setItem('cachydrop-session', session);
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

    const files = Array.from(fileInput.files);
    const totalSize = files.reduce((sum, file) => sum + file.size, 0);
    if (!files.length || !session) return;
    if (files.length > 20 || totalSize > 100 * 1024 * 1024) {
        status.textContent = "Select at most 20 files totaling no more than 100 MiB.";
        return;
    }

    send.disabled = true;
    progress.value = 0;

    try {
        status.textContent = 'Requesting desktop approval…';

        const request = await jsonFetch('/request', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({
                session,
                files: files.map(file => ({ filename: file.name, size: file.size }))
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

        status.textContent = 'Approved. Uploading files…';

        const data = new FormData();
        for (const file of files) data.append('files', file, file.name);

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

        status.textContent = files.length === 1 ? 'File received successfully!' : `${files.length} files received successfully!`;
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
li{list-style:none;margin:14px 0;padding:12px;border:1px solid #8885;border-radius:10px}
.file-row{display:flex;align-items:flex-start;gap:12px}
.file-row input{width:auto;margin-top:5px;flex-shrink:0}
.file-name{overflow-wrap:anywhere}
.file-size{font-size:.9em;opacity:.75}
#status{overflow-wrap:anywhere}
a{overflow-wrap:anywhere}
</style>
</head>
<body>
<main>
<h1>Get files from CachyDrop</h1>
<p id="status">Checking device pairing…</p>
<button id="load" hidden>Show available files</button>
<div id="selection" hidden>
<label>
<input type="checkbox" id="selectAll">
Select all files
</label>
<ul id="files"></ul>
<button id="download" disabled>Download selected</button>
</div>
<p><a href="/">Back to sending files to the computer</a></p>
</main>
<script>
const load = document.querySelector('#load');
const status = document.querySelector('#status');
const list = document.querySelector('#files');
const selection = document.querySelector('#selection');
const selectAll = document.querySelector('#selectAll');
const download = document.querySelector('#download');
const session = localStorage.getItem('cachydrop-session');

function updateSelection() {
    const boxes = Array.from(
        list.querySelectorAll('input[type="checkbox"][data-id]')
    );
    const checked = boxes.filter(box => box.checked);
    download.disabled = checked.length === 0;
    selectAll.checked = boxes.length > 0 && checked.length === boxes.length;
    selectAll.indeterminate =
        checked.length > 0 && checked.length < boxes.length;
}

selectAll.addEventListener('change', () => {
    list.querySelectorAll('input[type="checkbox"][data-id]')
        .forEach(box => box.checked = selectAll.checked);
    updateSelection();
});

list.addEventListener('change', event => {
    if (event.target.matches('input[type="checkbox"][data-id]'))
        updateSelection();
});

load.addEventListener('click', async () => {
    load.disabled = true;
    list.replaceChildren();
    selection.hidden = true;
    selectAll.checked = false;
    selectAll.indeterminate = false;
    status.textContent = 'Loading available files…';

    try {
        const r = await fetch('/outgoing', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            cache: 'no-store',
            body: JSON.stringify({session})
        });

        const result = await r.json();
        if (!r.ok)
            throw new Error(result.error || 'Could not list files');

        if (!result.files.length) {
            status.textContent =
                'No shared files yet. Select files in the desktop app first.';
            return;
        }

        for (const file of result.files) {
            const item = document.createElement('li');
            const row = document.createElement('label');
            const checkbox = document.createElement('input');
            const details = document.createElement('div');
            const name = document.createElement('div');
            const size = document.createElement('div');

            row.className = 'file-row';
            checkbox.type = 'checkbox';
            checkbox.dataset.id = file.id;
            checkbox.dataset.filename = file.filename;

            name.className = 'file-name';
            name.textContent = file.filename;
            size.className = 'file-size';
            size.textContent =
                (file.size / (1024 * 1024)).toFixed(2) + ' MiB';

            details.append(name, size);
            row.append(checkbox, details);
            item.append(row);
            list.append(item);
        }

        selection.hidden = false;
        status.textContent =
            result.files.length + ' file(s) available. Select what to download.';
        updateSelection();
    } catch (error) {
        status.textContent = 'Error: ' + error.message;
    } finally {
        load.disabled = false;
    }
});

download.addEventListener('click', () => {
    const selected = Array.from(
        list.querySelectorAll('input[type="checkbox"][data-id]:checked')
    );

    if (!selected.length)
        return;

    status.textContent =
        'Starting ' + selected.length +
        ' download(s). Your browser may ask permission for multiple downloads.';

    // Each selected file is downloaded separately.
    // A browser may require permission for multiple automatic downloads.
    for (const box of selected) {
        const link = document.createElement('a');
        link.href = '/download?id=' + encodeURIComponent(box.dataset.id) +
            '&session=' + encodeURIComponent(session);
        link.download = box.dataset.filename || '';
        link.hidden = true;
        document.body.append(link);
        link.click();
        link.remove();
    }
});

if (session) {
    fetch('/session', {
        headers: {'Authorization': 'Bearer ' + session},
        cache: 'no-store'
    }).then(r => {
        if (!r.ok)
            throw new Error('Device is not paired');
        return r.json();
    }).then(data => {
        if (!data.valid)
            throw new Error('Device is not paired');
        status.textContent = 'Connected to CachyDrop.';
        load.hidden = false;
    }).catch(error => {
        status.textContent =
            error.message + '. Return to the main page and pair again.';
    });
} else {
    status.textContent =
        'No paired device. Scan the desktop QR code first.';
}
</script>
</body>
</html>)HTML";
}

} // namespace

ReceiverServer::ReceiverServer(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    settings.beginGroup("trustedDevices");

    for (const QString &token : settings.allKeys()) {
        const QString device = settings.value(token).toString();

        if (!token.isEmpty() && !device.isEmpty()) {
            m_trustedDevices.insert(token, device);
        }
    }

    settings.endGroup();
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
m_trustedDevices.insert(it->sessionToken, it->device);

QSettings settings;
settings.beginGroup("trustedDevices");
settings.setValue(it->sessionToken, it->device);
settings.endGroup();
settings.sync();

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
                contentLength > MaxBatchSize + 2 * 1024 * 1024) {
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
    return !token.isEmpty() &&
           (m_sessions.contains(token) ||
            m_trustedDevices.contains(token));
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
                    const QJsonArray filesArray = obj.value("files").toArray();
                    if (!authorized(token)) {
                        sendJson("403 Forbidden", {{"error", "Device is not paired"}});
                    } else if (filesArray.isEmpty() || filesArray.size() > MaxBatchFiles) {
                        sendJson("400 Bad Request", {{"error", "Select between 1 and 20 files"}});
                    } else if (hasPendingRequest() ||
                               (m_requests.contains(m_activeRequestId) &&
                                m_requests.value(m_activeRequestId).state == "approved")) {
                        sendJson("409 Conflict", {{"error", "Another transfer is in progress"}});
                    } else {
                        TransferRequest request;
                        request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        request.sender = m_sessions.value(token, m_trustedDevices.value(token));
                        request.sessionToken = token;
                        request.totalSize = 0;
                        request.state = "pending";
                        request.expiresAt = QDateTime::currentDateTimeUtc().addSecs(RequestLifetimeSeconds);
                        bool valid = true;
                        for (const QJsonValue &value : filesArray) {
                            if (!value.isObject()) { valid = false; break; }
                            const QJsonObject fileObj = value.toObject();
                            const QString filename = safeFilename(fileObj.value("filename").toString());
                            const QJsonValue sizeValue = fileObj.value("size");
                            if (filename.isEmpty() || !sizeValue.isDouble()) { valid = false; break; }
                            const double rawSize = sizeValue.toDouble(-1);
                            if (rawSize < 0 || rawSize > MaxBatchSize) { valid = false; break; }
                            const qint64 size = static_cast<qint64>(rawSize);
                            if (static_cast<double>(size) != rawSize || size > MaxBatchSize - request.totalSize) { valid = false; break; }
                            TransferFile file;
                            file.filename = filename;
                            file.size = size;
                            request.files.append(file);
                            request.totalSize += size;
                        }
                        if (!valid) {
                            sendJson("400 Bad Request", {{"error", "Invalid file list or batch exceeds 100 MiB"}});
                        } else {
                            m_requests.insert(request.id, request);
                            m_activeRequestId = request.id;
                            emit pendingRequestChanged();
                            sendJson("200 OK", {{"id", request.id}});
                        }
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
                        if (colon >= 0 && trimmed.left(colon).trimmed().toLower() == "content-type")
                            contentType = trimmed.mid(colon + 1).trimmed();
                    }
                    const QRegularExpression boundaryRegex(R"regex(boundary=(?:"([^"]+)"|([^;\s]+)))regex");
                    const auto match = boundaryRegex.match(QString::fromLatin1(contentType));
                    if (!contentType.toLower().startsWith("multipart/form-data") || !match.hasMatch()) {
                        sendJson("400 Bad Request", {{"error", "Expected multipart upload"}});
                    } else {
                        const QByteArray boundary = (match.captured(1).isEmpty() ? match.captured(2) : match.captured(1)).toLatin1();
                        const QByteArray delimiter = "--" + boundary;
                        struct IncomingFile { QString filename; QByteArray data; };
                        QVector<IncomingFile> incoming;
                        qsizetype pos = body.indexOf(delimiter);
                        bool valid = pos >= 0;
                        while (valid && pos >= 0) {
                            pos += delimiter.size();
                            if (body.mid(pos, 2) == "--") break;
                            if (body.mid(pos, 2) == "\r\n") pos += 2;
                            const qsizetype headersEnd = body.indexOf("\r\n\r\n", pos);
                            if (headersEnd < 0) { valid = false; break; }
                            const QByteArray partHeaders = body.mid(pos, headersEnd - pos);
                            const qsizetype dataStart = headersEnd + 4;
                            const qsizetype next = body.indexOf("\r\n" + delimiter, dataStart);
                            if (next < 0) { valid = false; break; }
                            const QRegularExpression nameRegex(R"regex(name="([^"]*)")regex");
                            const QRegularExpression filenameRegex(R"regex(filename="([^"]*)")regex");
                            const auto nameMatch = nameRegex.match(QString::fromLatin1(partHeaders));
                            const auto filenameMatch = filenameRegex.match(QString::fromLatin1(partHeaders));
                            if (!nameMatch.hasMatch() || nameMatch.captured(1) != "files" || !filenameMatch.hasMatch()) { valid = false; break; }
                            IncomingFile f;
                            f.filename = safeFilename(QUrl::fromPercentEncoding(filenameMatch.captured(1).toUtf8()));
                            f.data = body.mid(dataStart, next - dataStart);
                            if (f.filename.isEmpty()) { valid = false; break; }
                            incoming.append(f);
                            pos = next + 2;
                        }
                        if (!valid || incoming.size() != it->files.size()) {
                            sendJson("400 Bad Request", {{"error", "Uploaded file list does not match approval"}});
                        } else {
                            qint64 total = 0;
                            for (int i = 0; i < incoming.size(); ++i) {
                                if (incoming[i].filename != it->files[i].filename || incoming[i].data.size() != it->files[i].size || incoming[i].data.size() > MaxBatchSize - total) { valid = false; break; }
                                total += incoming[i].data.size();
                            }
                            if (!valid || total != it->totalSize) {
                                sendJson("400 Bad Request", {{"error", "File details did not match approval"}});
                            } else {
                                const QString folder = QDir::homePath() + QStringLiteral("/Downloads/CachyDrop");
                                if (!QDir().mkpath(folder)) {
                                    sendJson("500 Internal Server Error", {{"error", "Cannot create destination folder"}});
                                } else {
                                    QVector<QString> written;
                                    bool saved = true;
                                    QString failedName;
                                    for (const IncomingFile &f : incoming) {
                                        QString destination = folder + "/" + f.filename;
                                        if (QFileInfo::exists(destination)) {
                                            const QFileInfo info(f.filename);
                                            const QString base = info.completeBaseName();
                                            const QString suffix = info.completeSuffix().isEmpty() ? QString() : "." + info.completeSuffix();
                                            int index = 1;
                                            do { destination = folder + "/" + base + QStringLiteral(" (%1)").arg(index++) + suffix; } while (QFileInfo::exists(destination));
                                        }
                                        QFile output(destination);
                                        if (!output.open(QIODevice::WriteOnly) || output.write(f.data) != f.data.size()) {
                                            output.close(); QFile::remove(destination); saved = false; failedName = f.filename; break;
                                        }
                                        output.close(); written.append(destination);
                                    }
                                    if (!saved) {
                                        for (const QString &path : written) QFile::remove(path);
                                        sendJson("500 Internal Server Error", {{"error", "Could not save all files; batch rolled back"}, {"filename", failedName}});
                                    } else {
                                        it->state = "uploaded";
                                        it->expiresAt = QDateTime::currentDateTimeUtc().addSecs(30);
                                        QStringList names;
                                        for (const IncomingFile &f : incoming) names.append(f.filename);
                                        socket->write(response("200 OK", "text/html; charset=utf-8",
                                            "<!doctype html><meta charset=utf-8><h1>Files received!</h1><p>" + names.join(", ").toHtmlEscaped().toUtf8() + "</p><a href=\"/\">Send more files</a>"));
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
