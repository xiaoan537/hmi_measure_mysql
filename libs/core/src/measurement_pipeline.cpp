#include "core/measurement_pipeline.hpp"

#include <QJsonValue>
#include <QUuid>

namespace core {
namespace {

QString failWith(QString *err, const QString &text) {
  if (err) *err = text;
  return text;
}

QString normalizedPartType(QChar partType) {
  const QChar ch = partType.toUpper();
  if (ch == QChar('A') || ch == QChar('B')) return QString(ch);
  return QString();
}

QJsonArray toJsonFloatArray(const QVector<float> &values) {
  QJsonArray arr;
  for (float v : values) {
    if (qIsNaN(v) || qIsInf(v)) {
      arr.push_back(QStringLiteral("NaN"));
    } else {
      arr.push_back(v);
    }
  }
  return arr;
}

QString uploadKindFrom(const MeasurementContext &context,
                       const MeasurementComputeResult &result) {
  if (context.run_kind == BusinessRunKind::Calibration) {
    return QStringLiteral("CALIBRATION_ONLY");
  }
  if (context.attempt_kind == BusinessAttemptKind::Retest) {
    return result.judgement == MeasurementJudgement::Ok
               ? QStringLiteral("RETEST_PASS")
               : QStringLiteral("RETEST_NG");
  }
  if (context.measure_mode == BusinessMeasureMode::Mil) {
    return QStringLiteral("MIL_CHECK");
  }
  return QStringLiteral("FIRST_MEASURE");
}

int measureRoundFrom(const MeasurementContext &context) {
  switch (context.measure_mode) {
  case BusinessMeasureMode::Second:
    return 2;
  case BusinessMeasureMode::Third:
    return 3;
  case BusinessMeasureMode::Mil:
    return 9;
  case BusinessMeasureMode::Normal:
  case BusinessMeasureMode::Unknown:
  default:
    return 1;
  }
}

QString resultTextFrom(const MeasurementComputeResult &result) {
  return toString(result.judgement);
}

} // namespace

bool PlcMailboxSnapshot::isValid(QString *err) const {
  const QString pt = normalizedPartType(part_type);
  if (pt.isEmpty()) {
    failWith(err, QStringLiteral("snapshot.part_type 必须是 A 或 B"));
    return false;
  }
  if (item_count < 1 || item_count > 2) {
    failWith(err, QStringLiteral("snapshot.item_count 必须是 1 或 2"));
    return false;
  }
  if (ring_count <= 0 || point_count <= 0 || channel_count <= 0) {
    failWith(err, QStringLiteral("snapshot.ring_count / point_count / channel_count 必须大于 0"));
    return false;
  }
  if (channel_count != expectedChannels()) {
    failWith(err, QStringLiteral("snapshot.channel_count 与 part_type 不匹配"));
    return false;
  }
  if (items.size() < item_count) {
    failWith(err, QStringLiteral("snapshot.items 数量不足"));
    return false;
  }

  const int expectPoints = expectedPointCountPerItem();
  for (int i = 0; i < item_count; ++i) {
    const auto &item = items.at(i);
    if (!item.present) {
      failWith(err, QStringLiteral("snapshot.items[%1] 未标记 present").arg(i));
      return false;
    }
    if (item.item_index != i) {
      failWith(err, QStringLiteral("snapshot.items[%1].item_index 不匹配").arg(i));
      return false;
    }
    if (item.slot_index < 0 || item.slot_index >= kLogicalSlotCount) {
      failWith(err, QStringLiteral("snapshot.items[%1].slot_index 超出范围").arg(i));
      return false;
    }
    if (item.raw_points_um.size() != expectPoints) {
      failWith(err, QStringLiteral("snapshot.items[%1].raw_points_um 长度不正确，期望 %2，实际 %3")
                        .arg(i)
                        .arg(expectPoints)
                        .arg(item.raw_points_um.size()));
      return false;
    }
  }

  return true;
}

const PlcMailboxItemSnapshot *PlcMailboxSnapshot::findItem(int itemIndex) const {
  for (const auto &item : items) {
    if (item.item_index == itemIndex) return &item;
  }
  return nullptr;
}

bool MeasurementContext::isValid(QString *err) const {
  if (!measured_at_utc.isValid()) {
    failWith(err, QStringLiteral("context.measured_at_utc 不能为空"));
    return false;
  }
  if (run_kind == BusinessRunKind::Production &&
      measure_mode == BusinessMeasureMode::Unknown) {
    failWith(err, QStringLiteral("生产测量 context.measure_mode 不能为空"));
    return false;
  }
  if (run_kind == BusinessRunKind::Calibration) {
    if (calibration_slot_index != kCalibrationSlotIndex) {
      failWith(err, QStringLiteral("标定 context.calibration_slot_index 必须为 15"));
      return false;
    }
    if (calibration_type.trimmed().isEmpty()) {
      failWith(err, QStringLiteral("标定 context.calibration_type 不能为空"));
      return false;
    }
  }
  return true;
}

bool MeasurementComputeInput::isValid(QString *err) const {
  if (!snapshot.isValid(err)) return false;
  if (!context.isValid(err)) return false;

  if (context.run_kind == BusinessRunKind::Calibration) {
    if (snapshot.item_count != 1) {
      failWith(err, QStringLiteral("标定测量要求 snapshot.item_count = 1"));
      return false;
    }
    const auto *item0 = snapshot.findItem(0);
    if (!item0) {
      failWith(err, QStringLiteral("标定测量必须存在 item0"));
      return false;
    }
    if (item0->slot_index != context.calibration_slot_index) {
      failWith(err, QStringLiteral("标定测量要求 snapshot.slot_index[0] 与 context.calibration_slot_index 一致"));
      return false;
    }
  }
  return true;
}

QString toString(BusinessRunKind v) {
  switch (v) {
  case BusinessRunKind::Calibration:
    return QStringLiteral("CALIBRATION");
  case BusinessRunKind::Production:
  default:
    return QStringLiteral("PRODUCTION");
  }
}

QString toString(BusinessMeasureMode v) {
  switch (v) {
  case BusinessMeasureMode::Normal:
    return QStringLiteral("NORMAL");
  case BusinessMeasureMode::Second:
    return QStringLiteral("SECOND");
  case BusinessMeasureMode::Third:
    return QStringLiteral("THIRD");
  case BusinessMeasureMode::Mil:
    return QStringLiteral("MIL");
  case BusinessMeasureMode::Unknown:
  default:
    return QString();
  }
}

QString toString(BusinessAttemptKind v) {
  switch (v) {
  case BusinessAttemptKind::Retest:
    return QStringLiteral("RETEST");
  case BusinessAttemptKind::Primary:
  default:
    return QStringLiteral("PRIMARY");
  }
}

QString toString(MeasurementFailClass v) {
  switch (v) {
  case MeasurementFailClass::Length:
    return QStringLiteral("LENGTH");
  case MeasurementFailClass::Geometry:
    return QStringLiteral("GEOMETRY");
  case MeasurementFailClass::Mixed:
    return QStringLiteral("MIXED");
  case MeasurementFailClass::None:
  default:
    return QString();
  }
}

QString toString(MeasurementJudgement v) {
  switch (v) {
  case MeasurementJudgement::Ok:
    return QStringLiteral("OK");
  case MeasurementJudgement::Ng:
    return QStringLiteral("NG");
  case MeasurementJudgement::Invalid:
    return QStringLiteral("INVALID");
  case MeasurementJudgement::Aborted:
    return QStringLiteral("ABORTED");
  case MeasurementJudgement::Unknown:
  default:
    return QStringLiteral("UNKNOWN");
  }
}

BusinessMeasureMode businessMeasureModeFromString(const QString &text) {
  const QString v = text.trimmed().toUpper();
  if (v == QStringLiteral("NORMAL")) return BusinessMeasureMode::Normal;
  if (v == QStringLiteral("SECOND")) return BusinessMeasureMode::Second;
  if (v == QStringLiteral("THIRD")) return BusinessMeasureMode::Third;
  if (v == QStringLiteral("MIL")) return BusinessMeasureMode::Mil;
  return BusinessMeasureMode::Unknown;
}

MeasurementJudgement measurementJudgementFromBool(bool ok) {
  return ok ? MeasurementJudgement::Ok : MeasurementJudgement::Ng;
}

MeasurementFailClass measurementFailClassFromString(const QString &text) {
  const QString v = text.trimmed().toUpper();
  if (v == QStringLiteral("LENGTH")) return MeasurementFailClass::Length;
  if (v == QStringLiteral("GEOMETRY")) return MeasurementFailClass::Geometry;
  if (v == QStringLiteral("MIXED")) return MeasurementFailClass::Mixed;
  return MeasurementFailClass::None;
}

MeasurementComputeResult makePlaceholderComputeResult(const MeasurementComputeInput &input,
                                                      int itemIndex) {
  MeasurementComputeResult r;
  const auto *item = input.snapshot.findItem(itemIndex);
  if (!item) return r;

  r.valid = true;
  r.judgement = MeasurementJudgement::Unknown;
  if (input.snapshot.isPartTypeA()) {
    r.values.total_len_mm = item->total_len_mm;
  } else {
    r.values.ad_len_mm = item->ad_len_mm;
    r.values.bc_len_mm = item->bc_len_mm;
  }
  r.extra_json = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("source"), QStringLiteral("placeholder")},
                                                             {QStringLiteral("item_index"), itemIndex}})
                                       .toJson(QJsonDocument::Compact));
  return r;
}

ProductionSlotSummary makeProductionSlotSummary(const MeasurementComputeInput &input,
                                                int itemIndex,
                                                const MeasurementComputeResult &result) {
  ProductionSlotSummary s;
  const auto *item = input.snapshot.findItem(itemIndex);
  if (!item) return s;

  s.slot_index = item->slot_index;
  s.part_id = item->part_id;
  s.part_type = input.snapshot.part_type.toUpper();
  s.valid = result.valid;
  s.compute = result;
  s.judgement_known = result.judgement == MeasurementJudgement::Ok ||
                      result.judgement == MeasurementJudgement::Ng;
  s.judgement_ok = result.judgement == MeasurementJudgement::Ok;
  s.fail_reason_text = result.fail_reason_text;
  return s;
}

CalibrationSlotSummary makeCalibrationSlotSummary(const MeasurementComputeInput &input,
                                                  int itemIndex,
                                                  const MeasurementComputeResult &result) {
  CalibrationSlotSummary s;
  const auto *item = input.snapshot.findItem(itemIndex);
  if (!item) return s;

  s.slot_index = item->slot_index;
  s.calibration_type = input.context.calibration_type;
  s.calibration_master_part_id = input.context.calibration_master_part_id;
  s.measured_part_id = item->part_id;
  s.valid = result.valid;
  s.compute = result;
  s.judgement_known = result.judgement == MeasurementJudgement::Ok ||
                      result.judgement == MeasurementJudgement::Ng;
  s.judgement_ok = result.judgement == MeasurementJudgement::Ok;
  s.fail_reason_text = result.fail_reason_text;
  return s;
}

bool buildRawLoopItem(const MeasurementComputeInput &input, int itemIndex,
                      const MeasurementComputeResult &result,
                      const QString &measurementUuid,
                      RawLoopItemBuildResult *out,
                      QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("out 不能为空"));
    return false;
  }
  QString validErr;
  if (!input.isValid(&validErr)) {
    failWith(err, validErr);
    return false;
  }
  const auto *item = input.snapshot.findItem(itemIndex);
  if (!item) {
    failWith(err, QStringLiteral("未找到 item_index=%1").arg(itemIndex));
    return false;
  }

  MeasurementSnapshot raw;
  raw.measurement_uuid = measurementUuid.trimmed().isEmpty()
                             ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                             : measurementUuid.trimmed();
  raw.part_type = input.snapshot.part_type.toUpper();
  raw.measured_at_utc = input.context.measured_at_utc;
  raw.meta_json = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("meas_seq"), static_cast<qint64>(input.snapshot.meas_seq)},
                                                             {QStringLiteral("item_index"), itemIndex},
                                                             {QStringLiteral("slot_index"), item->slot_index},
                                                             {QStringLiteral("part_id"), item->part_id},
                                                             {QStringLiteral("run_kind"), toString(input.context.run_kind)},
                                                             {QStringLiteral("measure_mode"), toString(input.context.measure_mode)},
                                                             {QStringLiteral("attempt_kind"), toString(input.context.attempt_kind)}})
                                       .toJson(QJsonDocument::Compact));

  if (input.snapshot.isPartTypeA()) {
    raw.conf_spec.rings = input.snapshot.ring_count;
    raw.conf_spec.points_per_ring = input.snapshot.point_count;
    raw.conf_spec.angle_step_deg = input.snapshot.point_count > 0
                                       ? 360.0f / static_cast<float>(input.snapshot.point_count)
                                       : 0.0f;
    raw.conf_spec.order_code = 1;
    raw.confocal4 = item->raw_points_um;
    raw.gt2r_mm3 = {item->total_len_mm, qQNaN(), qQNaN()};
  } else {
    raw.run_spec.rings = input.snapshot.ring_count;
    raw.run_spec.points_per_ring = input.snapshot.point_count;
    raw.run_spec.angle_step_deg = input.snapshot.point_count > 0
                                      ? 360.0f / static_cast<float>(input.snapshot.point_count)
                                      : 0.0f;
    raw.run_spec.order_code = 1;
    raw.runout2 = item->raw_points_um;
    raw.gt2r_mm3 = {item->ad_len_mm, item->bc_len_mm, qQNaN()};
  }

  IngestItemInput ingestItem;
  ingestItem.item_index = itemIndex;
  ingestItem.slot_index = item->slot_index;
  ingestItem.slot_id = QStringLiteral("SLOT-%1").arg(item->slot_index, 2, 10, QLatin1Char('0'));
  ingestItem.part_id = item->part_id;
  ingestItem.result_ok = result.judgement == MeasurementJudgement::Unknown
                             ? QVariant()
                             : QVariant(result.judgement == MeasurementJudgement::Ok ? 1 : 0);
  ingestItem.fail_reason_code = result.fail_reason_code;
  ingestItem.fail_reason_text = result.fail_reason_text;
  ingestItem.is_valid = result.valid;
  ingestItem.run_kind = toString(input.context.run_kind);
  ingestItem.measure_mode = toString(input.context.measure_mode);
  ingestItem.attempt_kind = toString(input.context.attempt_kind);
  ingestItem.measure_round = measureRoundFrom(input.context);
  ingestItem.result_judgement = resultTextFrom(result);
  ingestItem.fail_class = toString(result.fail_class);
  ingestItem.upload_kind = uploadKindFrom(input.context, result);
  ingestItem.task_id = input.context.task_id;
  ingestItem.task_item_id = input.context.task_item_id;
  ingestItem.operator_id = input.context.operator_id;
  ingestItem.review_status = QStringLiteral("PENDING");
  ingestItem.is_effective = true;
  ingestItem.status = QStringLiteral("READY");

  IngestResultInput ingestResult;
  ingestResult.total_len_mm = qIsNaN(result.values.total_len_mm) ? QVariant() : QVariant(result.values.total_len_mm);
  ingestResult.ad_len_mm = qIsNaN(result.values.ad_len_mm) ? QVariant() : QVariant(result.values.ad_len_mm);
  ingestResult.bc_len_mm = qIsNaN(result.values.bc_len_mm) ? QVariant() : QVariant(result.values.bc_len_mm);
  ingestResult.id_left_mm = qIsNaN(result.values.id_left_mm) ? QVariant() : QVariant(result.values.id_left_mm);
  ingestResult.id_right_mm = qIsNaN(result.values.id_right_mm) ? QVariant() : QVariant(result.values.id_right_mm);
  ingestResult.od_left_mm = qIsNaN(result.values.od_left_mm) ? QVariant() : QVariant(result.values.od_left_mm);
  ingestResult.od_right_mm = qIsNaN(result.values.od_right_mm) ? QVariant() : QVariant(result.values.od_right_mm);
  ingestResult.runout_left_mm = qIsNaN(result.values.runout_left_mm) ? QVariant() : QVariant(result.values.runout_left_mm);
  ingestResult.runout_right_mm = qIsNaN(result.values.runout_right_mm) ? QVariant() : QVariant(result.values.runout_right_mm);
  ingestResult.tolerance_json = result.tolerance_json;
  ingestResult.extra_json = result.extra_json;

  out->raw_snapshot = raw;
  out->ingest_item = ingestItem;
  out->ingest_result = ingestResult;
  return true;
}

QJsonObject toJson(const PlcMailboxSnapshot &snapshot) {
  QJsonObject obj;
  obj.insert(QStringLiteral("meas_seq"), static_cast<qint64>(snapshot.meas_seq));
  obj.insert(QStringLiteral("part_type"), QString(snapshot.part_type));
  obj.insert(QStringLiteral("item_count"), snapshot.item_count);
  obj.insert(QStringLiteral("raw_layout_ver"), static_cast<int>(snapshot.raw_layout_ver));
  obj.insert(QStringLiteral("ring_count"), snapshot.ring_count);
  obj.insert(QStringLiteral("point_count"), snapshot.point_count);
  obj.insert(QStringLiteral("channel_count"), snapshot.channel_count);

  QJsonArray items;
  for (const auto &item : snapshot.items) {
    QJsonObject o;
    o.insert(QStringLiteral("present"), item.present);
    o.insert(QStringLiteral("item_index"), item.item_index);
    o.insert(QStringLiteral("slot_index"), item.slot_index);
    o.insert(QStringLiteral("part_id"), item.part_id);
    o.insert(QStringLiteral("total_len_mm"), qIsNaN(item.total_len_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(item.total_len_mm));
    o.insert(QStringLiteral("ad_len_mm"), qIsNaN(item.ad_len_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(item.ad_len_mm));
    o.insert(QStringLiteral("bc_len_mm"), qIsNaN(item.bc_len_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(item.bc_len_mm));
    o.insert(QStringLiteral("raw_points_um"), toJsonFloatArray(item.raw_points_um));
    items.push_back(o);
  }
  obj.insert(QStringLiteral("items"), items);
  return obj;
}

QJsonObject toJson(const MeasurementContext &context) {
  QJsonObject obj;
  obj.insert(QStringLiteral("run_kind"), toString(context.run_kind));
  obj.insert(QStringLiteral("measure_mode"), toString(context.measure_mode));
  obj.insert(QStringLiteral("attempt_kind"), toString(context.attempt_kind));
  obj.insert(QStringLiteral("operator_id"), context.operator_id);
  obj.insert(QStringLiteral("source_mode"), context.source_mode);
  obj.insert(QStringLiteral("measured_at_utc"), context.measured_at_utc.toString(Qt::ISODateWithMs));
  obj.insert(QStringLiteral("calibration_slot_index"), context.calibration_slot_index);
  obj.insert(QStringLiteral("calibration_type"), context.calibration_type);
  obj.insert(QStringLiteral("calibration_master_part_id"), context.calibration_master_part_id);
  return obj;
}

QJsonObject toJson(const MeasurementComputeResult &result) {
  QJsonObject values;
  values.insert(QStringLiteral("total_len_mm"), qIsNaN(result.values.total_len_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(result.values.total_len_mm));
  values.insert(QStringLiteral("ad_len_mm"), qIsNaN(result.values.ad_len_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(result.values.ad_len_mm));
  values.insert(QStringLiteral("bc_len_mm"), qIsNaN(result.values.bc_len_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(result.values.bc_len_mm));
  values.insert(QStringLiteral("id_left_mm"), qIsNaN(result.values.id_left_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(result.values.id_left_mm));
  values.insert(QStringLiteral("id_right_mm"), qIsNaN(result.values.id_right_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(result.values.id_right_mm));
  values.insert(QStringLiteral("od_left_mm"), qIsNaN(result.values.od_left_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(result.values.od_left_mm));
  values.insert(QStringLiteral("od_right_mm"), qIsNaN(result.values.od_right_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(result.values.od_right_mm));
  values.insert(QStringLiteral("runout_left_mm"), qIsNaN(result.values.runout_left_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(result.values.runout_left_mm));
  values.insert(QStringLiteral("runout_right_mm"), qIsNaN(result.values.runout_right_mm) ? QJsonValue(QStringLiteral("NaN")) : QJsonValue(result.values.runout_right_mm));

  QJsonObject obj;
  obj.insert(QStringLiteral("valid"), result.valid);
  obj.insert(QStringLiteral("judgement"), toString(result.judgement));
  obj.insert(QStringLiteral("fail_class"), toString(result.fail_class));
  obj.insert(QStringLiteral("fail_reason_code"), result.fail_reason_code);
  obj.insert(QStringLiteral("fail_reason_text"), result.fail_reason_text);
  obj.insert(QStringLiteral("values"), values);
  obj.insert(QStringLiteral("tolerance_json"), result.tolerance_json);
  obj.insert(QStringLiteral("extra_json"), result.extra_json);
  return obj;
}

} // namespace core
