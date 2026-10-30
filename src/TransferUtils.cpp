
#include "TransferUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace TransferUtils {

QString safeFilename(QString filename)
{
    // Strip any directory path supplied by the sender.
    filename = QFileInfo(filename).fileName();

    // Replace control characters, forward slashes, and backslashes.
    filename.replace(QRegularExpression("[\\x00-\\x1f/\\\\]"), "_");

    filename = filename.trimmed();

    // Reject invalid or empty filenames.
    if (filename.isEmpty() || filename == "." || filename == "..") {
        return {};
    }

    // Keep filenames to a reasonable length.
    return filename.left(240);
}

bool parseMultipart(
    const QByteArray &contentType,
    const QByteArray &body,
    QVector<IncomingFile> &incoming,
    QString *errorMessage)
{
    incoming.clear();

    auto fail = [&](const QString &message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        incoming.clear();
        return false;
    };

    const QString contentTypeString = QString::fromLatin1(contentType);

    if (!contentTypeString.startsWith(
            QStringLiteral("multipart/form-data"),
            Qt::CaseInsensitive)) {
        return fail(QStringLiteral("Request is not multipart/form-data."));
    }

    // Extract the multipart boundary, allowing quoted or unquoted values.
    const QRegularExpression boundaryRegex(
        R"regex(boundary=(?:"([^"]+)"|([^;\s]+)))regex",
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch boundaryMatch =
        boundaryRegex.match(contentTypeString);

    if (!boundaryMatch.hasMatch()) {
        return fail(QStringLiteral("Missing multipart boundary."));
    }

    const QString boundaryString =
        boundaryMatch.captured(1).isEmpty()
            ? boundaryMatch.captured(2)
            : boundaryMatch.captured(1);

    const QByteArray delimiter =
        "--" + boundaryString.toLatin1();

    if (boundaryString.isEmpty()) {
        return fail(QStringLiteral("Empty multipart boundary."));
    }

    const QByteArray partSeparator = "\r\n" + delimiter;

    qsizetype position = body.indexOf(delimiter);

    if (position < 0) {
        return fail(QStringLiteral("Multipart boundary not found."));
    }

    while (position >= 0) {
        // Skip the boundary marker.
        position += delimiter.size();

        // A closing boundary ends with "--".
        if (body.mid(position, 2) == "--") {
            break;
        }

        // Each boundary must be followed by CRLF.
        if (body.mid(position, 2) != "\r\n") {
            return fail(QStringLiteral("Malformed multipart boundary."));
        }

        position += 2;

        // Headers end at the first blank line.
        const qsizetype headersEnd =
            body.indexOf("\r\n\r\n", position);

        if (headersEnd < 0) {
            return fail(QStringLiteral("Malformed multipart headers."));
        }

        const QByteArray headers =
            body.mid(position, headersEnd - position);

        position = headersEnd + 4;

        // Find the next boundary using byte positions, not text splitting.
        const qsizetype nextBoundary =
            body.indexOf(partSeparator, position);

        if (nextBoundary < 0) {
            return fail(QStringLiteral("Multipart part has no closing boundary."));
        }

        const QByteArray partData =
            body.mid(position, nextBoundary - position);

        position = nextBoundary + 2; // Skip CRLF; next loop reads delimiter.

        // Only process file parts named "files".
        const QString headersString = QString::fromLatin1(headers);

        const QRegularExpression dispositionRegex(
            R"regex(content-disposition:\s*form-data\s*;([^\r\n]*))regex",
            QRegularExpression::CaseInsensitiveOption);

        const QRegularExpressionMatch dispositionMatch =
            dispositionRegex.match(headersString);

        if (!dispositionMatch.hasMatch()) {
            continue;
        }

        const QString disposition = dispositionMatch.captured(1);

        const QRegularExpression fieldNameRegex(
    R"regex((?:^|;)\s*name="([^"]*)")regex",
    QRegularExpression::CaseInsensitiveOption);

const QRegularExpression filenameRegex(
    R"regex((?:^|;)\s*filename="([^"]*)")regex",
    QRegularExpression::CaseInsensitiveOption);

        const QRegularExpressionMatch fieldNameMatch =
            fieldNameRegex.match(disposition);

        const QRegularExpressionMatch filenameMatch =
            filenameRegex.match(disposition);

        if (!fieldNameMatch.hasMatch() ||
            fieldNameMatch.captured(1) != QStringLiteral("files") ||
            !filenameMatch.hasMatch()) {
            continue;
        }

        // Decode percent-encoded filename characters, then sanitize.
        QString filename = QUrl::fromPercentEncoding(
            filenameMatch.captured(1).toUtf8());

        filename = safeFilename(filename);

        if (filename.isEmpty()) {
            return fail(QStringLiteral("Invalid uploaded filename."));
        }

        IncomingFile file;
        file.filename = filename;
        file.data = partData;

        incoming.append(file);
    }

    if (incoming.isEmpty()) {
        return fail(QStringLiteral("No uploaded files were found."));
    }

    if (errorMessage) {
        errorMessage->clear();
    }

    return true;
}

bool saveBatch(
    const QString &folder,
    const QVector<IncomingFile> &incoming,
    QStringList *savedPaths,
    QString *failedFilename)
{
    if (savedPaths) {
        savedPaths->clear();
    }

    if (failedFilename) {
        failedFilename->clear();
    }

    auto fail = [&](const QString &filename) {
        if (failedFilename) {
            *failedFilename = filename;
        }

        // Roll back files already written during this batch.
        if (savedPaths) {
            for (const QString &path : *savedPaths) {
                QFile::remove(path);
            }
            savedPaths->clear();
        }

        return false;
    };

    QDir directory(folder);

    if (!directory.exists() && !QDir().mkpath(folder)) {
        return fail(QString());
    }

    // Track names selected during this batch as well as files on disk,
    // so duplicate filenames within one batch cannot overwrite each other.
    QStringList reservedNames;

    for (const IncomingFile &incomingFile : incoming) {
        const QString safeName = safeFilename(incomingFile.filename);

        if (safeName.isEmpty()) {
            return fail(incomingFile.filename);
        }

        const QFileInfo info(safeName);
        const QString baseName = info.completeBaseName();
        const QString suffix = info.completeSuffix();

        QString candidate = safeName;
        int counter = 1;

        auto nameIsTaken = [&](const QString &name) {
            return QFileInfo::exists(directory.filePath(name)) ||
                   reservedNames.contains(name);
        };

        while (nameIsTaken(candidate)) {
            if (suffix.isEmpty()) {
                candidate = QStringLiteral("%1 (%2)")
                                .arg(baseName)
                                .arg(counter);
            } else {
                candidate = QStringLiteral("%1 (%2).%3")
                                .arg(baseName)
                                .arg(counter)
                                .arg(suffix);
            }

            ++counter;
        }

        reservedNames.append(candidate);

        const QString fullPath = directory.filePath(candidate);
        QFile output(fullPath);

        if (!output.open(QIODevice::WriteOnly)) {
            return fail(incomingFile.filename);
        }

        const qint64 bytesWritten = output.write(incomingFile.data);
        const bool writeSucceeded =
            bytesWritten == incomingFile.data.size();

        output.close();

        if (!writeSucceeded) {
            QFile::remove(fullPath);
            return fail(incomingFile.filename);
        }

        if (savedPaths) {
            savedPaths->append(fullPath);
        }
    }

    return true;
}

} // namespace TransferUtils
