#pragma once

#include <QObject>
#include <QStringList>
#include <memory>

namespace dfee { class EngineSession; }

class EngineController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList stockNames READ stockNames NOTIFY stocksChanged)
    Q_PROPERTY(QString stock READ stock WRITE setStock NOTIFY stockChanged)
public:
    explicit EngineController(QObject* parent = nullptr);
    ~EngineController() override;

    QStringList stockNames() const { return stockNames_; }
    QString stock() const { return stockId_; }
    void setStock(const QString& id);

    Q_INVOKABLE QString stockIdAt(int i) const {
        return (i >= 0 && i < stockIds_.size()) ? stockIds_.at(i) : QString("none");
    }

signals:
    void stocksChanged();
    void stockChanged();

private:
    void loadStocks();

    std::unique_ptr<dfee::EngineSession> session_;
    QStringList stockNames_;   // display names, parallel to stockIds_
    QStringList stockIds_;     // stock_id values
    QString stockId_ = "none";
};
