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
    timer_.setInterval(1000);                                     // 设置时间间隔为每1000毫秒（即1秒）触发一次,把1000赋值给 timer_ 对象的 interval 属性，表示定时器每隔1秒钟触发一次 timeout 信号
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

    timer_.start();
    return true;
}

// 手动触发一次检查，用于快速响应 UI 操作
void MesWorker::kick()
{
    // 立刻跑一次
    onTick();
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
    if (!cfg_.mes.enabled)
        return;
    if (busy_)
        return;

    QString e;
    core::MesOutboxTask task;
    if (!db_.fetchNextDueOutbox(&task, &e))
    {
        return; // none or error (error也先不刷屏)
    }

    if (!db_.markOutboxSending(task.id, &e))
    {
        emit logMessage("markOutboxSending failed: " + e);
        return;
    }

    current_ = task;
    busy_ = true;

    // 创建一个网络请求对象并设置其关键头部信息，以确保与服务器进行正确、安全的通信。
    QNetworkRequest req(QUrl(cfg_.mes.url));                                              // 创建 QNetworkRequest 对象，用于设置 HTTP 请求的 URL;QUrl:Qt 中用于处理和解析 URL 的类。它能将字符串转换为结构化的 URL 对象，并自动处理编码、解析协议、主机、端口、路径等部分。
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=utf-8"); // 通过 setHeader方法设置了一个预定义头部，设置请求头的 Content-Type 为 application/json; charset=utf-8，确保服务器能正确解析请求体中的 JSON 数据。ContentTypeHeader是 Qt 提供的枚举值，对应 HTTP 标准中的 Content-Type头
    req.setRawHeader("Idempotency-Key", task.measurement_uuid.toUtf8());                  // 通过 setRawHeader方法设置了一个自定义头部，setRawHeader用于设置那些不在 Qt 预定义枚举范围内的头部，设置请求头的 Idempotency-Key 为任务的 measurement_uuid，用于实现幂等性，防止重复上传相同数据。
    if (!cfg_.mes.auth_token.trimmed().isEmpty())
    {
        req.setRawHeader("Authorization", QByteArray("Bearer ") + cfg_.mes.auth_token.toUtf8()); // 通过 setRawHeader方法设置了一个自定义头部，设置请求头的 Authorization 为 Bearer 加上配置文件中的 auth_token，用于进行 HTTP 基本认证，确保与 MES 服务器的安全通信。
    }

    const QByteArray body = task.payload_json.toUtf8();
    reply_ = nam_.post(req, body);

    connect(reply_, &QNetworkReply::finished, this, &MesWorker::onReplyFinished); // 连接 finished 信号，当 HTTP 请求完成时（无论成功或失败），会触发 onReplyFinished 槽函数，用于处理响应结果。
    timeoutTimer_.start(cfg_.mes.timeout_ms);                                     // 启动超时定时器，设置超时时间为配置文件中指定的 timeout_ms（毫秒），如果在这个时间内网络请求没有完成，就会触发 timeoutTimer_ 的 timeout 信号，从而调用 lambda 函数强制中断网络请求，防止请求卡死。
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
}
