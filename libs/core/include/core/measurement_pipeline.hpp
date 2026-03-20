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

// 仅表达 PLC 冻结出来的原始测量包，不携带业务上下文。
struct PlcMailboxSnapshot {
  quint32 meas_seq = 0;
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

// 仅表达 PC 在发起测量前就已知的业务上下文。
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

// 计算层统一输入：PLC 原始测量事实 + PC 已知业务事实。


// 对应 PLC Mailbox 的“逻辑原始块”：header + 线性展开 arrays。
// arrays_um 的顺序约定：item0 完整块，再 item1 完整块，不交织；
// item 内顺序：ring -> channel -> point。
struct PlcMailboxRawFrame {
  PlcMailboxHeaderV2 header;
  QVector<float> arrays_um;
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

QChar mailboxPartTypeFromHeader(quint16 plcPartType);
int expectedMailboxPointCountPerItem(const PlcMailboxHeaderV2 &header);
int expectedMailboxUsedPointCount(const PlcMailboxHeaderV2 &header);

bool buildPlcMailboxSnapshot(const PlcMailboxHeaderV2 &header,
                             const QVector<float> &arrays_um,
                             PlcMailboxSnapshot *out,
                             QString *err = nullptr);
bool buildPlcMailboxSnapshot(const PlcMailboxRawFrame &frame,
                             PlcMailboxSnapshot *out,
                             QString *err = nullptr);

QJsonObject toJson(const PlcMailboxSnapshot &snapshot);
QJsonObject toJson(const MeasurementContext &context);
QJsonObject toJson(const MeasurementComputeResult &result);

} // namespace core
