#include <QGuiApplication>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QImage>
#include <QFontDatabase>
#include <QFont>
#include <QCommandLineParser>
#include <QFile>
#include <QTextStream>
#include "EngineController.h"
#include "PreviewImageProvider.h"

int main(int argc, char* argv[]) {
    // Native Windows controls cannot be safely restyled from QML.  Basic keeps
    // rendering entirely within the application and makes the Graphite tokens
    // deterministic across supported Windows versions.
    QQuickStyle::setStyle("Basic");

    QGuiApplication app(argc, argv);
    app.setApplicationName("DFEE");
    app.setOrganizationName("DFEE");

    // Bundled Geist (Swiss grotesque) — the Graphite typeface. Embedded via qrc so it
    // renders identically everywhere, including headless/offscreen captures.
    QFontDatabase::addApplicationFont(":/fonts/Geist-Regular.ttf");
    QFontDatabase::addApplicationFont(":/fonts/Geist-Medium.ttf");
    QFontDatabase::addApplicationFont(":/fonts/Geist-SemiBold.ttf");
    {
        QFont base("Geist");
        base.setPixelSize(13);
        base.setStyleStrategy(QFont::PreferAntialias);
        app.setFont(base);
    }

    QCommandLineParser commandLine;
    commandLine.setApplicationDescription("DFEE native film editor");
    commandLine.addHelpOption();
    const QCommandLineOption lightroomEditOption(
        "lightroom-edit", "Open a Lightroom-provided TIFF and save back to that exact working file.", "tiff");
    commandLine.addOption(lightroomEditOption);
    commandLine.addPositionalArgument(
        "tiff", "A Lightroom-provided TIFF working file. This is the standard Additional External Editor launch form.");
    commandLine.process(app);

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

    const QStringList positionalArguments = commandLine.positionalArguments();
    if (commandLine.isSet(lightroomEditOption) && !positionalArguments.isEmpty()) {
        qCritical() << "DFEE accepts either --lightroom-edit <tiff> or one TIFF positional argument, not both.";
        return -1;
    }
    if (commandLine.isSet(lightroomEditOption)) {
        controller.beginLightroomRoundTrip(commandLine.value(lightroomEditOption));
    } else if (positionalArguments.size() == 1) {
        controller.beginLightroomRoundTrip(positionalArguments.constFirst());
    } else if (positionalArguments.size() > 1) {
        qCritical() << "DFEE accepts at most one Lightroom TIFF positional argument.";
        return -1;
    }

    // Headless self-test: set DFEE_SELFTEST=<path-to-tiff> to verify end-to-end.
    // Set DFEE_SELFTEST2=<path-to-second-tiff> to exercise the coalescing fix:
    // B is opened while A is still decoding; the final preview must show B's
    // dimensions, not A's.
    if (qEnvironmentVariableIsSet("DFEE_SELFTEST")) {
        const QString pathA = qEnvironmentVariable("DFEE_SELFTEST");
        if (qEnvironmentVariableIsSet("DFEE_SELFTEST_EXPORT_FORMAT")) {
            controller.setExportFormat(qEnvironmentVariable("DFEE_SELFTEST_EXPORT_FORMAT"));
        }
        if (qEnvironmentVariableIsSet("DFEE_SELFTEST_FILM_LAB")) {
            // Exercise the native Film Lab request snapshot with controls from
            // every primary group, rather than only the legacy exposure knob.
            controller.setStock("portra_400");
        }
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

        if (qEnvironmentVariableIsSet("DFEE_SELFTEST_FILM_LAB")) {
            QTimer::singleShot(5000, &controller, [&controller]() {
                qDebug() << "SELFTEST applying Film Lab snapshot controls";
                controller.setFilmControl("highlight_rolloff", 125.0);
                controller.setFilmControl("film_contrast", 115.0);
                controller.setFilmControl("emulsion_color_density", 20.0);
                controller.setFilmControl("palette_range", -15.0);
                controller.setFilmControl("halation_strength", 80.0);
                controller.setFilmControl("bloom", 12.0);
            });
        }

        if (qEnvironmentVariableIsSet("DFEE_SELFTEST_GRAIN")) {
            QTimer::singleShot(7000, &controller, [&controller]() {
                qDebug() << "SELFTEST materializing Auto grain";
                controller.setAutoGrain(false);
            });
        }

        // Task-5 export self-test: after render completes, trigger an export.
        // When DFEE_SELFTEST_EXPORT is set, we also install a status watcher that
        // writes the export result to a log file so the headless test script can
        // verify the output path without relying on OutputDebugString.
        if (qEnvironmentVariableIsSet("DFEE_SELFTEST_EXPORT")) {
            const QString logPath = qEnvironmentVariable("DFEE_SELFTEST_EXPORT");
            // Connect statusChanged so we can capture the export result.
            QObject::connect(&controller, &EngineController::statusChanged,
                             &controller, [&controller, logPath]() {
                const QString st = controller.status();
                if (st.startsWith("Exported:") || st.startsWith("Export failed:")) {
                    QFile f(logPath);
                    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                        QTextStream ts(&f);
                        ts << st << "\n";
                    }
                    QCoreApplication::quit();
                }
            }, Qt::QueuedConnection);

            QTimer::singleShot(7000, &controller, [&controller]() {
                controller.exportImage();
            });
        }
    }

    // Headless UI screenshot: set DFEE_SCREENSHOT=<png-path> to grab the window
    // after first paint and quit. Optional DFEE_SCREENSHOT_OPEN=<image> loads a
    // photo first; DFEE_SCREENSHOT_DELAY=<ms> tunes the settle time. Run under
    // `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software` for deterministic,
    // display-free captures. This is the dev loop for iterating the Graphite look.
    if (qEnvironmentVariableIsSet("DFEE_SCREENSHOT")) {
        const QString shotPath = qEnvironmentVariable("DFEE_SCREENSHOT");
        if (qEnvironmentVariableIsSet("DFEE_SCREENSHOT_OPEN")) {
            controller.openFile(QUrl::fromLocalFile(qEnvironmentVariable("DFEE_SCREENSHOT_OPEN")));
        }
        QObject* rootObj = engine.rootObjects().isEmpty() ? nullptr : engine.rootObjects().constFirst();
        if (auto* win = qobject_cast<QQuickWindow*>(rootObj)) {
            const int delayMs = qEnvironmentVariableIsSet("DFEE_SCREENSHOT_DELAY")
                ? qEnvironmentVariable("DFEE_SCREENSHOT_DELAY").toInt()
                : 1600;
            QTimer::singleShot(delayMs, win, [win, shotPath]() {
                const QImage img = win->grabWindow();
                if (!img.isNull() && img.save(shotPath)) {
                    qInfo() << "SCREENSHOT saved" << shotPath << img.width() << "x" << img.height();
                } else {
                    qWarning() << "SCREENSHOT failed to save" << shotPath;
                }
                QCoreApplication::quit();
            });
        } else {
            qWarning() << "SCREENSHOT: root object is not a QQuickWindow";
        }
    }

    return app.exec();
}
