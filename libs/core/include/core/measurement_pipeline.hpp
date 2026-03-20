#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QtGlobal>

#include "core/measurement_ingest.hpp"
#include "core/plc_contract_v2.hpp"
#include "core/raw_v2.hpp"
#include "core/snapshot.hpp"

namespace core {

enum class BusinessRunKind {
  Production = 0,
  Calibration = 1,
};

enum class BusinessMeasureMode {
  Unknown = 0,
  Normal,
  Second,
  Third,
  Mil,
};

enum class BusinessAttemptKind {
  Primary = 0,
  Retest = 1,
};

enum class MeasurementFailClass {
  None = 0,
  Length,
  Geometry,
  Mixed,
};

enum class MeasurementJudgement {
  Unknown = 0,
  Ok,
  Ng,
  Invalid,
  Aborted,
};

struct PlcMailboxItemSnapshot {
  bool present = false;
  int item_index = -1;
  int slot_index = -1;
  QString part_id;

  float total_len_mm = qQNaN();
  float ad_len_mm = qQNaN();
  float bc_len_mm = qQNaN();

  // 线性展开顺序：ring -> channel -> point
  QVector<float> raw_points_um;
};

struct PlcMailboxSnapshot {
  quint32 meas_seq = 0;
  PlcRunKind run_kind = PlcRunKind::None;
  QChar part_type = QChar('A');
  int item_count = 0;

  quint16 raw_layout_ver = 1;
  int ring_count = 0;
  int point_count = 0;
  int channel_count = 0;

  QVector<PlcMailboxItemSnapshot> items;

  bool isFrozen() const { return meas_seq != 0; }
  bool isPartTypeA() const { return part_type.toUpper() == QChar('A'); }
  bool isPartTypeB() const { return part_type.toUpper() == QChar('B'); }
  int expectedChannels() const { return isPartTypeA() ? 4 : (isPartTypeB() ? 2 : 0); }
  int expectedPointCountPerItem() const { return ring_count * point_count * channel_count; }

  bool isValid(QString *err = nullptr) const;
  const PlcMailboxItemSnapshot *findItem(int itemIndex) const;
};

struct MeasurementContext {
  BusinessRunKind run_kind = BusinessRunKind::Production;
  BusinessMeasureMode measure_mode = BusinessMeasureMode::Unknown;
  BusinessAttemptKind attempt_kind = BusinessAttemptKind::Primary;

  QString operator_id;
  QVariant task_id;
  QVariant task_item_id;
  QString source_mode = QStringLiteral("AUTO");
  QDateTime measured_at_utc;

  int calibration_slot_index = kCalibrationSlotIndex;
  QString calibration_type;            // "A" / "B"
  QString calibration_master_part_id; // 标定件主数据，可空

  bool isValid(QString *err = nullptr) const;
};

struct MeasurementComputeInput {
  PlcMailboxSnapshot snapshot;
  MeasurementContext context;

  bool isValid(QString *err = nullptr) const;
};

struct MeasurementValues {
  float total_len_mm = qQNaN();
  float ad_len_mm = qQNaN();
  float bc_len_mm = qQNaN();

  float id_left_mm = qQNaN();
  float id_right_mm = qQNaN();
  float od_left_mm = qQNaN();
  float od_right_mm = qQNaN();

  float runout_left_mm = qQNaN();
  float runout_right_mm = qQNaN();
};

struct MeasurementComputeResult {
  bool valid = false;
  MeasurementJudgement judgement = MeasurementJudgement::Unknown;
  MeasurementFailClass fail_class = MeasurementFailClass::None;

  QString fail_reason_code;
  QString fail_reason_text;
  MeasurementValues values;

  QString tolerance_json;
  QString extra_json;
};

class IMeasurementAlgorithm {
public:
  virtual ~IMeasurementAlgorithm() = default;
  virtual QString name() const = 0;
  virtual bool compute(const MeasurementComputeInput &input, int itemIndex,
                       MeasurementComputeResult *out,
                       QString *err = nullptr) const = 0;
};

struct ProductionSlotSummary {
  int slot_index = -1;
  QString part_id;
  QChar part_type = QChar('A');

  bool valid = false;
  bool judgement_known = false;
  bool judgement_ok = false;
  QString fail_reason_text;

  MeasurementComputeResult compute;
};

struct CalibrationSlotSummary {
  int slot_index = kCalibrationSlotIndex;
  QString calibration_type;            // "A" / "B"
  QString calibration_master_part_id; // PC 主数据
  QString measured_part_id;           // Mailbox 中读到的 ID，可空

  bool valid = false;
  bool judgement_known = false;
  bool judgement_ok = false;
  QString fail_reason_text;

  MeasurementComputeResult compute;
};

struct RawLoopItemBuildResult {
  MeasurementSnapshot raw_snapshot;
  IngestItemInput ingest_item;
  IngestResultInput ingest_result;
};

QString toString(BusinessRunKind v);
QString toString(BusinessMeasureMode v);
QString toString(BusinessAttemptKind v);
QString toString(MeasurementFailClass v);
QString toString(MeasurementJudgement v);

BusinessMeasureMode businessMeasureModeFromString(const QString &text);
MeasurementJudgement measurementJudgementFromBool(bool ok);
MeasurementFailClass measurementFailClassFromString(const QString &text);

MeasurementComputeResult makePlaceholderComputeResult(const MeasurementComputeInput &input,
                                                      int itemIndex);
ProductionSlotSummary makeProductionSlotSummary(const MeasurementComputeInput &input,
                                                int itemIndex,
                                                const MeasurementComputeResult &result);
CalibrationSlotSummary makeCalibrationSlotSummary(const MeasurementComputeInput &input,
                                                  int itemIndex,
                                                  const MeasurementComputeResult &result);

bool buildRawLoopItem(const MeasurementComputeInput &input, int itemIndex,
                      const MeasurementComputeResult &result,
                      const QString &measurementUuid,
                      RawLoopItemBuildResult *out,
                      QString *err = nullptr);

QJsonObject toJson(const PlcMailboxSnapshot &snapshot);
QJsonObject toJson(const MeasurementContext &context);
QJsonObject toJson(const MeasurementComputeResult &result);

} // namespace core
