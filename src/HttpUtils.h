
#pragma once

#include <QByteArray>
#include <QJsonObject>

namespace HttpUtils
{
    QByteArray response(const QByteArray &status,
                        const QByteArray &contentType,
                        const QByteArray &body);

    QByteArray jsonResponse(const QByteArray &status,
                            const QJsonObject &object);
}
