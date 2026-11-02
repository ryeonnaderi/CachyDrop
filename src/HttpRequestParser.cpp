
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

    if (firstLine.size() != 3 ||
        firstLine.at(0).isEmpty() ||
        firstLine.at(1).isEmpty() ||
        firstLine.at(2) != "HTTP/1.1") {
        result.status = Status::Error;
        result.errorStatus = "400 Bad Request";
        result.errorMessage = "Invalid request line";
        return result;
    }

    const QByteArray method = firstLine.at(0);
    const QByteArray target = firstLine.at(1);

    // This server accepts origin-form targets, e.g. /request?a=1.
    // Preserve these exact bytes for later signature verification.
    if (!target.startsWith('/') ||
        target.startsWith("//") ||
        target.contains('#') ||
        target.contains('\r') ||
        target.contains('\n')) {
        result.status = Status::Error;
        result.errorStatus = "400 Bad Request";
        result.errorMessage = "Invalid request target";
        return result;
    }

    
    qint64 contentLength = 0;
    bool foundContentLength = false;

    for (qsizetype i = 1; i < lines.size(); ++i) {
    const QByteArray &line = lines.at(i);
        const QByteArray trimmed = line.trimmed();
        const qsizetype colon = trimmed.indexOf(':');

       if (colon <= 0) {
    result.status = Status::Error;
    result.errorStatus = "400 Bad Request";
    result.errorMessage = "Malformed HTTP header";
    return result;
}


const QByteArray rawName = trimmed.left(colon);

for (const char ch : rawName) {
    const bool valid =
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9') ||
        QByteArray("!#$%&'*+-.^_`|~").contains(ch);

    if (!valid) {
        result.status = Status::Error;
        result.errorStatus = "400 Bad Request";
        result.errorMessage = "Invalid HTTP header name";
        return result;
    }
}


        const QByteArray name =
            trimmed.left(colon).trimmed().toLower();
        const QByteArray value =
            trimmed.mid(colon + 1).trimmed();

        if (name == "transfer-encoding") {
            result.status = Status::Error;
            result.errorStatus = "400 Bad Request";
            result.errorMessage = "Transfer-Encoding is not supported";
            return result;
        }

        if (name == "content-length") {
            bool ok = false;
            const qint64 parsedLength = value.toLongLong(&ok);

            // Reject duplicate Content-Length headers, even if identical.
            if (foundContentLength || !ok || parsedLength < 0) {
                result.status = Status::Error;
                result.errorStatus = "400 Bad Request";
                result.errorMessage = "Invalid Content-Length";
                return result;
            }

            contentLength = parsedLength;
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

    const QUrl url(
        QStringLiteral("http://localhost") +
        QString::fromUtf8(target));

    if (!url.isValid() ||
        url.path().isEmpty() ||
        !url.fragment().isEmpty()) {
        result.status = Status::Error;
        result.errorStatus = "400 Bad Request";
        result.errorMessage = "Invalid request URL";
        return result;
    }

    result.request.method = method;
    result.request.rawTarget = target;
    result.request.url = url;
    result.request.query = QUrlQuery(url);
    result.request.body = buffer.mid(bodyStart, contentLength);
    result.request.headerLines = lines;
    result.status = Status::Complete;

    return result;
}

} // namespace HttpRequestParser
