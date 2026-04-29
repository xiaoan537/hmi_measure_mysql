#pragma once

#include <QDateTime>
#include <QString>
#include <QVariant>
#include <QVector>

namespace core {

class Db;

struct IngestCycleInput {
  QString cycle_uuid; // 可空，服务内自动生成
  QString part_type;   // "A" / "B"
  int item_count = 0;  // 0 / 1 / 2
  QString source_mode; // AUTO / MANUAL / MIL_CHECK
  QDateTime measured_at_utc;

  QVariant machine_state;
  QVariant step_state;

  QString mailbox_header_json;
  QString mailbox_meta_json;
};

struct IngestItemInput {
  QString measurement_uuid; // 可空；写 RAW 时传入同一个 UUID，保证 raw_file_index 可追溯
  int item_index = 0; // 0 / 1
  QVariant slot_index;
  QString slot_id;
  QString part_id;

  QVariant result_ok; // 可空，兼容 header.ok[i]
  QString fail_reason_code;
  QString fail_reason_text;
  bool is_valid = true;

  QString run_kind = "PRODUCTION"; // PRODUCTION / CALIBRATION
  QString measure_mode;              // NORMAL / SECOND / THIRD / MIL；标定时可空
  QString attempt_kind = "PRIMARY";  // PRIMARY / RETEST
  int measure_round = 1;             // 兼容字段：1 / 2 / 3 / 9
  QString result_judgement;          // OK / NG / INVALID / ABORTED
  QString fail_class;                // LENGTH / GEOMETRY / MIXED
  QString upload_kind;               // FIRST_MEASURE / ...

  QVariant task_id;
  QVariant task_item_id;
  QString operator_id;

  QString review_status = "PENDING";
  bool is_effective = true;
  QVariant superseded_by;
  QString status = "READY"; // NEW / READY / REPORTED / ARCHIVED
};

struct IngestResultInput {
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

struct IngestRawInput {
  bool enabled = false;

  QString file_path;
  quint64 file_size_bytes = 0;
  int format_version = 0;
  quint64 file_crc32 = 0;
  quint64 chunk_mask = 0;

  QString scan_kind; // "A" / "B"
  int main_channels = 0;
  int rings = 0;
  int points_per_ring = 0;
  double angle_step_deg = 0.0;

  QString meta_json;
  QString raw_kind; // MAILBOX_V2 / DERIVED / EXPORT
};

struct IngestReportInput {
  bool create_mes_report = false;

  QString report_uuid; // 可空，服务内自动生成
  QString report_type; // FIRST_MEASURE / RETEST_2_PASS / ...
  QString interface_code;
  QString business_key;
  bool need_upload = true;
  QString report_status = "PENDING";
  QString payload_json;
};

struct MeasurementIngestRequest {
  IngestCycleInput cycle;
  QVector<IngestItemInput> items;
  QVector<IngestResultInput> results; // 可空；非空时必须和 items 一一对应
  QVector<IngestRawInput> raws; // 可空；非空时必须和 items 一一对应
  QVector<IngestReportInput> reports; // 可空；非空时必须和 items 一一对应
};

struct MeasurementIngestItemResult {
  quint64 plc_cycle_item_id = 0;
  quint64 measurement_id = 0;
  quint64 measurement_result_id = 0;
  bool raw_written = false;
  bool report_created = false;
  quint64 mes_report_id = 0;
};

struct MeasurementIngestResponse {
  quint64 plc_cycle_id = 0;
  QVector<MeasurementIngestItemResult> items;
};

class MeasurementIngestService {
public:
  explicit MeasurementIngestService(Db &db);

  bool ingest(const MeasurementIngestRequest &req,
              MeasurementIngestResponse *resp, QString *err);

private:
  bool validateRequest(const MeasurementIngestRequest &req, QString *err) const;

private:
  Db &db_;
};

} // namespace core
