#include "mes_worker.hpp"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

MesWorker::MesWorker(const core::AppConfig &cfg, QObject *parent)
    : QObject(parent), cfg_(cfg)
{
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, &MesWorker::onTick);

    timeoutTimer_.setSingleShot(true);
    connect(&timeoutTimer_, &QTimer::timeout, this, [this]()
            {
    if (reply_) reply_->abort(); });
}

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

void MesWorker::kick()
{
    // 立刻跑一次
    onTick();
}

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

void MesWorker::onReplyFinished()
{
    timeoutTimer_.stop();

    const int httpCode = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString respBody = QString::fromUtf8(reply_->readAll());
    const QString netErr = reply_->error() == QNetworkReply::NoError ? "" : reply_->errorString();

    reply_->deleteLater();
    reply_ = nullptr;

    QString e;
    const bool ok = (netErr.isEmpty() && httpCode >= 200 && httpCode < 300);

    if (ok)
    {
        if (!db_.markOutboxSent(current_.id, httpCode, respBody, &e))
        {
            emit logMessage("markOutboxSent failed: " + e);
        }
    }
    else
    {
        const int backoff = computeBackoffSeconds(current_.attempt_count);
        const QString errMsg = netErr.isEmpty() ? QString("HTTP %1").arg(httpCode) : netErr;
        if (!db_.markOutboxFailed(current_.id, httpCode, respBody, errMsg, backoff, &e))
        {
            emit logMessage("markOutboxFailed failed: " + e);
        }
    }

    busy_ = false;
    emit outboxChanged();
}
