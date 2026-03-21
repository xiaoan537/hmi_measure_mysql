#include "core/plc_fake_client_v2.hpp"

#include <cstring>

namespace core {
namespace {

void failWith(QString *err, const QString &message) {
  if (err) {
    *err = message;
  }
}

void appendUint16(QVector<quint16> *out, quint16 value) {
  out->push_back(value);
}

void appendUint32Abcd(QVector<quint16> *out, quint32 value) {
  out->push_back(static_cast<quint16>((value >> 16) & 0xFFFFu));
  out->push_back(static_cast<quint16>(value & 0xFFFFu));
}

void appendFloat32Abcd(QVector<quint16> *out, float value) {
  quint32 bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float/quint32 size mismatch");
  std::memcpy(&bits, &value, sizeof(bits));
  appendUint32Abcd(out, bits);
}

bool ensureConnected(bool connected, QString *err) {
  if (!connected) {
    failWith(err, QStringLiteral("Fake PLC 当前未连接"));
    return false;
  }
  return true;
}

} // namespace

bool encodePlcStatusBlockRegsV2(const PlcStatusBlockV2 &status,
                                QVector<quint16> *out,
                                QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("encodePlcStatusBlockRegsV2.out 不能为空"));
    return false;
  }

  QVector<quint16> regs;
  regs.reserve(kStatusBlockRegsV2);
  appendUint16(&regs, status.machine_state);
  appendUint16(&regs, status.step_state);
  appendUint32Abcd(&regs, status.state_seq);
  appendUint32Abcd(&regs, status.interlock_mask);
  appendUint16(&regs, status.alarm_code);
  appendUint16(&regs, status.alarm_level);
  appendUint16(&regs, status.tray_present_mask);
  appendUint16(&regs, status.scan_done);
  appendUint32Abcd(&regs, status.scan_seq);
  appendUint16(&regs, status.active_item_count);
  appendUint16(&regs, status.active_slot_index[0]);
  appendUint16(&regs, status.active_slot_index[1]);
  appendUint16(&regs, status.mailbox_ready);
  appendUint32Abcd(&regs, status.meas_seq);

  if (regs.size() != kStatusBlockRegsV2) {
    failWith(err, QStringLiteral("编码 Status Block 失败：寄存器数应为 %1，实际 %2")
                      .arg(kStatusBlockRegsV2)
                      .arg(regs.size()));
    return false;
  }
  *out = regs;
  return true;
}

bool encodePlcCommandBlockFullRegsV2(const PlcCommandBlockV2 &command,
                                     QVector<quint16> *out,
                                     QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("encodePlcCommandBlockFullRegsV2.out 不能为空"));
    return false;
  }

  QVector<quint16> regs;
  regs.reserve(kCommandBlockRegsV2);
  appendUint16(&regs, command.cmd_code);
  appendUint32Abcd(&regs, command.cmd_seq);
  appendUint32Abcd(&regs, command.cmd_arg0);
  appendUint32Abcd(&regs, command.cmd_arg1);
  appendUint32Abcd(&regs, command.cmd_ack_seq);
  appendUint16(&regs, command.cmd_result);
  appendUint16(&regs, command.cmd_error_code);

  if (regs.size() != kCommandBlockRegsV2) {
    failWith(err, QStringLiteral("编码 Command Block 失败：寄存器数应为 %1，实际 %2")
                      .arg(kCommandBlockRegsV2)
                      .arg(regs.size()));
    return false;
  }
  *out = regs;
  return true;
}

bool encodePlcMailboxHeaderRegsV2(const PlcMailboxHeaderV2 &header,
                                  QVector<quint16> *out,
                                  QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("encodePlcMailboxHeaderRegsV2.out 不能为空"));
    return false;
  }

  QVector<quint16> regs(kMailboxHeaderBlockRegsV2, 0);
  regs[kMailboxOffsetMeasSeq] = static_cast<quint16>((header.meas_seq >> 16) & 0xFFFFu);
  regs[kMailboxOffsetMeasSeq + 1] = static_cast<quint16>(header.meas_seq & 0xFFFFu);
  regs[kMailboxOffsetPartType] = header.part_type;
  regs[kMailboxOffsetItemCount] = header.item_count;
  regs[kMailboxOffsetSlotIndex0] = header.slot_index[0];
  regs[kMailboxOffsetSlotIndex1] = header.slot_index[1];

  QVector<quint16> ascii0;
  QVector<quint16> ascii1;
  if (!plcWriteAsciiRegs(header.part_id_ascii[0], kTrayPartIdRegsPerSlot, &ascii0, err)) {
    return false;
  }
  if (!plcWriteAsciiRegs(header.part_id_ascii[1], kTrayPartIdRegsPerSlot, &ascii1, err)) {
    return false;
  }
  for (int i = 0; i < ascii0.size(); ++i) {
    regs[kMailboxOffsetPartIdAscii0 + i] = ascii0.at(i);
  }
  for (int i = 0; i < ascii1.size(); ++i) {
    regs[kMailboxOffsetPartIdAscii1 + i] = ascii1.at(i);
  }

  auto putFloat = [&](int offset, float value) {
    quint32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    regs[offset] = static_cast<quint16>((bits >> 16) & 0xFFFFu);
    regs[offset + 1] = static_cast<quint16>(bits & 0xFFFFu);
  };

  putFloat(kMailboxOffsetTotalLen0, header.total_len_mm[0]);
  putFloat(kMailboxOffsetTotalLen1, header.total_len_mm[1]);
  putFloat(kMailboxOffsetAdLen0, header.ad_len_mm[0]);
  putFloat(kMailboxOffsetAdLen1, header.ad_len_mm[1]);
  putFloat(kMailboxOffsetBcLen0, header.bc_len_mm[0]);
  putFloat(kMailboxOffsetBcLen1, header.bc_len_mm[1]);

  regs[kMailboxOffsetRawLayoutVer] = header.raw_layout_ver;
  regs[kMailboxOffsetRingCount] = header.ring_count;
  regs[kMailboxOffsetPointCount] = header.point_count;
  regs[kMailboxOffsetChannelCount] = header.channel_count;

  *out = regs;
  return true;
}

bool encodePlcMailboxArrayRegsV2(const QVector<float> &arrays_um,
                                 QVector<quint16> *out,
                                 QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("encodePlcMailboxArrayRegsV2.out 不能为空"));
    return false;
  }
  if (arrays_um.size() > kMailboxArrayFloatCountReservedV2) {
    failWith(err, QStringLiteral("arrays_um float 数超出预留容量：最多 %1，实际 %2")
                      .arg(kMailboxArrayFloatCountReservedV2)
                      .arg(arrays_um.size()));
    return false;
  }

  QVector<quint16> regs;
  regs.reserve(kMailboxArrayRegsReservedV2);
  for (float v : arrays_um) {
    appendFloat32Abcd(&regs, v);
  }
  while (regs.size() < kMailboxArrayRegsReservedV2) {
    regs.push_back(0);
  }
  *out = regs;
  return true;
}

bool buildPlcMailboxRegisterBlockFromFrameV2(const PlcMailboxRawFrame &frame,
                                             PlcMailboxRegisterBlock *out,
                                             QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("buildPlcMailboxRegisterBlockFromFrameV2.out 不能为空"));
    return false;
  }

  PlcMailboxRegisterBlock block;
  if (!encodePlcMailboxHeaderRegsV2(frame.header, &block.header_regs, err)) {
    return false;
  }
  if (!encodePlcMailboxArrayRegsV2(frame.arrays_um, &block.array_regs, err)) {
    return false;
  }
  *out = block;
  return true;
}

bool flattenPlcMailboxRegisterBlockV2(const PlcMailboxRegisterBlock &block,
                                      QVector<quint16> *out,
                                      QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("flattenPlcMailboxRegisterBlockV2.out 不能为空"));
    return false;
  }
  if (block.header_regs.size() != kMailboxHeaderBlockRegsV2) {
    failWith(err, QStringLiteral("Mailbox header_regs 长度应为 %1，当前为 %2")
                      .arg(kMailboxHeaderBlockRegsV2)
                      .arg(block.header_regs.size()));
    return false;
  }
  if (block.array_regs.size() != kMailboxArrayRegsReservedV2) {
    failWith(err, QStringLiteral("Mailbox array_regs 长度应为 %1，当前为 %2")
                      .arg(kMailboxArrayRegsReservedV2)
                      .arg(block.array_regs.size()));
    return false;
  }

  QVector<quint16> regs;
  regs.reserve(kMailboxTotalRegsV2);
  regs += block.header_regs;
  regs += block.array_regs;
  *out = regs;
  return true;
}

FakePlcRegisterClientV2::FakePlcRegisterClientV2(QObject *parent)
    : QObject(parent) {}

FakePlcRegisterClientV2::~FakePlcRegisterClientV2() = default;

void FakePlcRegisterClientV2::setConnected(bool connected) {
  connected_ = connected;
}

void FakePlcRegisterClientV2::clearAll() {
  holding_registers_.clear();
  write_history_.clear();
}

void FakePlcRegisterClientV2::clearWriteHistory() {
  write_history_.clear();
}

quint16 FakePlcRegisterClientV2::registerValue(quint32 address,
                                               quint16 defaultValue) const {
  return holding_registers_.value(address, defaultValue);
}

QVector<quint16> FakePlcRegisterClientV2::registerRange(quint32 start_address,
                                                        quint16 reg_count) const {
  QVector<quint16> regs;
  regs.reserve(reg_count);
  for (quint16 i = 0; i < reg_count; ++i) {
    regs.push_back(holding_registers_.value(start_address + i, 0));
  }
  return regs;
}

void FakePlcRegisterClientV2::setRegister(quint32 address, quint16 value) {
  holding_registers_.insert(address, value);
}

void FakePlcRegisterClientV2::setRegisters(quint32 start_address,
                                           const QVector<quint16> &values) {
  for (int i = 0; i < values.size(); ++i) {
    holding_registers_.insert(start_address + static_cast<quint32>(i), values.at(i));
  }
}

bool FakePlcRegisterClientV2::loadStatusBlock(const PlcAddressLayoutV2 &layout,
                                              const PlcStatusBlockV2 &status,
                                              QString *err) {
  if (!layout.isValid(err)) {
    return false;
  }
  QVector<quint16> regs;
  if (!encodePlcStatusBlockRegsV2(status, &regs, err)) {
    return false;
  }
  setRegisters(layout.status.start_address, regs);
  return true;
}

bool FakePlcRegisterClientV2::loadTrayPartIdBlock(const PlcAddressLayoutV2 &layout,
                                                  const PlcTrayPartIdBlockV2 &tray,
                                                  QString *err) {
  if (!layout.isValid(err)) {
    return false;
  }
  QVector<quint16> regs;
  if (!encodePlcTrayPartIdBlockV2(tray, &regs, err)) {
    return false;
  }
  setRegisters(layout.tray.start_address, regs);
  return true;
}

bool FakePlcRegisterClientV2::loadCommandBlock(const PlcAddressLayoutV2 &layout,
                                               const PlcCommandBlockV2 &command,
                                               QString *err) {
  if (!layout.isValid(err)) {
    return false;
  }
  QVector<quint16> regs;
  if (!encodePlcCommandBlockFullRegsV2(command, &regs, err)) {
    return false;
  }
  setRegisters(layout.command.start_address, regs);
  return true;
}

bool FakePlcRegisterClientV2::loadMailboxRegisterBlock(const PlcAddressLayoutV2 &layout,
                                                       const PlcMailboxRegisterBlock &block,
                                                       QString *err) {
  if (!layout.isValid(err)) {
    return false;
  }
  QVector<quint16> regs;
  if (!flattenPlcMailboxRegisterBlockV2(block, &regs, err)) {
    return false;
  }
  setRegisters(layout.mailbox.start_address, regs);
  return true;
}

bool FakePlcRegisterClientV2::loadMailboxRawFrame(const PlcAddressLayoutV2 &layout,
                                                  const PlcMailboxRawFrame &frame,
                                                  QString *err) {
  PlcMailboxRegisterBlock block;
  if (!buildPlcMailboxRegisterBlockFromFrameV2(frame, &block, err)) {
    return false;
  }
  return loadMailboxRegisterBlock(layout, block, err);
}

bool FakePlcRegisterClientV2::readHoldingRegisters(quint32 start_address,
                                                   quint16 reg_count,
                                                   QVector<quint16> *out,
                                                   QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("FakePlcRegisterClientV2.read.out 不能为空"));
    return false;
  }
  if (!ensureConnected(connected_, err)) {
    return false;
  }

  *out = registerRange(start_address, reg_count);
  return true;
}

bool FakePlcRegisterClientV2::writeHoldingRegisters(quint32 start_address,
                                                    const QVector<quint16> &values,
                                                    QString *err) {
  if (!ensureConnected(connected_, err)) {
    return false;
  }

  setRegisters(start_address, values);

  PlcFakeWriteRecordV2 rec;
  rec.start_address = start_address;
  rec.values = values;
  write_history_.push_back(rec);
  return true;
}

} // namespace core
