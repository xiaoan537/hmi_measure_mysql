#include "core/measurement_pipeline.hpp"

#include <QByteArray>
#include <QJsonValue>
#include <QUuid>
#include <QVector>

#include <cstring>
#include <limits>

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

QString normalizedAsciiField(QString text) {
  int nul = text.indexOf(QChar('\0'));
  if (nul >= 0) text = text.left(nul);
  return text.trimmed();
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

bool checkRegSpan(const QVector<quint16> &regs, int offsetRegs, int regCount,
                  QString *err, const QString &label) {
  if (offsetRegs < 0 || regCount < 0) {
    failWith(err, QStringLiteral("%1 offset/regCount 非法").arg(label));
    return false;
  }
  if (offsetRegs + regCount > regs.size()) {
    failWith(err, QStringLiteral("%1 越界：offset=%2, regs=%3, size=%4")
                      .arg(label)
                      .arg(offsetRegs)
                      .arg(regCount)
                      .arg(regs.size()));
    return false;
  }
  return true;
}

float floatFromBits(quint32 bits) {
  float value = 0.0f;
  static_assert(sizeof(value) == sizeof(bits), "float32 size mismatch");
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

double doubleFromBits(quint64 bits) {
  double value = 0.0;
  static_assert(sizeof(value) == sizeof(bits), "float64 size mismatch");
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

float normalizedFirstStageRawValue(float v) {
  return (qAbs(v - plc_v26::kInvalidRawValue) < 1e-3f) ? qQNaN() : v;
}

QByteArray mbBytesFromRegs(const QVector<quint16> &regs) {
  QByteArray bytes;
  bytes.reserve(regs.size() * 2);
  for (quint16 reg : regs) {
    bytes.append(static_cast<char>(reg & 0x00FFu));
    bytes.append(static_cast<char>((reg >> 8) & 0x00FFu));
  }
  return bytes;
}

QString asciiFromBytes(const QByteArray &bytes) {
  int nul = bytes.indexOf('\0');
  const QByteArray sliced = (nul >= 0) ? bytes.left(nul) : bytes;
  return QString::fromLatin1(sliced).trimmed();
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
    if (item.slot_index >= kLogicalSlotCount) {
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
      failWith(err, QStringLiteral("标定 context.calibration_slot_index 必须为 %1(协议槽位%2)")
                        .arg(kCalibrationSlotIndex)
                        .arg(kCalibrationSlotIndex + 1));
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
    if (item0->slot_index >= 0 && item0->slot_index != context.calibration_slot_index) {
      failWith(err, QStringLiteral("标定测量要求 snapshot.slot_index[0] 与 context.calibration_slot_index 一致"));
      return false;
    }
  }
  return true;
}

static bool plcReadUint16At(const QVector<quint16> &regs, int offsetRegs,
                            quint16 *out, QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("plcReadUint16At.out 不能为空"));
    return false;
  }
  if (!checkRegSpan(regs, offsetRegs, 1, err, QStringLiteral("uint16"))) {
    return false;
  }
  *out = regs.at(offsetRegs);
  return true;
}

static bool plcReadUint32AbcdAt(const QVector<quint16> &regs, int offsetRegs,
                                quint32 *out, QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("plcReadUint32AbcdAt.out 不能为空"));
    return false;
  }
  if (!checkRegSpan(regs, offsetRegs, 2, err, QStringLiteral("uint32 ABCD"))) {
    return false;
  }
  const quint32 lo = static_cast<quint32>(regs.at(offsetRegs));
  const quint32 hi = static_cast<quint32>(regs.at(offsetRegs + 1));
  *out = lo | (hi << 16);
  return true;
}

static bool plcReadFloat32AbcdAt(const QVector<quint16> &regs, int offsetRegs,
                                 float *out, QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("plcReadFloat32AbcdAt.out 不能为空"));
    return false;
  }
  quint32 bits = 0;
  if (!plcReadUint32AbcdAt(regs, offsetRegs, &bits, err)) {
    return false;
  }
  *out = floatFromBits(bits);
  return true;
}

static bool plcReadFloat64WordSwappedAt(const QVector<quint16> &regs, int offsetRegs,
                                        double *out, QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("plcReadFloat64WordSwappedAt.out 不能为空"));
    return false;
  }
  if (!checkRegSpan(regs, offsetRegs, 4, err, QStringLiteral("float64 word-swapped"))) {
    return false;
  }
  quint64 bits = 0;
  bits |= static_cast<quint64>(regs.at(offsetRegs));
  bits |= static_cast<quint64>(regs.at(offsetRegs + 1)) << 16;
  bits |= static_cast<quint64>(regs.at(offsetRegs + 2)) << 32;
  bits |= static_cast<quint64>(regs.at(offsetRegs + 3)) << 48;
  *out = doubleFromBits(bits);
  return true;
}

static bool plcReadAsciiAt(const QVector<quint16> &regs, int offsetRegs, int regCount,
                           QString *out, QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("plcReadAsciiAt.out 不能为空"));
    return false;
  }
  if (!checkRegSpan(regs, offsetRegs, regCount, err, QStringLiteral("ASCII"))) {
    return false;
  }
  QString text;
  text.reserve(regCount * 2);
  for (int i = 0; i < regCount; ++i) {
    const quint16 reg = regs.at(offsetRegs + i);
    text.append(QChar(static_cast<ushort>(reg & 0x00FFu)));
    text.append(QChar(static_cast<ushort>((reg >> 8) & 0x00FFu)));
  }
  *out = normalizedAsciiField(text);
  return true;
}

static bool plcReadFloat32ArrayAbcd(const QVector<quint16> &regs, int offsetRegs,
                                    int floatCount, QVector<float> *out,
                                    QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("plcReadFloat32ArrayAbcd.out 不能为空"));
    return false;
  }
  if (floatCount < 0) {
    failWith(err, QStringLiteral("floatCount 不能小于 0"));
    return false;
  }
  const int regCount = floatCount * 2;
  if (!checkRegSpan(regs, offsetRegs, regCount, err, QStringLiteral("float32 array ABCD"))) {
    return false;
  }
  QVector<float> values;
  values.reserve(floatCount);
  for (int i = 0; i < floatCount; ++i) {
    float value = 0.0f;
    if (!plcReadFloat32AbcdAt(regs, offsetRegs + i * 2, &value, err)) {
      return false;
    }
    values.push_back(value);
  }
  *out = values;
  return true;
}

static QVector<int> logicalSlotsFromMaskV26(quint16 slotMask) {
  QVector<int> slotList;
  for (int bit = 0; bit < 16; ++bit) {
    if (((slotMask >> bit) & 0x1u) != 0) {
      slotList.push_back(bit);
    }
  }
  return slotList;
}

bool buildPlcStatusBlockV26(const QVector<quint16> &statusRegs,
                            PlcStatusBlockV2 *out,
                            QString *err) {
  if (!out) return failWith(err, QStringLiteral("buildPlcStatusBlockV26.out 不能为空")).isEmpty();
  if (statusRegs.size() < kStatusBlockRegsV26) {
    failWith(err, QStringLiteral("statusRegs 长度不足，期望至少 %1，实际 %2").arg(kStatusBlockRegsV26).arg(statusRegs.size()));
    return false;
  }
  PlcStatusBlockV2 status;
  if (!plcReadUint16At(statusRegs, kStatusOffsetMachineStateV26, &status.machine_state, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetStepStateV26, &status.step_state, err)) return false;
  if (!plcReadUint32AbcdAt(statusRegs, kStatusOffsetInterlockMaskV26, &status.interlock_mask, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetAlarmCodeV26, &status.alarm_code, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetTrayPresentMaskV26, &status.tray_present_mask, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetScanDoneV26, &status.scan_done, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetActiveItemCountV26, &status.active_item_count, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetActiveSlotMaskV26, &status.active_slot_mask, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetMailboxReadyV26, &status.mailbox_ready, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetAfterMeasurementV26, &status.after_measurement_count, err)) return false;
  status.state_seq = 0;
  status.alarm_level = 0;
  status.scan_seq = 0;
  status.meas_seq = 0;
  const auto slotList = logicalSlotsFromMaskV26(status.active_slot_mask);
  status.active_slot_index[0] = slotList.size() >= 1 ? static_cast<quint16>(slotList.at(0)) : kInvalidSlotIndex;
  status.active_slot_index[1] = slotList.size() >= 2 ? static_cast<quint16>(slotList.at(1)) : kInvalidSlotIndex;
  *out = status;
  return true;
}

bool buildPlcCommandBlockV26(const QVector<quint16> &commandRegs,
                             PlcCommandBlockV2 *out,
                             QString *err) {
  if (!out) return failWith(err, QStringLiteral("buildPlcCommandBlockV26.out 不能为空")).isEmpty();
  if (commandRegs.size() < kCommandBlockRegsV26) {
    failWith(err, QStringLiteral("commandRegs 长度不足，期望至少 %1，实际 %2").arg(kCommandBlockRegsV26).arg(commandRegs.size()));
    return false;
  }
  PlcCommandBlockV2 command;
  quint16 tmp = 0;
  if (!plcReadUint16At(commandRegs, kCommandOffsetCategoryModeV26, &tmp, err)) return false;
  command.category_mode = static_cast<qint16>(tmp);
  if (!plcReadUint16At(commandRegs, kCommandOffsetCmdCodeV26, &command.cmd_code, err)) return false;
  if (!plcReadUint16At(commandRegs, kCommandOffsetCmdResultV26, &command.cmd_result, err)) return false;
  if (!plcReadUint16At(commandRegs, kCommandOffsetCmdErrorCodeV26, &command.cmd_error_code, err)) return false;
  command.cmd_arg0 = static_cast<quint32>(qMax(0, static_cast<int>(command.category_mode)));
  command.cmd_seq = 0;
  command.cmd_arg1 = 0;
  command.cmd_ack_seq = 0;
  command.pc_ack = 0;
  command.judge_result = 0;
  *out = command;
  return true;
}

bool buildPlcTrayAllCodingBlockV26(const QVector<quint16> &trayRegs,
                                   PlcTrayPartIdBlockV2 *out,
                                   QString *err) {
  if (!out) return failWith(err, QStringLiteral("buildPlcTrayAllCodingBlockV26.out 不能为空")).isEmpty();
  if (trayRegs.size() < kTrayAllCodingRegsV26) {
    failWith(err, QStringLiteral("trayRegs 长度不足，期望至少 %1，实际 %2").arg(kTrayAllCodingRegsV26).arg(trayRegs.size()));
    return false;
  }
  const QByteArray bytes = mbBytesFromRegs(trayRegs).left(kTrayAllCodingBytesV26);
  PlcTrayPartIdBlockV2 block;
  for (int slot = 0; slot < kLogicalSlotCount; ++slot) {
    block.part_ids[slot] = asciiFromBytes(bytes.mid(slot * 81, 81));
  }
  *out = block;
  return true;
}

bool buildSecondStageMailboxSnapshotV26(const QVector<quint16> &mailboxRegs,
                                        QChar partType,
                                        PlcMailboxSnapshot *out,
                                        QString *err) {
  if (!out) return failWith(err, QStringLiteral("buildSecondStageMailboxSnapshotV26.out 不能为空")).isEmpty();
  const QChar pt = partType.toUpper();
  if (pt != QChar('A') && pt != QChar('B')) {
    failWith(err, QStringLiteral("第二阶段读取 Mailbox 需要明确 partType=A/B"));
    return false;
  }
  if (mailboxRegs.size() < kMailboxTotalRegsV26) {
    failWith(err, QStringLiteral("mailboxRegs 长度不足，期望至少 %1，实际 %2").arg(kMailboxTotalRegsV26).arg(mailboxRegs.size()));
    return false;
  }
  quint16 itemCount = 0, slotMask = 0;
  if (!plcReadUint16At(mailboxRegs, 0, &itemCount, err)) return false;
  if (!plcReadUint16At(mailboxRegs, 1, &slotMask, err)) return false;
  const QByteArray bytes = mbBytesFromRegs(mailboxRegs.mid(2, 81)).left(162);
  const QString id0 = asciiFromBytes(bytes.mid(0, 81));
  const QString id1 = asciiFromBytes(bytes.mid(81, 81));
  QVector<double> lenValues; lenValues.reserve(4);
  const int keyenceOffset = kMailboxHeaderRegsV26;
  for (int i = 0; i < 4; ++i) {
    double v = 0.0;
    if (!plcReadFloat64WordSwappedAt(mailboxRegs, keyenceOffset + i * 4, &v, err)) return false;
    lenValues.push_back(v);
  }
  QVector<float> rawValues;
  if (!plcReadFloat32ArrayAbcd(mailboxRegs, keyenceOffset + 16, 576, &rawValues, err)) return false;
  for (float &v : rawValues) v = normalizedFirstStageRawValue(v);
  PlcMailboxSnapshot snapshot;
  snapshot.meas_seq = 0;
  snapshot.part_type = pt;
  snapshot.raw_layout_ver = 26;
  snapshot.ring_count = 1;
  snapshot.point_count = 72;
  snapshot.channel_count = (pt == QChar('A')) ? 4 : 2;
  snapshot.item_count = qBound(0, static_cast<int>(itemCount), 2);
  const auto slotList = logicalSlotsFromMaskV26(slotMask);
  auto slotForItem = [&](int idx)->int { return (slotList.size() > idx) ? slotList.at(idx) : -1; };
  const auto buildItemA = [&](int itemIndex, const QString &partId, int curveBase) {
    PlcMailboxItemSnapshot item;
    item.present = true; item.item_index = itemIndex; item.slot_index = slotForItem(itemIndex); item.part_id = normalizedAsciiField(partId);
    item.total_len_mm = static_cast<float>(lenValues.value(itemIndex));
    item.raw_points_um.reserve(4 * 72);
    for (int ch = 0; ch < 4; ++ch) { const int offset = (curveBase + ch) * 72; for (int ptIdx = 0; ptIdx < 72; ++ptIdx) item.raw_points_um.push_back(rawValues.at(offset + ptIdx)); }
    return item;
  };
  const auto buildItemB = [&](int itemIndex, const QString &partId, int curveBase) {
    PlcMailboxItemSnapshot item;
    item.present = true; item.item_index = itemIndex; item.slot_index = slotForItem(itemIndex); item.part_id = normalizedAsciiField(partId);
    item.ad_len_mm = static_cast<float>(lenValues.value(itemIndex * 2));
    item.bc_len_mm = static_cast<float>(lenValues.value(itemIndex * 2 + 1));
    item.raw_points_um.reserve(2 * 72);
    for (int ch = 0; ch < 2; ++ch) { const int offset = (curveBase + ch) * 72; for (int ptIdx = 0; ptIdx < 72; ++ptIdx) item.raw_points_um.push_back(rawValues.at(offset + ptIdx)); }
    return item;
  };
  if (snapshot.item_count >= 1) snapshot.items.push_back(pt == QChar('A') ? buildItemA(0, id0, 0) : buildItemB(0, id0, 0));
  if (snapshot.item_count >= 2) snapshot.items.push_back(pt == QChar('A') ? buildItemA(1, id1, 4) : buildItemB(1, id1, 2));
  *out = snapshot;
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
    const float nan32 = std::numeric_limits<float>::quiet_NaN();
    raw.gt2r_mm3 = {item->total_len_mm, nan32, nan32};
  } else {
    raw.run_spec.rings = input.snapshot.ring_count;
    raw.run_spec.points_per_ring = input.snapshot.point_count;
    raw.run_spec.angle_step_deg = input.snapshot.point_count > 0
                                      ? 360.0f / static_cast<float>(input.snapshot.point_count)
                                      : 0.0f;
    raw.run_spec.order_code = 1;
    raw.runout2 = item->raw_points_um;
    raw.gt2r_mm3 = {item->ad_len_mm, item->bc_len_mm,
                    std::numeric_limits<float>::quiet_NaN()};
  }

  IngestItemInput ingestItem;
  ingestItem.item_index = itemIndex;
  ingestItem.slot_index = (item->slot_index >= 0) ? QVariant(item->slot_index) : QVariant();
  ingestItem.slot_id = (item->slot_index >= 0)
                          ? QStringLiteral("SLOT-%1").arg(item->slot_index, 2, 10, QLatin1Char('0'))
                          : QStringLiteral("UNKNOWN");
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

QJsonObject toJson(const PlcStatusBlockV2 &status) {
  QJsonObject obj;
  obj.insert(QStringLiteral("machine_state"), static_cast<int>(status.machine_state));
  obj.insert(QStringLiteral("step_state"), static_cast<int>(status.step_state));
  obj.insert(QStringLiteral("state_seq"), static_cast<qint64>(status.state_seq));
  obj.insert(QStringLiteral("interlock_mask"), static_cast<qint64>(status.interlock_mask));
  obj.insert(QStringLiteral("alarm_code"), static_cast<int>(status.alarm_code));
  obj.insert(QStringLiteral("alarm_level"), static_cast<int>(status.alarm_level));
  obj.insert(QStringLiteral("tray_present_mask"), static_cast<int>(status.tray_present_mask));
  obj.insert(QStringLiteral("scan_done"), static_cast<int>(status.scan_done));
  obj.insert(QStringLiteral("scan_seq"), static_cast<qint64>(status.scan_seq));
  obj.insert(QStringLiteral("active_item_count"), static_cast<int>(status.active_item_count));
  QJsonArray active;
  active.push_back(static_cast<int>(status.active_slot_index[0]));
  active.push_back(static_cast<int>(status.active_slot_index[1]));
  obj.insert(QStringLiteral("active_slot_index"), active);
  obj.insert(QStringLiteral("mailbox_ready"), static_cast<int>(status.mailbox_ready));
  obj.insert(QStringLiteral("meas_seq"), static_cast<qint64>(status.meas_seq));
  return obj;
}

QJsonObject toJson(const PlcTrayPartIdBlockV2 &tray) {
  QJsonObject obj;
  QJsonArray ids;
  for (int slot = 0; slot < kLogicalSlotCount; ++slot) {
    ids.push_back(tray.part_ids[slot]);
  }
  obj.insert(QStringLiteral("part_ids"), ids);
  return obj;
}

QJsonObject toJson(const PlcCommandBlockV2 &command) {
  QJsonObject obj;
  obj.insert(QStringLiteral("cmd_code"), static_cast<int>(command.cmd_code));
  obj.insert(QStringLiteral("cmd_seq"), static_cast<qint64>(command.cmd_seq));
  obj.insert(QStringLiteral("cmd_arg0"), static_cast<qint64>(command.cmd_arg0));
  obj.insert(QStringLiteral("cmd_arg1"), static_cast<qint64>(command.cmd_arg1));
  obj.insert(QStringLiteral("cmd_ack_seq"), static_cast<qint64>(command.cmd_ack_seq));
  obj.insert(QStringLiteral("cmd_result"), static_cast<int>(command.cmd_result));
  obj.insert(QStringLiteral("cmd_error_code"), static_cast<int>(command.cmd_error_code));
  return obj;
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
