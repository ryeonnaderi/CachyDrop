
#include "Network.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>

namespace Network
{

QString localIPv4Address()
{
    const auto interfaces = QNetworkInterface::allInterfaces();

    for (const QNetworkInterface &interface : interfaces) {
        const auto flags = interface.flags();

        // Ignore interfaces that are down, stopped,
        // or loopback interfaces.
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }

        for (const QNetworkAddressEntry &entry :
             interface.addressEntries()) {
            const QHostAddress ip = entry.ip();

            if (ip.protocol() != QAbstractSocket::IPv4Protocol)
                continue;

            if (ip.isLoopback() || ip.isNull())
                continue;

            return ip.toString();
        }
    }

    return {};
}

QString serverUrl(quint16 port)
{
    const QString ip = localIPv4Address();

    if (ip.isEmpty())
        return {};

    return QStringLiteral("http://%1:%2")
        .arg(ip)
        .arg(port);
}

} // namespace Network
