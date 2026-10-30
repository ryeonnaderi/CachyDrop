
#pragma once

#include <QString>
#include <QtGlobal>

namespace Network
{

// Returns the first usable local IPv4 address,
// or an empty string if none is available.
QString localIPv4Address();

// Returns the server URL, such as "http://192.168.1.25:8765".
// Returns an empty string if no usable address is found.
QString serverUrl(quint16 port = 8765);

} // namespace Network
