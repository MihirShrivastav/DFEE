#pragma once

#include <QObject>
#include <QString>

#include "dfee/bridge_types.hpp"

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
    void openAndRender(const dfee::NativePreviewRenderRequest& request);

    // Re-render only (file already decoded in session cache).
    void render(const dfee::NativePreviewRenderRequest& request);

    // Export full-resolution image. Lightroom mode supplies the exact working TIFF path.
    void exportImage(const dfee::NativeExportRequest& request);

    // Resolves Auto grain through the same native solver without rendering.
    void resolveAutoGrain(const dfee::NativePreviewRenderRequest& request);

private:
    void doRender(const dfee::NativePreviewRenderRequest& request);

    dfee::EngineSession* session_;       // not owned
    EngineController*    controller_;    // not owned, GUI thread
    PreviewImageProvider* provider_;     // not owned, thread-safe via its own mutex
};
