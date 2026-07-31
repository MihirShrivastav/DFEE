#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include "EngineController.h"
#include "PreviewImageProvider.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("DFEE");
    app.setOrganizationName("DFEE");

    // Construct the provider BEFORE EngineController so it can be passed to
    // the worker at construction time.  This ensures provider_ is set before
    // workerThread_.start() and is never written again — fixing the latent
    // race where setProvider() could write provider_ while the worker thread
    // was already live and reading it in doRender().
    // QQmlApplicationEngine takes ownership after addImageProvider(), so we
    // must NOT delete it ourselves.
    auto* provider = new PreviewImageProvider();

    EngineController controller(provider);

    QQmlApplicationEngine engine;
    engine.addImageProvider("preview", provider);   // engine takes ownership
    engine.rootContext()->setContextProperty("engine", &controller);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("DFEE", "Main");

    // Headless self-test: set DFEE_SELFTEST=<path-to-tiff> to verify end-to-end.
    // Set DFEE_SELFTEST2=<path-to-second-tiff> to exercise the coalescing fix:
    // B is opened while A is still decoding; the final preview must show B's
    // dimensions, not A's.
    if (qEnvironmentVariableIsSet("DFEE_SELFTEST")) {
        const QString pathA = qEnvironmentVariable("DFEE_SELFTEST");
        controller.openFile(QUrl::fromLocalFile(pathA));

        if (qEnvironmentVariableIsSet("DFEE_SELFTEST2")) {
            // Back-to-back: B is requested before the event loop runs, so A's
            // openAndRender is queued but not yet executing.  openFile(B) sees
            // workerBusy_=true (set synchronously in openFile(A)), stores B as
            // the pending open, and onWorkerBusyChanged(false) from A's
            // completion will then fire openAndRender(B).
            const QString pathB = qEnvironmentVariable("DFEE_SELFTEST2");
            controller.openFile(QUrl::fromLocalFile(pathB));
        }

        // Task-4 param-change self-test: after a short delay (giving the first
        // render time to complete), nudge filmExposure so a second render fires.
        // The RenderWorker logs "SELFTEST preview ready" on each completion, so
        // two such lines in stderr confirm both renders happened.
        QTimer::singleShot(3000, &controller, [&controller]() {
            qDebug() << "SELFTEST nudging filmExposure to 2.0 to trigger re-render";
            controller.setFilmExposure(2.0);
        });
    }

    return app.exec();
}
