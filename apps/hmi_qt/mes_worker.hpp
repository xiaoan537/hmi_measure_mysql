#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QTimer>

#include "core/config.hpp"
#include "core/db.hpp"

class MesWorker : public QObject
{
    Q_OBJECT
public:
    MesWorker(const core::AppConfig &cfg, QObject *parent = nullptr);

    bool start(QString *err); // 打开DB、ensureSchema、启动定时器
    void kick();              // UI 入队后可调用，促使尽快发送

signals:
    void outboxChanged(); // 发送状态变化，UI可刷新
    void logMessage(const QString &msg);

private slots:
    void onTick();
    void onReplyFinished();

private:
    int computeBackoffSeconds(int attempt_count) const;

    core::AppConfig cfg_;
    core::Db db_;
    QNetworkAccessManager nam_;
    QTimer timer_;

    bool busy_ = false;
    core::MesOutboxTask current_;
    QNetworkReply *reply_ = nullptr;
    QTimer timeoutTimer_;
};
