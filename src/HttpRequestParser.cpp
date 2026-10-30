
#include "HttpRequestParser.h"

namespace HttpRequestParser
{

Result parse(const QByteArray &buffer)
{
    Result result;

    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");

    if (headerEnd < 0) {
        if (buffer.size() > MaxHeaderSize) {
            result.status = Status::Error;
            result.errorStatus = "431 Request Header Fields Too Large";
            result.errorMessage = "Headers too large";
        }

        return result;
    }

    if (headerEnd > MaxHeaderSize) {
        result.status = Status::Error;
        result.errorStatus = "431 Request Header Fields Too Large";
        result.errorMessage = "Headers too large";
        return result;
    }

    const QByteArray headers = buffer.left(headerEnd);
    const QList<QByteArray> lines = headers.split('\n');

    if (lines.isEmpty()) {
        result.status = Status::Error;
        result.errorStatus = "400 Bad Request";
        result.errorMessage = "Bad request";
        return result;
    }

    const QList<QByteArray> firstLine =
        lines.first().trimmed().split(' ');

    if (firstLine.size() < 2 ||
        firstLine.at(0).isEmpty() ||
        firstLine.at(1).isEmpty()) {
        result.status = Status::Error;
        result.errorStatus = "400 Bad Request";
        result.errorMessage = "Bad request";
        return result;
    }

    qint64 contentLength = 0;
    bool foundContentLength = false;

    for (const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();

        if (trimmed.toLower().startsWith("content-length:")) {
            bool ok = false;

            const qint64 value =
                trimmed.mid(trimmed.indexOf(':') + 1)
                    .trimmed()
                    .toLongLong(&ok);

            if (!ok || value < 0 ||
                (foundContentLength && value != contentLength)) {
                result.status = Status::Error;
                result.errorStatus = "400 Bad Request";
                result.errorMessage = "Invalid Content-Length";
                return result;
            }

            contentLength = value;
            foundContentLength = true;
        }
    }

    if (contentLength > MaxBodySize) {
        result.status = Status::Error;
        result.errorStatus = "413 Content Too Large";
        result.errorMessage = "Upload too large";
        return result;
    }

    const qsizetype bodyStart = headerEnd + 4;

    if (buffer.size() - bodyStart < contentLength)
        return result;

    const QByteArray target = firstLine.at(1);

    const QUrl url(
        QStringLiteral("http://localhost") +
        QString::fromUtf8(target));

    if (!url.isValid() || url.path().isEmpty()) {
        result.status = Status::Error;
        result.errorStatus = "400 Bad Request";
        result.errorMessage = "Bad request";
        return result;
    }

    result.request.method = firstLine.at(0);
    result.request.url = url;
    result.request.query = QUrlQuery(url);
    result.request.body = buffer.mid(bodyStart, contentLength);
    result.request.headerLines = lines;
    result.status = Status::Complete;

    return result;
}

} // namespace HttpRequestParser
