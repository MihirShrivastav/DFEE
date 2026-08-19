#include "RenderWorker.h"

#include "EngineController.h"
#include "PreviewImageProvider.h"

#include "dfee/session.hpp"
#include "dfee/bridge_types.hpp"

#include <QImage>
#include <QMetaObject>
#include <QVariantList>
#include <QDebug>

namespace {
constexpr int kVectorscopeSize = 96;

// Compute a 256-bin per-channel histogram from a preview image. Runs on the
// worker thread; the preview is small (~1k px edge) so a full scan is cheap.
void computeHistogram(const QImage& src, QVariantList& r, QVariantList& g, QVariantList& b) {
    int binR[256] = {0}, binG[256] = {0}, binB[256] = {0};
    const QImage im = src.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < im.height(); ++y) {
        const uchar* line = im.constScanLine(y);
        for (int x = 0; x < im.width(); ++x) {
            const uchar* px = line + x * 3;
            ++binR[px[0]];
            ++binG[px[1]];
            ++binB[px[2]];
        }
    }
    r.clear(); g.clear(); b.clear();
    r.reserve(256); g.reserve(256); b.reserve(256);
    for (int i = 0; i < 256; ++i) {
        r.append(binR[i]);
        g.append(binG[i]);
        b.append(binB[i]);
    }
}

// Rec.709 Cb/Cr density field from the rendered preview. Sampling is capped:
// the scope is a live compositional diagnostic, not a full-resolution export.
void computeVectorscope(const QImage& src, QVariantList& scope) {
    int bins[kVectorscopeSize * kVectorscopeSize] = {};
    const QImage im = src.convertToFormat(QImage::Format_RGB888);
    const int step = im.width() * im.height() > 250000 ? 2 : 1;
    for (int y = 0; y < im.height(); y += step) {
        const uchar* line = im.constScanLine(y);
        for (int x = 0; x < im.width(); x += step) {
            const uchar* px = line + x * 3;
            const float red = static_cast<float>(px[0]) / 255.0F;
            const float green = static_cast<float>(px[1]) / 255.0F;
            const float blue = static_cast<float>(px[2]) / 255.0F;
            const float luma = 0.2126F * red + 0.7152F * green + 0.0722F * blue;
            const float cb = (blue - luma) / 1.8556F;
            const float cr = (red - luma) / 1.5748F;
            const int sx = std::clamp(
                static_cast<int>(((cb / 0.5F + 1.0F) * 0.5F) * (kVectorscopeSize - 1) + 0.5F),
                0, kVectorscopeSize - 1);
            const int sy = std::clamp(
                static_cast<int>(((-cr / 0.5F + 1.0F) * 0.5F) * (kVectorscopeSize - 1) + 0.5F),
                0, kVectorscopeSize - 1);
            ++bins[sy * kVectorscopeSize + sx];
        }
    }
    scope.clear();
    scope.reserve(kVectorscopeSize * kVectorscopeSize);
    for (int value : bins) scope.append(value);
}
}  // namespace

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

    // Neutral "before" preview for the before/after compare view. Cheap + cached
    // in the session; produced once per open (it does not change with edits).
    try {
        dfee::NativeRawPreviewRequest rp;
        rp.filename = request.filename;
        rp.max_edge = 1600;
        const dfee::NativeRawPreviewResponse before = session_->raw_preview(rp);
        QImage bimg;
        if (before.ok && !before.jpeg_bytes.empty()) {
            bimg.loadFromData(before.jpeg_bytes.data(),
                              static_cast<int>(before.jpeg_bytes.size()), "JPG");
        }
        if (provider_) provider_->setBeforeImage(bimg);
        QMetaObject::invokeMethod(controller_, "onBeforeReady", Qt::QueuedConnection,
                                  Q_ARG(bool, !bimg.isNull()));
    } catch (const std::exception&) {
        if (provider_) provider_->setBeforeImage(QImage());
        QMetaObject::invokeMethod(controller_, "onBeforeReady", Qt::QueuedConnection,
                                  Q_ARG(bool, false));
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

    if (!img.isNull()) {
        QVariantList hr, hg, hb;
        computeHistogram(img, hr, hg, hb);
        QVariantList scope;
        computeVectorscope(img, scope);
        QMetaObject::invokeMethod(controller_, "onHistogram", Qt::QueuedConnection,
                                  Q_ARG(QVariantList, hr), Q_ARG(QVariantList, hg),
                                  Q_ARG(QVariantList, hb), Q_ARG(QVariantList, scope));
    }

    QMetaObject::invokeMethod(controller_, "onPreviewReady",
                              Qt::QueuedConnection);
    QMetaObject::invokeMethod(controller_, "onWorkerBusyChanged",
                              Qt::QueuedConnection, Q_ARG(bool, false));
}
