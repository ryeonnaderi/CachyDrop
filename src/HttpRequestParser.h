
#pragma once

#include <QByteArray>
#include <QList>
#include <QUrl>
#include <QUrlQuery>

namespace HttpRequestParser
{

enum class Status {
    NeedMoreData,
    Complete,
    Error
};

struct Request
{
    QByteArray method;
    QUrl url;
    QByteArray body;
    QList<QByteArray> headerLines;
    QUrlQuery query;
};

struct Result
{
    Status status = Status::NeedMoreData;
    Request request;
    QByteArray errorStatus;
    QByteArray errorMessage;
};

constexpr qint64 MaxHeaderSize = 16 * 1024;
constexpr qint64 MaxBodySize =
    100LL * 1024 * 1024 + 2 * 1024 * 1024;

Result parse(const QByteArray &buffer);

} // namespace HttpRequestParser
