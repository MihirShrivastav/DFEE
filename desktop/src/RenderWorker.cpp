#include "RenderWorker.h"

#include "EngineController.h"
#include "PreviewImageProvider.h"

#include "dfee/session.hpp"
#include "dfee/bridge_types.hpp"

#include <QImage>
#include <QMetaObject>
#include <QDebug>

RenderWorker::RenderWorker(dfee::EngineSession* session,
                           EngineController*    controller,
                           PreviewImageProvider* provider,
                           QObject* parent)
    : QObject(parent)
    , session_(session)
    , controller_(controller)
    , provider_(provider)
{}

void RenderWorker::openAndRender(const dfee::NativePreviewRenderRequest& request)
{
    // Notify controller we're busy.
    QMetaObject::invokeMethod(controller_, "onWorkerBusyChanged",
                              Qt::QueuedConnection, Q_ARG(bool, true));
    try {
        dfee::NativeSelectRequest sel;
        sel.filename = request.filename;
        session_->select_file(sel);

        dfee::NativeRawDecodeRequest dec;
        dec.filename  = request.filename;
        dec.draft_mode = true;
        session_->decode_raw(dec);
    } catch (const std::exception& e) {
        const QString msg = QString("Open failed: %1").arg(e.what());
        QMetaObject::invokeMethod(controller_, "onRenderFailed",
                                  Qt::QueuedConnection, Q_ARG(QString, msg));
        QMetaObject::invokeMethod(controller_, "onWorkerBusyChanged",
                                  Qt::QueuedConnection, Q_ARG(bool, false));
        return;
    }

    doRender(request);
}

void RenderWorker::render(const dfee::NativePreviewRenderRequest& request)
{
    QMetaObject::invokeMethod(controller_, "onWorkerBusyChanged",
                              Qt::QueuedConnection, Q_ARG(bool, true));
    doRender(request);
}

void RenderWorker::exportImage(const dfee::NativeExportRequest& request)
{
    QString msg;
    try {
        const dfee::NativeExportResponse resp = session_->export_image(request);
        if (resp.ok) {
            msg = "Exported: " + QString::fromStdString(resp.output_path.string());
            if (qEnvironmentVariableIsSet("DFEE_SELFTEST_EXPORT")) {
                qDebug() << "SELFTEST_EXPORT output_path:" << QString::fromStdString(resp.output_path.string());
            }
        } else {
            msg = "Export failed: " + QString::fromStdString(resp.error.user_message);
        }
    } catch (const std::exception& e) {
        msg = QString("Export failed: ") + e.what();
    }

    QMetaObject::invokeMethod(controller_, "onExportDone",
                              Qt::QueuedConnection, Q_ARG(QString, msg));
}

void RenderWorker::resolveAutoGrain(const dfee::NativePreviewRenderRequest& request)
{
    try {
        const dfee::NativeGrainResolutionResponse response = session_->resolve_auto_grain(request);
        QMetaObject::invokeMethod(controller_, "onAutoGrainResolved", Qt::QueuedConnection,
                                  Q_ARG(bool, response.ok),
                                  Q_ARG(double, static_cast<double>(response.grain_strength)),
                                  Q_ARG(double, static_cast<double>(response.grain_size)),
                                  Q_ARG(double, static_cast<double>(response.grain_roughness)),
                                  Q_ARG(QString, QString::fromStdString(response.error.user_message)));
    } catch (const std::exception& e) {
        QMetaObject::invokeMethod(controller_, "onAutoGrainResolved", Qt::QueuedConnection,
                                  Q_ARG(bool, false), Q_ARG(double, 0.0), Q_ARG(double, 0.0),
                                  Q_ARG(double, 0.0), Q_ARG(QString, QString::fromUtf8(e.what())));
    }
}

void RenderWorker::doRender(const dfee::NativePreviewRenderRequest& request)
{
    dfee::NativePreviewRenderResponse resp;
    try {
        resp = session_->render_preview(request);
    } catch (const std::exception& e) {
        const QString msg = QString("Render failed: %1").arg(e.what());
        QMetaObject::invokeMethod(controller_, "onRenderFailed",
                                  Qt::QueuedConnection, Q_ARG(QString, msg));
        QMetaObject::invokeMethod(controller_, "onWorkerBusyChanged",
                                  Qt::QueuedConnection, Q_ARG(bool, false));
        return;
    }

    QImage img;
    if (!resp.jpeg_bytes.empty()) {
        img.loadFromData(resp.jpeg_bytes.data(),
                         static_cast<int>(resp.jpeg_bytes.size()), "JPG");
    }

    if (qEnvironmentVariableIsSet("DFEE_SELFTEST") ||
        qEnvironmentVariableIsSet("DFEE_SELFTEST2")) {
        qDebug() << "SELFTEST preview" << img.width() << img.height();
    }

    if (provider_) provider_->setImage(img);

    QMetaObject::invokeMethod(controller_, "onPreviewReady",
                              Qt::QueuedConnection);
    QMetaObject::invokeMethod(controller_, "onWorkerBusyChanged",
                              Qt::QueuedConnection, Q_ARG(bool, false));
}
