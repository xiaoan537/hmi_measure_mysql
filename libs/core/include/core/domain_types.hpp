#pragma once
#include <QDateTime>
#include <QString>
#include <optional>

namespace core {

enum class TaskType { Normal, Retest, MilCheck, Manual };

enum class MeasureMode { Normal, Retest, Manual, MilCheck };

enum class ResultJudgement { Ok, Ng, Invalid, Aborted };

enum class ReviewStatus { Pending, Approved, Rejected, Waived };

enum class ReportType {
  FirstMeasure,
  Retest2Pass,
  Retest3Pass,
  Scrap,
  MilCheck,
  ManualSkip
};

enum class ReportStatus { Pending, Approved, Sent, Failed, Skipped };

struct PlcCycle {
  quint64 id = 0;
  QString cycle_uuid;
  quint64 meas_seq = 0;
  QString part_type;  // "A" / "B"
  int item_count = 0; // 0/1/2
  std::optional<int> machine_state;
  std::optional<int> step_state;
  QString source_mode; // AUTO / MANUAL / MIL_CHECK
  QString mailbox_header_json;
  QString mailbox_meta_json;
  QDateTime measured_at_utc;
  std::optional<QDateTime> acked_at_utc;
  QDateTime created_at_utc;
};

struct PlcCycleItem {
  quint64 id = 0;
  quint64 plc_cycle_id = 0;
  int item_index = 0; // 0 / 1
  std::optional<int> slot_index;
  QString part_id;
  std::optional<bool> result_ok;
  QString fail_reason_code;
  QString fail_reason_text;
  bool is_valid = true;
  std::optional<quint64> measurement_id;
  QDateTime created_at_utc;
};

struct MeasurementEx {
  quint64 id = 0;
  QString measurement_uuid;

  std::optional<quint64> plc_cycle_id;
  std::optional<quint64> plc_cycle_item_id;
  std::optional<quint64> task_id;
  std::optional<quint64> task_item_id;

  QString part_id;
  QString part_type; // "A" / "B"
  QString slot_id;
  std::optional<int> slot_index;
  std::optional<int> item_index;

  QString measure_mode; // NORMAL / RETEST / MANUAL / MIL_CHECK
  int measure_round = 1;
  QString result_judgement; // OK / NG / INVALID / ABORTED
  QString upload_kind;

  QDateTime measured_at_utc;
  QString operator_id;

  QString review_status = "PENDING";
  QString reviewer_id;
  std::optional<QDateTime> reviewed_at_utc;
  QString review_note;

  QString fail_reason_code;
  QString fail_reason_text;

  QString status; // NEW / READY / REPORTED / ARCHIVED
  QDateTime created_at_utc;
  QDateTime updated_at_utc;
};

struct MeasurementResultEx {
  quint64 id = 0;
  quint64 measurement_id = 0;

  std::optional<double> total_len_mm;
  std::optional<double> ad_len_mm;
  std::optional<double> bc_len_mm;

  std::optional<double> id_left_mm;
  std::optional<double> id_right_mm;
  std::optional<double> od_left_mm;
  std::optional<double> od_right_mm;

  std::optional<double> runout_left_mm;
  std::optional<double> runout_right_mm;

  QString tolerance_json;
  QString extra_json;
  QDateTime created_at_utc;
};

} // namespace core