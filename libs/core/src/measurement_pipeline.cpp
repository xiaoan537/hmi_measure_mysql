#include "core/measurement_pipeline.hpp"

#include <QJsonValue>
#include <QUuid>

#include <cstring>

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

bool plcReadUint16At(const QVector<quint16> &regs, int offsetRegs,
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

bool plcReadUint32AbcdAt(const QVector<quint16> &regs, int offsetRegs,
                         quint32 *out, QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("plcReadUint32AbcdAt.out 不能为空"));
    return false;
  }
  if (!checkRegSpan(regs, offsetRegs, 2, err, QStringLiteral("uint32 ABCD"))) {
    return false;
  }
  const quint32 hi = static_cast<quint32>(regs.at(offsetRegs));
  const quint32 lo = static_cast<quint32>(regs.at(offsetRegs + 1));
  *out = (hi << 16) | lo;
  return true;
}

bool plcReadFloat32AbcdAt(const QVector<quint16> &regs, int offsetRegs,
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

bool plcReadAsciiAt(const QVector<quint16> &regs, int offsetRegs, int regCount,
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
    text.append(QChar(static_cast<ushort>((reg >> 8) & 0x00FFu)));
    text.append(QChar(static_cast<ushort>(reg & 0x00FFu)));
  }
  *out = normalizedAsciiField(text);
  return true;
}

bool plcReadFloat32ArrayAbcd(const QVector<quint16> &regs, int offsetRegs,
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

bool plcWriteAsciiRegs(const QString &text, int regCount,
                       QVector<quint16> *out, QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("plcWriteAsciiRegs.out 不能为空"));
    return false;
  }
  if (regCount < 0) {
    failWith(err, QStringLiteral("regCount 不能小于 0"));
    return false;
  }

  QString normalized = text;
  if (normalized.size() > regCount * 2) {
    normalized = normalized.left(regCount * 2);
  }
  while (normalized.size() < regCount * 2) {
    normalized.append(QChar('\0'));
  }

  QVector<quint16> regs;
  regs.reserve(regCount);
  for (int i = 0; i < regCount; ++i) {
    const ushort hi = static_cast<ushort>(normalized.at(i * 2).unicode() & 0x00FFu);
    const ushort lo = static_cast<ushort>(normalized.at(i * 2 + 1).unicode() & 0x00FFu);
    regs.push_back(static_cast<quint16>((hi << 8) | lo));
  }
  *out = regs;
  return true;
}

bool buildPlcStatusBlockV2(const QVector<quint16> &statusRegs,
                           PlcStatusBlockV2 *out,
                           QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("buildPlcStatusBlockV2.out 不能为空"));
    return false;
  }
  if (statusRegs.size() < kStatusBlockRegsV2) {
    failWith(err, QStringLiteral("statusRegs 长度不足，期望至少 %1，实际 %2")
                      .arg(kStatusBlockRegsV2)
                      .arg(statusRegs.size()));
    return false;
  }

  PlcStatusBlockV2 status;
  if (!plcReadUint16At(statusRegs, kStatusOffsetMachineState, &status.machine_state, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetStepState, &status.step_state, err)) return false;
  if (!plcReadUint32AbcdAt(statusRegs, kStatusOffsetStateSeq, &status.state_seq, err)) return false;
  if (!plcReadUint32AbcdAt(statusRegs, kStatusOffsetInterlockMask, &status.interlock_mask, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetAlarmCode, &status.alarm_code, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetAlarmLevel, &status.alarm_level, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetTrayPresentMask, &status.tray_present_mask, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetScanDone, &status.scan_done, err)) return false;
  if (!plcReadUint32AbcdAt(statusRegs, kStatusOffsetScanSeq, &status.scan_seq, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetActiveItemCount, &status.active_item_count, err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetActiveSlotIndex0, &status.active_slot_index[0], err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetActiveSlotIndex1, &status.active_slot_index[1], err)) return false;
  if (!plcReadUint16At(statusRegs, kStatusOffsetMailboxReady, &status.mailbox_ready, err)) return false;
  if (!plcReadUint32AbcdAt(statusRegs, kStatusOffsetMeasSeq, &status.meas_seq, err)) return false;

  *out = status;
  return true;
}

bool buildPlcTrayPartIdBlockV2(const QVector<quint16> &trayRegs,
                               PlcTrayPartIdBlockV2 *out,
                               QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("buildPlcTrayPartIdBlockV2.out 不能为空"));
    return false;
  }
  if (trayRegs.size() < kTrayPartIdBlockRegsV2) {
    failWith(err, QStringLiteral("trayRegs 长度不足，期望至少 %1，实际 %2")
                      .arg(kTrayPartIdBlockRegsV2)
                      .arg(trayRegs.size()));
    return false;
  }

  PlcTrayPartIdBlockV2 block;
  for (int slot = 0; slot < kLogicalSlotCount; ++slot) {
    QString partId;
    if (!plcReadAsciiAt(trayRegs, slot * kTrayPartIdRegsPerSlot,
                        kTrayPartIdRegsPerSlot, &partId, err)) {
      return false;
    }
    block.part_ids[slot] = normalizedAsciiField(partId);
  }

  *out = block;
  return true;
}

bool buildPlcCommandBlockV2(const QVector<quint16> &commandRegs,
                            PlcCommandBlockV2 *out,
                            QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("buildPlcCommandBlockV2.out 不能为空"));
    return false;
  }
  if (commandRegs.size() < kCommandBlockRegsV2) {
    failWith(err, QStringLiteral("commandRegs 长度不足，期望至少 %1，实际 %2")
                      .arg(kCommandBlockRegsV2)
                      .arg(commandRegs.size()));
    return false;
  }

  PlcCommandBlockV2 command;
  if (!plcReadUint16At(commandRegs, kCommandOffsetCmdCode, &command.cmd_code, err)) return false;
  if (!plcReadUint32AbcdAt(commandRegs, kCommandOffsetCmdSeq, &command.cmd_seq, err)) return false;
  if (!plcReadUint32AbcdAt(commandRegs, kCommandOffsetCmdArg0, &command.cmd_arg0, err)) return false;
  if (!plcReadUint32AbcdAt(commandRegs, kCommandOffsetCmdArg1, &command.cmd_arg1, err)) return false;
  if (!plcReadUint32AbcdAt(commandRegs, kCommandOffsetCmdAckSeq, &command.cmd_ack_seq, err)) return false;
  if (!plcReadUint16At(commandRegs, kCommandOffsetCmdResult, &command.cmd_result, err)) return false;
  if (!plcReadUint16At(commandRegs, kCommandOffsetCmdErrorCode, &command.cmd_error_code, err)) return false;

  *out = command;
  return true;
}

bool encodePlcTrayPartIdBlockV2(const PlcTrayPartIdBlockV2 &block,
                                QVector<quint16> *out,
                                QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("encodePlcTrayPartIdBlockV2.out 不能为空"));
    return false;
  }

  QVector<quint16> regs;
  regs.reserve(kTrayPartIdBlockRegsV2);
  for (int slot = 0; slot < kLogicalSlotCount; ++slot) {
    QVector<quint16> slotRegs;
    if (!plcWriteAsciiRegs(block.part_ids[slot], kTrayPartIdRegsPerSlot, &slotRegs, err)) {
      return false;
    }
    regs += slotRegs;
  }
  *out = regs;
  return true;
}

bool encodePlcTrayPartIdSlotRegs(const QString &partId,
                                 QVector<quint16> *out,
                                 QString *err) {
  return plcWriteAsciiRegs(partId, kTrayPartIdRegsPerSlot, out, err);
}

bool splitPlcMailboxRegisters(const QVector<quint16> &mailboxRegs,
                              PlcMailboxRegisterBlock *out,
                              QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("splitPlcMailboxRegisters.out 不能为空"));
    return false;
  }
  if (mailboxRegs.size() < kMailboxTotalRegsV2) {
    failWith(err, QStringLiteral("mailboxRegs 长度不足，期望至少 %1，实际 %2")
                      .arg(kMailboxTotalRegsV2)
                      .arg(mailboxRegs.size()));
    return false;
  }

  PlcMailboxRegisterBlock block;
  block.header_regs = mailboxRegs.mid(0, kMailboxHeaderBlockRegsV2);
  block.array_regs = mailboxRegs.mid(kMailboxHeaderBlockRegsV2, kMailboxArrayRegsReservedV2);
  *out = block;
  return true;
}

bool buildPlcMailboxHeaderV2(const QVector<quint16> &headerRegs,
                             PlcMailboxHeaderV2 *out,
                             QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("buildPlcMailboxHeaderV2.out 不能为空"));
    return false;
  }
  if (headerRegs.size() < kMailboxHeaderUsedRegsV2) {
    failWith(err, QStringLiteral("headerRegs 长度不足，期望至少 %1（有效字段区），实际 %2")
                      .arg(kMailboxHeaderUsedRegsV2)
                      .arg(headerRegs.size()));
    return false;
  }

  // 仅解析前 54 regs 的有效字段；若 headerRegs 实际为 58 regs，则尾部 4 regs 作为 reserved，当前忽略。
  PlcMailboxHeaderV2 header;
  if (!plcReadUint32AbcdAt(headerRegs, kMailboxOffsetMeasSeq, &header.meas_seq, err)) return false;
  if (!plcReadUint16At(headerRegs, kMailboxOffsetPartType, &header.part_type, err)) return false;
  if (!plcReadUint16At(headerRegs, kMailboxOffsetItemCount, &header.item_count, err)) return false;
  if (!plcReadUint16At(headerRegs, kMailboxOffsetSlotIndex0, &header.slot_index[0], err)) return false;
  if (!plcReadUint16At(headerRegs, kMailboxOffsetSlotIndex1, &header.slot_index[1], err)) return false;
  if (!plcReadAsciiAt(headerRegs, kMailboxOffsetPartIdAscii0, kTrayPartIdRegsPerSlot, &header.part_id_ascii[0], err)) return false;
  if (!plcReadAsciiAt(headerRegs, kMailboxOffsetPartIdAscii1, kTrayPartIdRegsPerSlot, &header.part_id_ascii[1], err)) return false;
  if (!plcReadFloat32AbcdAt(headerRegs, kMailboxOffsetTotalLen0, &header.total_len_mm[0], err)) return false;
  if (!plcReadFloat32AbcdAt(headerRegs, kMailboxOffsetTotalLen1, &header.total_len_mm[1], err)) return false;
  if (!plcReadFloat32AbcdAt(headerRegs, kMailboxOffsetAdLen0, &header.ad_len_mm[0], err)) return false;
  if (!plcReadFloat32AbcdAt(headerRegs, kMailboxOffsetAdLen1, &header.ad_len_mm[1], err)) return false;
  if (!plcReadFloat32AbcdAt(headerRegs, kMailboxOffsetBcLen0, &header.bc_len_mm[0], err)) return false;
  if (!plcReadFloat32AbcdAt(headerRegs, kMailboxOffsetBcLen1, &header.bc_len_mm[1], err)) return false;
  if (!plcReadUint16At(headerRegs, kMailboxOffsetRawLayoutVer, &header.raw_layout_ver, err)) return false;
  if (!plcReadUint16At(headerRegs, kMailboxOffsetRingCount, &header.ring_count, err)) return false;
  if (!plcReadUint16At(headerRegs, kMailboxOffsetPointCount, &header.point_count, err)) return false;
  if (!plcReadUint16At(headerRegs, kMailboxOffsetChannelCount, &header.channel_count, err)) return false;

  *out = header;
  return true;
}

bool buildPlcMailboxRawFrame(const QVector<quint16> &headerRegs,
                             const QVector<quint16> &arrayRegs,
                             PlcMailboxRawFrame *out,
                             QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("buildPlcMailboxRawFrame.out 不能为空"));
    return false;
  }
  if (arrayRegs.size() % 2 != 0) {
    failWith(err, QStringLiteral("arrayRegs 长度必须为偶数，实际 %1").arg(arrayRegs.size()));
    return false;
  }

  PlcMailboxRawFrame frame;
  if (!buildPlcMailboxHeaderV2(headerRegs, &frame.header, err)) {
    return false;
  }
  if (!plcReadFloat32ArrayAbcd(arrayRegs, 0, arrayRegs.size() / 2, &frame.arrays_um, err)) {
    return false;
  }

  *out = frame;
  return true;
}

bool buildPlcMailboxRawFrame(const PlcMailboxRegisterBlock &regBlock,
                             PlcMailboxRawFrame *out,
                             QString *err) {
  return buildPlcMailboxRawFrame(regBlock.header_regs, regBlock.array_regs, out, err);
}

QChar mailboxPartTypeFromHeader(quint16 plcPartType) {
  switch (plcPartType) {
  case 1:
    return QChar('A');
  case 2:
    return QChar('B');
  default:
    return QChar();
  }
}

int expectedMailboxPointCountPerItem(const PlcMailboxHeaderV2 &header) {
  if (header.ring_count == 0 || header.point_count == 0 || header.channel_count == 0) {
    return 0;
  }
  return static_cast<int>(header.ring_count) *
         static_cast<int>(header.point_count) *
         static_cast<int>(header.channel_count);
}

int expectedMailboxUsedPointCount(const PlcMailboxHeaderV2 &header) {
  const int perItem = expectedMailboxPointCountPerItem(header);
  if (perItem <= 0 || header.item_count == 0) return 0;
  return perItem * static_cast<int>(header.item_count);
}

bool buildPlcMailboxSnapshot(const PlcMailboxHeaderV2 &header,
                             const QVector<float> &arrays_um,
                             PlcMailboxSnapshot *out,
                             QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("out 不能为空"));
    return false;
  }

  const QChar partType = mailboxPartTypeFromHeader(header.part_type);
  if (partType.isNull()) {
    failWith(err, QStringLiteral("header.part_type 非法，仅支持 1=A, 2=B"));
    return false;
  }
  if (header.item_count != 1 && header.item_count != 2) {
    failWith(err, QStringLiteral("header.item_count 仅支持 1 或 2"));
    return false;
  }
  if (header.ring_count == 0 || header.point_count == 0 || header.channel_count == 0) {
    failWith(err, QStringLiteral("header.ring_count / point_count / channel_count 必须大于 0"));
    return false;
  }

  const int expectChannels = (partType == QChar('A')) ? 4 : 2;
  if (header.channel_count != expectChannels) {
    failWith(err, QStringLiteral("header.channel_count 与 part_type 不匹配，期望 %1，实际 %2")
                      .arg(expectChannels)
                      .arg(header.channel_count));
    return false;
  }

  const int perItemPoints = expectedMailboxPointCountPerItem(header);
  const int usedPoints = expectedMailboxUsedPointCount(header);
  if (perItemPoints <= 0 || usedPoints <= 0) {
    failWith(err, QStringLiteral("根据 header 计算出的 arrays 点数非法"));
    return false;
  }
  if (arrays_um.size() < usedPoints) {
    failWith(err, QStringLiteral("arrays_um 长度不足，期望至少 %1，实际 %2")
                      .arg(usedPoints)
                      .arg(arrays_um.size()));
    return false;
  }

  PlcMailboxSnapshot snapshot;
  snapshot.meas_seq = header.meas_seq;
  snapshot.part_type = partType;
  snapshot.item_count = static_cast<int>(header.item_count);
  snapshot.raw_layout_ver = header.raw_layout_ver;
  snapshot.ring_count = static_cast<int>(header.ring_count);
  snapshot.point_count = static_cast<int>(header.point_count);
  snapshot.channel_count = static_cast<int>(header.channel_count);
  snapshot.items.reserve(snapshot.item_count);

  for (int i = 0; i < snapshot.item_count; ++i) {
    const quint16 slotIndex = header.slot_index[i];
    if (slotIndex == kInvalidSlotIndex || slotIndex >= kLogicalSlotCount) {
      failWith(err, QStringLiteral("header.slot_index[%1] 非法").arg(i));
      return false;
    }

    PlcMailboxItemSnapshot item;
    item.present = true;
    item.item_index = i;
    item.slot_index = static_cast<int>(slotIndex);
    item.part_id = normalizedAsciiField(header.part_id_ascii[i]);
    item.total_len_mm = header.total_len_mm[i];
    item.ad_len_mm = header.ad_len_mm[i];
    item.bc_len_mm = header.bc_len_mm[i];
    item.raw_points_um = arrays_um.mid(i * perItemPoints, perItemPoints);
    snapshot.items.push_back(item);
  }

  QString validErr;
  if (!snapshot.isValid(&validErr)) {
    failWith(err, validErr);
    return false;
  }

  *out = snapshot;
  return true;
}

bool buildPlcMailboxSnapshot(const PlcMailboxRawFrame &frame,
                             PlcMailboxSnapshot *out,
                             QString *err) {
  return buildPlcMailboxSnapshot(frame.header, frame.arrays_um, out, err);
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
