
#pragma once

#include "HttpRequestParser.h"

class ReceiverServer;
class QTcpSocket;

class RequestRouter
{
public:
    static void handle(
        ReceiverServer &server,
        QTcpSocket *socket,
        const HttpRequestParser::Request &httpRequest
    );
};
