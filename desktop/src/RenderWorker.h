#pragma once

#include <QObject>
#include <QString>

namespace dfee { class EngineSession; }
class EngineController;
class PreviewImageProvider;

// Runs on the worker QThread.  The EngineSession* is owned by EngineController
// but may only be touched from within this worker's thread.
class RenderWorker : public QObject {
    Q_OBJECT
public:
    explicit RenderWorker(dfee::EngineSession* session,
                          EngineController* controller,
                          PreviewImageProvider* provider,
                          QObject* parent = nullptr);

public slots:
    // Decode (select + decode_raw) then render.  Called when a new file is opened.
    void openAndRender(const QString& file,
                       const QString& stock,
                       double filmExposureEv,
                       double shadowLift);

    // Re-render only (file already decoded in session cache).
    void render(const QString& file,
                const QString& stock,
                double filmExposureEv,
                double shadowLift);

    // Export full-resolution TIFF (writes beside the source file).
    void exportImage(const QString& file,
                     const QString& stock,
                     double filmExposureEv,
                     double shadowLift);

private:
    void doRender(const QString& file,
                  const QString& stock,
                  double filmExposureEv,
                  double shadowLift);

    dfee::EngineSession* session_;       // not owned
    EngineController*    controller_;    // not owned, GUI thread
    PreviewImageProvider* provider_;     // not owned, thread-safe via its own mutex
};
