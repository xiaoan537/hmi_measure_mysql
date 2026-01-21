#pragma once
#include <QString>
#include <QSqlDatabase> // 包含 QSqlDatabase 类，QT 的数据库连接类
#include <QVector>
#include <QDateTime>
#include <QtGlobal>

#include "core/model.hpp"  // 使用 MeasureResult 数据模型
#include "core/config.hpp" // 使用 DbConfig 配置信息
#include "core/raw_v2.hpp"
// #include "core/raw_writer.hpp"

namespace core
{
    struct MesUploadRow
    {
        QString measurement_uuid;
        QString part_id;
        QString part_type;
        bool ok = false;
        QDateTime measured_at_utc;
        double total_len_mm = 0.0;
        double bc_len_mm = 0.0;

        QString mes_status; // NOT_QUEUED/PENDING/SENDING/SENT/FAILED
        int attempt_count = 0;
        QString last_error;
        QDateTime mes_updated_at_utc;
    };

    struct MesUploadFilter
    {
        QDateTime from_utc;
        QDateTime to_utc;
        QString part_id_like; // optional
        QString part_type;    // "", "A", "B"
        int ok_filter = -1;   // -1=all, 1=ok, 0=ng
        QString mes_status;   // "", "NOT_QUEUED", "PENDING", ...
    };

    struct MesOutboxTask
    {
        quint64 id = 0;
        QString measurement_uuid;
        QString payload_json;
        int attempt_count = 0;
    };

    class Db
    {
    public:
        bool open(const DbConfig &cfg, QString *err);            // 打开数据库，建立数据库连接
        bool ensureSchema(QString *err);                         // 确保数据库表结构存在
        bool insertResult(const MeasureResult &r, QString *err); // 插入测量结果到数据库

        // 插入结果 + raw 索引
        // bool insertResultWithRawIndex(const MeasureResult& r, const RawWriteInfo& raw, QString* err);
        bool insertResultWithRawIndexV2(const MeasureResult &r, const RawWriteInfoV2 &raw, QString *err);

        // --- MES manual upload ---
        QVector<MesUploadRow> queryMesUploadRows(const MesUploadFilter &f, int limit, QString *err);

        // 把某条 measurement_uuid 入队（若已 SENT 则跳过返回 false 并给 err 说明）
        bool queueMesUploadByUuid(const QString &measurement_uuid, QString *err);

        // 将 FAILED 变回 PENDING（用于“重试失败”按钮），不重建 payload
        int retryFailed(const QVector<QString> &uuids, QString *err);

        // Worker side
        bool resetStaleSending(int stale_seconds, QString *err);
        bool fetchNextDueOutbox(MesOutboxTask *task, QString *err);
        bool markOutboxSending(quint64 id, QString *err);
        bool markOutboxSent(quint64 id, int http_code, const QString &resp, QString *err);
        bool markOutboxFailed(quint64 id, int http_code, const QString &resp,
                              const QString &error, int next_retry_seconds, QString *err);

    private:
        QSqlDatabase db_; // QT 数据库连接对象 （私有，隐藏实现细节）
    };

} // namespace core
