#pragma once
#include <QDateTime>
#include <QSqlDatabase> // 包含 QSqlDatabase 类，QT 的数据库连接类
#include <QString>
#include <QVariant>
#include <QVector>
#include <QtGlobal>

#include "core/config.hpp" // 使用 DbConfig 配置信息
#include "core/model.hpp"  // 使用 MeasureResult 数据模型
#include "core/raw_v2.hpp"
// #include "core/raw_writer.hpp"

namespace core {
// 表示一个测量结果及其MES上传状态的综合信息。这个结构体用于在UI层显示测量结果列表，包括其上传状态和相关信息。
struct MesUploadRow {
  quint64 mes_report_id = 0;
  QString report_uuid;
  QString measurement_uuid;
  QString part_id;
  QString part_type;
  QString task_card_no;
  QString interface_code;
  QString run_kind;      // PRODUCTION / CALIBRATION
  QString measure_mode;  // NORMAL / SECOND / THIRD / MIL
  QString attempt_kind;  // PRIMARY / RETEST
  bool is_effective = true;
  bool ok = false;
  QDateTime measured_at_utc;
  double total_len_mm = 0.0;
  double bc_len_mm = 0.0;

  QString report_status;
  QString mes_status; // NOT_QUEUED/PENDING/SENDING/SENT/FAILED
  int attempt_count = 0;
  QString last_error;
  QDateTime mes_updated_at_utc;
};

struct MeasurementListRowEx {
  quint64 measurement_id = 0;
  QString measurement_uuid;

  QString part_id;
  QString part_type;
  QString slot_id;
  int slot_index = -1;

  QString task_card_no;
  QString run_kind;         // PRODUCTION / CALIBRATION
  QString measure_mode;     // NORMAL / SECOND / THIRD / MIL
  QString attempt_kind;     // PRIMARY / RETEST
  int measure_round = 1;    // 兼容字段
  QString result_judgement; // OK / NG / INVALID / ABORTED
  QString fail_class;       // LENGTH / GEOMETRY / MIXED
  QString review_status;    // PENDING / APPROVED / ...
  bool is_effective = true;
  QVariant superseded_by;

  QDateTime measured_at_utc;

  double total_len_mm = 0.0;
  double ad_len_mm = 0.0;
  double bc_len_mm = 0.0;
  double id_left_mm = 0.0;
  double id_right_mm = 0.0;
  double od_left_mm = 0.0;
  double od_right_mm = 0.0;
  double runout_left_mm = 0.0;
  double runout_right_mm = 0.0;

  bool has_total_len = false;
  bool has_ad_len = false;
  bool has_bc_len = false;
  bool has_id_left = false;
  bool has_id_right = false;
  bool has_od_left = false;
  bool has_od_right = false;
  bool has_runout_left = false;
  bool has_runout_right = false;
};

struct MeasurementDetailEx {
  bool found = false;

  quint64 measurement_id = 0;
  QString measurement_uuid;

  QVariant plc_cycle_id;
  QVariant plc_cycle_item_id;
  QVariant task_id;
  QVariant task_item_id;

  QString part_id;
  QString part_type;
  QString slot_id;
  QVariant slot_index;
  QVariant item_index;

  QString task_card_no;
  QString run_kind;
  QString measure_mode;
  QString attempt_kind;
  int measure_round = 1; // 兼容字段
  QString result_judgement;
  QString fail_class;
  bool is_effective = true;
  QVariant superseded_by;
  QString upload_kind;

  QDateTime measured_at_utc;
  QString operator_id;

  QString review_status;
  QString reviewer_id;
  QDateTime reviewed_at_utc;
  QString review_note;

  QString fail_reason_code;
  QString fail_reason_text;
  QString status;

  QVariant total_len_mm;
  QVariant ad_len_mm;
  QVariant bc_len_mm;
  QVariant id_left_mm;
  QVariant id_right_mm;
  QVariant od_left_mm;
  QVariant od_right_mm;
  QVariant runout_left_mm;
  QVariant runout_right_mm;

  QString tolerance_json;
  QString extra_json;
};

struct MeasurementQueryFilter {
  QDateTime from_utc;
  QDateTime to_utc;

  QString part_id_like;
  QString task_card_no_like;

  QString part_type;        // "", "A", "B"
  QString run_kind;         // "", "PRODUCTION", "CALIBRATION"
  QString measure_mode;     // "", "NORMAL", "SECOND", "THIRD", "MIL"
  QString attempt_kind;     // "", "PRIMARY", "RETEST"
  QString result_judgement; // "", "OK", "NG", ...

  int effective_only = -1;  // -1=all, 1=only effective, 0=only superseded
};

// 定义查询测量结果时的过滤条件。用户可以通过这个结构体指定时间范围、工件类型、合格性和MES状态等条件来筛选需要查看的测量结果。
struct MesUploadFilter {
  QDateTime from_utc;   // 查询起始时间
  QDateTime to_utc;     // 查询结束时间
  QString part_id_like; // optional，工件ID模糊匹配（可选）
  QString task_card_no_like; // optional，任务卡号模糊匹配
  QString part_type; // "", "A", "B"，工件类型过滤:""（全部）、"A"、"B"
  int ok_filter =
      -1; // -1=all, 1=ok, 0=ng,合格性过滤：-1（全部）、1（合格）、0（不合格）
  QString mes_status; // "", "NOT_QUEUED", "PENDING",
                      // ...，MES状态过滤：""（全部）、"NOT_QUEUED"、"PENDING"等
};

// 表示一个待上传的MES任务。这个结构体主要用于上传工作线程（Worker）获取和处理上传任务。payload_json字段包含了要发送给MES系统的完整数据。
struct MesOutboxTask {
  quint64 id = 0;             // 任务ID，数据库主键
  quint64 mes_report_id = 0;  // 关联的上报对象ID
  QString report_uuid;        // 上报对象UUID
  QString measurement_uuid;   // 关联的测量UUID
  QString interface_code;     // 路由到哪个 MES 接口
  QString business_key;       // 幂等业务键
  QString payload_json;       // 任务负载，JSON 格式字符串
  int attempt_count = 0;      // 重试次数，初始为 0
};

class Db {
public:
  bool open(const DbConfig &cfg, QString *err); // 打开数据库，建立数据库连接
  bool ensureSchema(QString *err); // 确保数据库表结构存在
  bool insertResult(const MeasureResult &r,
                    QString *err); // 插入测量结果到数据库

  // 插入结果 + raw 索引
  // bool insertResultWithRawIndex(const MeasureResult& r, const RawWriteInfo&
  // raw, QString* err);
  bool insertResultWithRawIndexV2(const MeasureResult &r,
                                  const RawWriteInfoV2 &raw, QString *err);

  // --- MES manual upload ，查询上传记录---
  // 根据过滤条件查询测量结果及其MES上传状态。返回一个MesUploadRow数组，每个元素包含一个测量结果的完整信息和上传状态。
  QVector<MesUploadRow> queryMesUploadRows(const MesUploadFilter &f, int limit,
                                           QString *err);

  // 将指定的测量结果加入MES上传队列。如果该测量结果已经成功上传（状态为SENT），则跳过并返回false。这个函数通常由用户手动触发，用于将选中的测量结果加入上传队列。
  bool queueMesUploadByUuid(const QString &measurement_uuid, QString *err);

  // 重试失败的上传任务。将状态为FAILED的任务重新设置为PENDING状态，但不重建payload。用于"重试失败"按钮功能，返回成功重置的任务数量。
  int retryFailed(const QVector<QString> &uuids, QString *err);

  // Worker side
  // 重置长时间处于发送状态的任务。如果某个任务处于SENDING状态超过指定时间（stale_seconds），则将其重置为PENDING状态，防止因异常情况导致的任务卡死。
  bool resetStaleSending(int stale_seconds, QString *err);
  // 获取下一个待处理的上传任务。上传工作线程调用此函数获取下一个需要上传的任务。函数会查找状态为PENDING且到达重试时间的任务。
  bool fetchNextDueOutbox(MesOutboxTask *task, QString *err);
  // 标记任务为发送中状态。在开始上传前调用，将任务状态从PENDING改为SENDING，防止其他工作线程同时处理同一任务。
  bool markOutboxSending(quint64 id, QString *err);
  // 标记任务为已发送状态。上传成功后调用，记录HTTP响应码和响应内容，将任务状态设置为SENT。
  bool markOutboxSent(quint64 id, int http_code, const QString &resp,
                      QString *err);
  // 标记任务为失败状态。上传失败后调用，记录错误信息和下次重试时间，将任务状态设置为FAILED。
  bool markOutboxFailed(quint64 id, int http_code, const QString &resp,
                        const QString &error, int next_retry_seconds,
                        QString *err);

  bool insertPlcCycle(const QString &cycle_uuid, const QString &part_type,
                      int item_count,
                      const QString &source_mode,
                      const QString &mailbox_header_json,
                      const QString &mailbox_meta_json,
                      const QDateTime &measured_at_utc, quint64 *new_id,
                      QString *err);

  bool insertPlcCycleItem(quint64 plc_cycle_id, int item_index,
                          const QVariant &slot_index, const QString &part_id,
                          const QVariant &result_ok,
                          const QString &fail_reason_code,
                          const QString &fail_reason_text, bool is_valid,
                          quint64 *new_id, QString *err);

  bool insertMeasurementEx(
      const QString &measurement_uuid, const QVariant &plc_cycle_id,
      const QVariant &plc_cycle_item_id, const QVariant &task_id,
      const QVariant &task_item_id, const QString &part_id,
      const QString &part_type, const QString &slot_id,
      const QVariant &slot_index, const QVariant &item_index,
      const QString &measure_mode, int measure_round,
      const QString &result_judgement, const QString &upload_kind,
      const QDateTime &measured_at_utc, const QString &operator_id,
      const QString &review_status, const QString &fail_reason_code,
      const QString &fail_reason_text, const QString &status, quint64 *new_id,
      QString *err, const QString &run_kind = QStringLiteral("PRODUCTION"),
      const QString &attempt_kind = QStringLiteral("PRIMARY"),
      const QString &fail_class = QString(),
      bool is_effective = true,
      const QVariant &superseded_by = QVariant());

  bool insertMeasurementResultEx(
      quint64 measurement_id, const QVariant &total_len_mm,
      const QVariant &ad_len_mm, const QVariant &bc_len_mm,
      const QVariant &id_left_mm, const QVariant &id_right_mm,
      const QVariant &od_left_mm, const QVariant &od_right_mm,
      const QVariant &runout_left_mm, const QVariant &runout_right_mm,
      const QString &tolerance_json, const QString &extra_json, quint64 *new_id,
      QString *err);

  bool createMesReport(quint64 measurement_id, const QVariant &task_id,
                       const QVariant &task_item_id, const QString &report_uuid,
                       const QString &report_type,
                       const QString &interface_code,
                       const QString &business_key, bool need_upload,
                       const QString &report_status,
                       const QString &payload_json, quint64 *new_id,
                       QString *err);

  bool beginTx(QString *err);
  bool commitTx(QString *err);
  bool rollbackTx(QString *err);

  bool bindCycleItemMeasurement(quint64 plc_cycle_item_id,
                                quint64 measurement_id, QString *err);

  QVector<MeasurementListRowEx> queryLatestMeasurementsEx(int limit,
                                                          QString *err);
  QVector<MeasurementListRowEx> queryMeasurementsEx(
      const MeasurementQueryFilter &f, int limit, QString *err);

  bool getMeasurementDetailExById(quint64 measurement_id,
                                  MeasurementDetailEx *out, QString *err);

  bool insertRawFileIndexForMeasurement(
      const QString &measurement_uuid, quint64 measurement_id,
      const QVariant &plc_cycle_id, const QString &file_path,
      quint64 file_size_bytes, int format_version, quint64 file_crc32,
      quint64 chunk_mask, const QString &scan_kind, int main_channels,
      int rings, int points_per_ring, double angle_step_deg,
      const QString &meta_json, const QString &raw_kind, QString *err);

private:
  QSqlDatabase db_; // QT 数据库连接对象 （私有，隐藏实现细节）
};

} // namespace core
