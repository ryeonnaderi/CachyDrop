
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace TransferUtils {

struct IncomingFile {
    QString filename;
    QByteArray data;
};

QString safeFilename(QString filename);

bool parseMultipart(
    const QByteArray &contentType,
    const QByteArray &body,
    QVector<IncomingFile> &incoming,
    QString *errorMessage = nullptr
);

bool saveBatch(
    const QString &folder,
    const QVector<IncomingFile> &incoming,
    QStringList *savedPaths = nullptr,
    QString *failedFilename = nullptr
);

} // namespace TransferUtils
