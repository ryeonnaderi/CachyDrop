#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QUrl>
#include <QStringList>
#include <QWindow>
#include <QQuickWindow>
#include "CryptoManager.h"
#include <QDebug>
#include "ReceiverServer.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QQuickWindow::setDefaultAlphaBuffer(true);

   QGuiApplication::setApplicationName("CachyDrop");
   QGuiApplication::setOrganizationName("CachyDrop");
   
   CryptoManager cryptoManager;
   QString cryptoError;

if (!cryptoManager.initialize(&cryptoError)) {
    qCritical() << "Cryptographic initialization failed:"
                << cryptoError;
    return -1;
}

qInfo() << "Cryptographic identity initialized.";
qInfo() << "Public key:" << cryptoManager.publicKeyHex();

ReceiverServer receiverServer(&cryptoManager);
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
    auto *window = qobject_cast<QQuickWindow *>(
    engine.rootObjects().first());

if (window)
    window->setColor(Qt::transparent);

   

    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &receiverServer, &ReceiverServer::stop);

    return app.exec();
}
