
#include "HttpUtils.h"

#include <QJsonDocument>

namespace HttpUtils
{
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

} // namespace HttpUtils
