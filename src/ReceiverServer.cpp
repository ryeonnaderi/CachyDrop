
#include "ReceiverServer.h"
#include "WebPages.h"
#include "TransferUtils.h"
#include "HttpUtils.h"
#include "PairingManager.h"
#include "Network.h"
#include "QrCodeUtils.h"
#include "HttpRequestParser.h"
#include "RequestRouter.h"


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

            RequestRouter::handle(*this, socket, parsed.request);
        });

        connect(socket, &QTcpSocket::disconnected,
                socket, &QObject::deleteLater);
    }
}
