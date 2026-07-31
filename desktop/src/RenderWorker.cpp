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

void RenderWorker::openAndRender(const QString& file,
                                  const QString& stock,
                                  double filmExposureEv,
                                  double shadowLift)
{
    // Notify controller we're busy.
    QMetaObject::invokeMethod(controller_, "onWorkerBusyChanged",
                              Qt::QueuedConnection, Q_ARG(bool, true));
    try {
        dfee::NativeSelectRequest sel;
        sel.filename = file.toStdString();
        session_->select_file(sel);

        dfee::NativeRawDecodeRequest dec;
        dec.filename  = file.toStdString();
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

    doRender(file, stock, filmExposureEv, shadowLift);
}

void RenderWorker::render(const QString& file,
                           const QString& stock,
                           double filmExposureEv,
                           double shadowLift)
{
    QMetaObject::invokeMethod(controller_, "onWorkerBusyChanged",
                              Qt::QueuedConnection, Q_ARG(bool, true));
    doRender(file, stock, filmExposureEv, shadowLift);
}

void RenderWorker::doRender(const QString& file,
                             const QString& stock,
                             double filmExposureEv,
                             double shadowLift)
{
    dfee::NativePreviewRenderRequest req;
    req.filename               = file.toStdString();
    req.stock                  = stock.toStdString();
    req.effect_pipeline_version = "filmic_v3";
    req.film_exposure_ev       = static_cast<float>(filmExposureEv);
    req.shadow_lift            = static_cast<float>(shadowLift);

    dfee::NativePreviewRenderResponse resp;
    try {
        resp = session_->render_preview(req);
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
