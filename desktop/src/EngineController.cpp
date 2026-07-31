#include "EngineController.h"
#include "RenderWorker.h"
#include "PreviewImageProvider.h"

#include "dfee/session.hpp"
#include "dfee/bridge_types.hpp"

#include <QFileInfo>
#include <QMetaObject>
#include <QDebug>

#ifndef DFEE_REPO_ROOT
#  define DFEE_REPO_ROOT "."
#endif

EngineController::EngineController(PreviewImageProvider* provider,
                                   QObject* parent)
    : QObject(parent)
    , session_(std::make_unique<dfee::EngineSession>(
          std::filesystem::path(DFEE_REPO_ROOT)))
    , provider_(provider)
{
    // list_profiles() is called on the GUI thread BEFORE the worker thread starts,
    // so there is no concurrent access.
    loadStocks();

    // provider_ is already set (constructor arg); worker receives it before the
    // thread starts, so provider_ is never written after workerThread_.start().
    worker_ = new RenderWorker(session_.get(), this, provider_);
    worker_->moveToThread(&workerThread_);
    workerThread_.start();
}

EngineController::~EngineController()
{
    workerThread_.quit();
    workerThread_.wait();
    // worker_ is now safe to delete; it's a child-less QObject, so plain delete.
    delete worker_;
}

void EngineController::loadStocks()
{
    stockNames_.clear();
    stockIds_.clear();
    stockNames_ << "None";
    stockIds_ << "none";
    const dfee::NativeProfilesResponse profiles = session_->list_profiles();
    for (const auto& s : profiles.stocks) {
        stockNames_ << QString::fromStdString(s.stock_name);
        stockIds_ << QString::fromStdString(s.stock_id);
    }
    emit stocksChanged();
}

void EngineController::setStock(const QString& id)
{
    if (stockId_ == id) return;
    stockId_ = id;
    emit stockChanged();
    scheduleRender();
}

void EngineController::setFilmExposure(double v)
{
    if (qFuzzyCompare(filmExposure_, v)) return;
    filmExposure_ = v;
    emit paramsChanged();
    scheduleRender();
}

void EngineController::setShadowLift(double v)
{
    if (qFuzzyCompare(shadowLift_, v)) return;
    shadowLift_ = v;
    emit paramsChanged();
    scheduleRender();
}

void EngineController::openFile(const QUrl& url)
{
    const QString file = url.toLocalFile();
    status_ = "Loading " + QFileInfo(file).fileName();
    emit statusChanged();

    if (workerBusy_) {
        // Latch the latest file for the pending open; mark it as an open (not
        // a plain re-render) so onWorkerBusyChanged posts openAndRender, which
        // runs select_file + decode_raw before rendering.
        pendingFile_  = file;
        dirty_        = true;
        dirtyIsOpen_  = true;
        // Do NOT update currentFile_ yet — the in-flight operation still owns
        // the session's decoded buffer.  currentFile_ is updated when the
        // deferred open actually fires.
        return;
    }

    currentFile_ = file;
    workerBusy_  = true;
    QMetaObject::invokeMethod(worker_, "openAndRender",
                              Qt::QueuedConnection,
                              Q_ARG(QString, currentFile_),
                              Q_ARG(QString, stockId_),
                              Q_ARG(double, filmExposure_),
                              Q_ARG(double, shadowLift_));
}

void EngineController::scheduleRender()
{
    if (currentFile_.isEmpty()) return;

    if (workerBusy_) {
        dirty_ = true;
        return;
    }

    workerBusy_ = true;
    QMetaObject::invokeMethod(worker_, "render",
                              Qt::QueuedConnection,
                              Q_ARG(QString, currentFile_),
                              Q_ARG(QString, stockId_),
                              Q_ARG(double, filmExposure_),
                              Q_ARG(double, shadowLift_));
}

// --- Callbacks marshalled back to the GUI thread ---

void EngineController::onPreviewReady()
{
    hasImage_ = true;
    emit hasImageChanged();
    previewRevision_++;
    emit previewChanged();
    status_.clear();
    emit statusChanged();
}

void EngineController::exportImage()
{
    if (currentFile_.isEmpty()) return;
    status_ = "Exporting…";
    emit statusChanged();
    QMetaObject::invokeMethod(worker_, "exportImage",
                              Qt::QueuedConnection,
                              Q_ARG(QString, currentFile_),
                              Q_ARG(QString, stockId_),
                              Q_ARG(double, filmExposure_),
                              Q_ARG(double, shadowLift_));
}

void EngineController::onExportDone(const QString& msg)
{
    status_ = msg;
    emit statusChanged();
    qDebug() << "DFEE export:" << msg;
}

void EngineController::onRenderFailed(const QString& msg)
{
    status_ = msg;
    emit statusChanged();
    qDebug() << "DFEE:" << msg;
}

void EngineController::onWorkerBusyChanged(bool busy)
{
    workerBusy_ = busy;
    if (!busy && dirty_) {
        dirty_ = false;

        if (dirtyIsOpen_) {
            // A new file was opened while the worker was busy.  We must run
            // select_file + decode_raw for the new file before rendering — a
            // plain render() would use the OLD decoded buffer.
            dirtyIsOpen_  = false;
            currentFile_  = pendingFile_;
            pendingFile_.clear();

            workerBusy_ = true;
            QMetaObject::invokeMethod(worker_, "openAndRender",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, currentFile_),
                                      Q_ARG(QString, stockId_),
                                      Q_ARG(double, filmExposure_),
                                      Q_ARG(double, shadowLift_));
        } else {
            // Just a parameter change (stock / exposure / shadow lift); the
            // file is already decoded — a render-only kick is correct.
            scheduleRender();
        }
    }
}
