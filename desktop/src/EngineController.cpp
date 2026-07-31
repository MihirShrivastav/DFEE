#include "EngineController.h"

#include "dfee/session.hpp"
#include "dfee/bridge_types.hpp"

#include <QString>

#ifndef DFEE_REPO_ROOT
#  define DFEE_REPO_ROOT "."
#endif

EngineController::EngineController(QObject* parent)
    : QObject(parent)
    , session_(std::make_unique<dfee::EngineSession>(
          std::filesystem::path(DFEE_REPO_ROOT)))
{
    loadStocks();
}

EngineController::~EngineController() = default;

void EngineController::loadStocks() {
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

void EngineController::setStock(const QString& id) {
    if (stockId_ == id) return;
    stockId_ = id;
    emit stockChanged();
}
