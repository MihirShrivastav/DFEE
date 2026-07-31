#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "EngineController.h"
#include "PreviewImageProvider.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("DFEE");
    app.setOrganizationName("DFEE");

    EngineController controller;

    // PreviewImageProvider is owned by QQmlApplicationEngine after addImageProvider().
    auto* provider = new PreviewImageProvider();
    controller.setProvider(provider);

    QQmlApplicationEngine engine;
    engine.addImageProvider("preview", provider);   // engine takes ownership
    engine.rootContext()->setContextProperty("engine", &controller);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("DFEE", "Main");

    // Headless self-test: set DFEE_SELFTEST=<path-to-tiff> to verify end-to-end.
    if (qEnvironmentVariableIsSet("DFEE_SELFTEST")) {
        const QString path = qEnvironmentVariable("DFEE_SELFTEST");
        controller.openFile(QUrl::fromLocalFile(path));
    }

    return app.exec();
}
