#pragma once
#include <QObject>               // Qt 的基类，提供信号槽机制
#include <QNetworkAccessManager> // 用于处理 HTTP 请求
#include <QTimer>                // 用于定时触发上传任务,用于定时器和超时处理

#include "core/config.hpp"
#include "core/db.hpp"

class MesWorker : public QObject
{
    Q_OBJECT
public:
    MesWorker(const core::AppConfig &cfg, QObject *parent = nullptr);

    bool start(QString *err); // 打开DB、ensureSchema、启动定时器
    void kick();              // UI 入队后可调用，促使尽快发送,用于立即触发一次上传检查

signals:                                 // 发送信号，用于通知UI层状态变化
    void outboxChanged();                // 当上传队列状态变化时发出，UI 可以据此刷新显示
    void logMessage(const QString &msg); // 当有日志消息时发出，UI 可以显示这些消息

private slots:              // 私有槽函数，用于处理定时器超时和网络请求完成
    void onTick();          // 定时器触发时调用，检查是否有待上传的任务
    void onReplyFinished(); // 网络请求完成时调用，处理响应结果
    void onHeartbeatTick(); // 定时发送 SYS Heartbeat

private:
    int computeBackoffSeconds(int attempt_count) const; // 根据失败次数计算重试间隔，实现指数退避算法

    core::AppConfig cfg_;
    core::Db db_;
    QNetworkAccessManager nam_; // 用于处理 HTTP 请求，发送 POST 请求到 MES 服务器
    QTimer timer_;              // 定时器，定期检查上传队列
    QTimer heartbeatTimer_;     // SYS Heartbeat 定时器

    bool busy_ = false;              // 标志位，表示是否正在处理上传
    bool heartbeat_busy_ = false;    // Heartbeat 请求进行中
    bool heartbeat_last_ok_ = false; // 上一次 Heartbeat 是否成功
    bool force_drain_ = false;       // 手动触发：尽可能把队列发送完（不依赖自动定时器）
    core::MesOutboxTask current_;    // 当前正在处理的任务
    QNetworkReply *reply_ = nullptr; // 当前的网络请求对象
    QTimer timeoutTimer_;            // 超时定时器，用于防止网络请求卡死
};

/*
定义了一个名为 MesWorker 的类，这是一个工作线程类，负责处理与 MES（制造执行系统）的通信，包括上传测量结果数据、
处理重试逻辑、管理网络请求等功能。这个类继承自 QObject，利用 Qt 的信号槽机制实现异步通信。
*/