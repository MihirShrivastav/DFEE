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

EngineController::EngineController(QObject* parent)
    : QObject(parent)
    , session_(std::make_unique<dfee::EngineSession>(
          std::filesystem::path(DFEE_REPO_ROOT)))
{
    // list_profiles() is called on the GUI thread BEFORE the worker thread starts,
    // so there is no concurrent access.
    loadStocks();

    // Create the worker and move it to its thread.
    // provider_ may still be null here; it will be set before any openFile() call.
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

void EngineController::setProvider(PreviewImageProvider* p)
{
    provider_ = p;
    if (worker_) worker_->setProvider(p);
}

void EngineController::setStock(const QString& id)
{
    if (stockId_ == id) return;
    stockId_ = id;
    emit stockChanged();
    // Re-render if we have a file loaded.
    if (!currentFile_.isEmpty()) {
        scheduleRender();
    }
}

void EngineController::openFile(const QUrl& url)
{
    currentFile_ = url.toLocalFile();
    status_ = "Loading " + QFileInfo(currentFile_).fileName();
    emit statusChanged();

    if (workerBusy_) {
        dirty_ = true;
        return;
    }

    workerBusy_ = true;
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

void EngineController::onPreviewReady(const QImage& /*img*/)
{
    hasImage_ = true;
    emit hasImageChanged();
    previewRevision_++;
    emit previewChanged();
    status_.clear();
    emit statusChanged();
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
        scheduleRender();
    }
}
