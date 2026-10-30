
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCoreApplication>
#include <QUrl>

#include "ReceiverServer.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName("CachyDrop");
    QGuiApplication::setOrganizationName("CachyDrop");

    ReceiverServer receiverServer;
    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty(
        "receiverServer", &receiverServer);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    engine.load(
        QUrl(QStringLiteral(
            "qrc:/qt/qml/CachyDrop/src/qml/Main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;

    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &receiverServer, &ReceiverServer::stop);

    return app.exec();
}
