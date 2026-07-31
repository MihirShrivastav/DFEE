#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "EngineController.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("DFEE");
    app.setOrganizationName("DFEE");

    EngineController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("engine", &controller);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("DFEE", "Main");
    return app.exec();
}
