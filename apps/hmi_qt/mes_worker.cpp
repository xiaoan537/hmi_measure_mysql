#include "mes_worker.hpp"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

/*
初始化基类 QObject，将父对象设为 parent，将 cfg_ 设为 cfg;member_name(initial_value)是初始化成员变量的标准语法。
cfg_ 是成员变量名，(cfg) 是用来初始化的值（构造函数参数）。说白了，就是将构造函数中的参数 cfg 赋值给成员变量 cfg_，
以便在类的其他成员函数中使用这个配置对象。只不过这个写法叫做初始化列表。在构造函数初始化时执行，确保成员变量在使用前已经被正确初始化。
*/
MesWorker::MesWorker(const core::AppConfig &cfg, QObject *parent)
    : QObject(parent), cfg_(cfg)
{
    timer_.setInterval(qMax(100, cfg_.mes.auto_interval_ms));                                     // 设置时间间隔为每1000毫秒（即1秒）触发一次,把1000赋值给 timer_ 对象的 interval 属性，表示定时器每隔1秒钟触发一次 timeout 信号
    connect(&timer_, &QTimer::timeout, this, &MesWorker::onTick); // QT中信号槽机制，将信号和槽函数进行连接。当timer_定时器超时时，调用onTick()函数，每秒检查一次数据库中是否有待上传到 MES 的任务,'connect' 通常用于将信号与槽关联起来，表示两个对象之间的交互。

    timeoutTimer_.setSingleShot(true); // timeoutTimer_ 专门用于监控 HTTP 请求的超时，setSingleShot(true) 表示该定时器只触发一次，不会自动重复,防止网络请求卡死（例如服务器无响应、网络故障等）
    connect(&timeoutTimer_, &QTimer::timeout, this, [this]()
            {
    if (reply_) reply_->abort(); }); // 使用 C++11 lambda 作为槽函数，捕获 this 指针，检查 reply_ 是否存在（当前正在进行的网络请求），调用 abort() 强制中断正在进行的 HTTP 请求
}

// 启动 MES 工作线程，初始化数据库连接和定时器
bool MesWorker::start(QString *err)
{
    QString e;
    if (!db_.open(cfg_.db, &e))
    {
        if (err)
            *err = e;
        return false;
    }
    if (!db_.ensureSchema(&e))
    {
        if (err)
            *err = e;
        return false;
    }

    // 防止异常退出导致 SENDING 卡死
    db_.resetStaleSending(300, &e);
    emit outboxChanged(); // 启动时可能 reset 了过期 SENDING，通知 UI 刷新一次

    if (cfg_.mes.enabled && cfg_.mes.auto_enabled)
    {
        timer_.start();
    }
    return true;
}

// 手动触发一次检查，用于快速响应 UI 操作
void MesWorker::kick()
{
    // 手动触发：允许在 auto_enabled=0 的情况下也能发送
    // 并且一次触发后会持续发送，直到队列为空
    force_drain_ = true;
    trySendOnce(true);
}

// 计算下一次重试间隔（指数退避）
int MesWorker::computeBackoffSeconds(int attempt_count) const
{
    const int base = cfg_.mes.retry_base_seconds;
    const int maxS = cfg_.mes.retry_max_seconds;
    // attempt_count 是“已失败次数”，下一次间隔：base * 2^attempt_count，最多 maxS
    int mul = 1;
    const int k = qMin(attempt_count, 10); // 防止移位过大
    mul = (1 << k);
    long long v = 1LL * base * mul;
    if (v > maxS)
        v = maxS;
    if (v < base)
        v = base;
    return int(v);
}

// 检查数据库中是否有待上传到 MES 的任务，并启动上传流程
void MesWorker::onTick()
{
    trySendOnce(false);
}


void MesWorker::trySendOnce(bool force)
{
    if (!cfg_.mes.enabled)
        return;
    if (!force && !cfg_.mes.auto_enabled)
        return;
    if (cfg_.mes.url.trimmed().isEmpty())
        return;
    if (busy_)
        return;

    QString e;
    core::MesOutboxTask task;
    if (!db_.fetchNextDueOutbox(&task, &e))
    {
        // 手动 drain 模式下，如果已无待发送任务，则退出 drain
        if (force_drain_)
            force_drain_ = false;
        return; // none or error (error也先不刷屏)
    }

    if (!db_.markOutboxSending(task.id, &e))
    {
        emit logMessage("markOutboxSending failed: " + e);
        return;
    }

    // PENDING/FAILED -> SENDING，也要让界面及时刷新（否则只能等发送完才看到变化）
    emit outboxChanged();

    current_ = task;
    busy_ = true;

    QNetworkRequest req(QUrl(cfg_.mes.url));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=utf-8");
    req.setRawHeader("Idempotency-Key", task.measurement_uuid.toUtf8());
    if (!cfg_.mes.auth_token.trimmed().isEmpty())
    {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + cfg_.mes.auth_token.toUtf8());
    }

    const QByteArray body = task.payload_json.toUtf8();
    reply_ = nam_.post(req, body);

    connect(reply_, &QNetworkReply::finished, this, &MesWorker::onReplyFinished);
    timeoutTimer_.start(cfg_.mes.timeout_ms);
}

// 处理 HTTP 请求完成信号，检查响应状态码、错误信息、响应体等，根据结果更新数据库状态（成功或失败），并触发 outboxChanged 信号通知界面刷新。
void MesWorker::onReplyFinished()
{
    timeoutTimer_.stop(); // 停止超时定时器，因为请求已完成（无论成功或失败），无需继续等待

    const int httpCode = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();      // 从网络回复对象中获取 HTTP 状态码，使用 attribute方法和 HttpStatusCodeAttribute枚举值来提取服务器响应的状态码，例如 200、400、500 等，以判断请求是否成功
    const QString respBody = QString::fromUtf8(reply_->readAll());                                 // 从网络回复对象中获取响应体，使用 readAll方法读取服务器响应的全部内容，以便进行进一步的处理
    const QString netErr = reply_->error() == QNetworkReply::NoError ? "" : reply_->errorString(); // 从网络回复对象中获取错误信息，使用 error方法检查是否有网络错误发生，若没有则为空字符串；若有错误，则调用 errorString方法获取具体的错误描述，例如连接超时、DNS 解析失败等。

    reply_->deleteLater(); // 安全删除网络回复QNetworkReply对象，确保在请求完成后及时释放资源，防止内存泄漏。
    reply_ = nullptr;      // 手动将 reply_ 指针设为 nullptr，确保在请求完成后及时释放资源，防止悬空指针问题。

    QString e;
    const bool ok = (netErr.isEmpty() && httpCode >= 200 && httpCode < 300); // 检查请求是否成功，根据 HTTP 状态码和网络错误信息判断。若 netErr 为空且 httpCode 在 200 到 300 之间（不包含 300），则认为请求成功。

    if (ok)
    {
        if (!db_.markOutboxSent(current_.id, httpCode, respBody, &e)) // 若请求成功，调用 db_.markOutboxSent方法将任务状态更新为已发送（Sent），并记录 HTTP 状态码、响应体等信息。若更新失败，则记录错误信息并通过 emit logMessage 触发日志消息。
        {
            emit logMessage("markOutboxSent failed: " + e);
        }
    }
    else
    {
        const int backoff = computeBackoffSeconds(current_.attempt_count); // 若请求失败，计算下一次重试间隔（指数退避），并调用 db_.markOutboxFailed方法将任务状态更新为失败（Failed），记录 HTTP 状态码、响应体、错误信息等，并设置下一次重试时间。若更新失败，则记录错误信息并通过 emit logMessage 触发日志消息。
        const QString errMsg = netErr.isEmpty() ? QString("HTTP %1").arg(httpCode) : netErr;
        if (!db_.markOutboxFailed(current_.id, httpCode, respBody, errMsg, backoff, &e))
        {
            emit logMessage("markOutboxFailed failed: " + e);
        }
    }

    busy_ = false;
    emit outboxChanged(); // 触发 outboxChanged 信号，通知界面刷新，显示最新的任务状态（已发送或失败）。

    // 若是手动触发的 drain 模式，则继续发送下一条，直到队列为空
    if (force_drain_)
    {
        trySendOnce(true);
    }
}
