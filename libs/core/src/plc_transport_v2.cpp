#include "core/plc_transport_v2.hpp"

#include "core/plc_addresses_v26.hpp"
#include "core/measurement_pipeline.hpp"

namespace core {
namespace {

void failWith(QString *err, const QString &msg) {
  if (err) {
    *err = msg;
  }
}

} // namespace

bool PlcRegisterSpanV2::isValid(QString *err) const {
  if (reg_count == 0) {
    failWith(err, QStringLiteral("寄存器区块 %1 的 reg_count 不能为 0")
                      .arg(name.isEmpty() ? QStringLiteral("<unnamed>") : name));
    return false;
  }
  return true;
}

bool PlcAddressLayoutV2::isValid(QString *err) const {
  if (!status.isValid(err) || !tray.isValid(err) || !command.isValid(err) ||
      !mailbox.isValid(err) || !pc_ack.isValid(err)) {
    return false;
  }

  if (status.reg_count != core::plc_v26::kStatusRegs) {
    failWith(err, QStringLiteral("Status Block reg_count 应为 %1，当前为 %2")
                      .arg(core::plc_v26::kStatusRegs)
                      .arg(status.reg_count));
    return false;
  }
  if (tray.reg_count != core::plc_v26::kTrayAllCodingRegs) {
    failWith(err, QStringLiteral("Tray Part-ID Block reg_count 应为 %1，当前为 %2")
                      .arg(core::plc_v26::kTrayAllCodingRegs)
                      .arg(tray.reg_count));
    return false;
  }
  if (command.reg_count != core::plc_v26::kCommandRegs) {
    failWith(err, QStringLiteral("Command Block reg_count 应为 %1，当前为 %2")
                      .arg(core::plc_v26::kCommandRegs)
                      .arg(command.reg_count));
    return false;
  }
  if (mailbox.reg_count != core::plc_v26::kMailboxTotalRegs) {
    failWith(err, QStringLiteral("Mailbox Block reg_count 应为 %1，当前为 %2")
                      .arg(core::plc_v26::kMailboxTotalRegs)
                      .arg(mailbox.reg_count));
    return false;
  }
  if (pc_ack.reg_count != kPcAckWriteRegsV2) {
    failWith(err, QStringLiteral("pc_ack reg_count 应为 %1，当前为 %2")
                      .arg(kPcAckWriteRegsV2)
                      .arg(pc_ack.reg_count));
    return false;
  }
  return true;
}

bool encodePlcCommandWriteRegsV2(const PlcCommandBlockV2 &command,
                                 QVector<quint16> *out,
                                 QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("encodePlcCommandWriteRegsV2.out 不能为空"));
    return false;
  }

  QVector<quint16> regs;
  regs.reserve(kCommandWriteRegsV2);
  regs.push_back(static_cast<quint16>(command.category_mode));
  regs.push_back(command.cmd_code);

  if (regs.size() != kCommandWriteRegsV2) {
    failWith(err, QStringLiteral("编码 Command Block 写入区失败：寄存器数应为 %1，实际 %2")
                      .arg(kCommandWriteRegsV2)
                      .arg(regs.size()));
    return false;
  }

  *out = regs;
  return true;
}

bool encodePcAckWriteRegsV2(quint16 pc_ack,
                            QVector<quint16> *out,
                            QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("encodePcAckWriteRegsV2.out 不能为空"));
    return false;
  }

  QVector<quint16> regs;
  regs.reserve(kPcAckWriteRegsV2);
  regs.push_back(pc_ack);
  *out = regs;
  return true;
}

bool writePlcCommandV2(IPlcRegisterClientV2 *client,
                       const PlcAddressLayoutV2 &layout,
                       const PlcCommandBlockV2 &command,
                       QString *err) {
  if (!client) {
    failWith(err, QStringLiteral("writePlcCommandV2.client 不能为空"));
    return false;
  }
  if (!layout.isValid(err)) {
    return false;
  }

  QVector<quint16> regs;
  if (!encodePlcCommandWriteRegsV2(command, &regs, err)) {
    return false;
  }
  return client->writeHoldingRegisters(layout.command.start_address, regs, err);
}

bool writePlcPcAckV2(IPlcRegisterClientV2 *client,
                     const PlcAddressLayoutV2 &layout,
                     quint16 pc_ack,
                     QString *err) {
  if (!client) {
    failWith(err, QStringLiteral("writePlcPcAckV2.client 不能为空"));
    return false;
  }
  if (!layout.isValid(err)) {
    return false;
  }

  QVector<quint16> regs;
  if (!encodePcAckWriteRegsV2(pc_ack, &regs, err)) {
    return false;
  }
  return client->writeHoldingRegisters(layout.pc_ack.start_address, regs, err);
}

bool writePlcTrayPartIdSlotV2(IPlcRegisterClientV2 *client,
                              const PlcAddressLayoutV2 &layout,
                              int slotIndex,
                              const QString &partId,
                              QString *err) {
  if (!client) {
    failWith(err, QStringLiteral("writePlcTrayPartIdSlotV2.client 不能为空"));
    return false;
  }
  if (!layout.isValid(err)) {
    return false;
  }
  if (slotIndex < 0 || slotIndex >= kLogicalSlotCount) {
    failWith(err, QStringLiteral("slotIndex 越界：%1").arg(slotIndex));
    return false;
  }

  QVector<quint16> regs;
  if (!encodePlcTrayPartIdSlotRegs(partId, &regs, err)) {
    return false;
  }
  if (regs.size() != kTrayPartIdRegsPerSlot) {
    failWith(err, QStringLiteral("单槽 Part-ID 编码寄存器数应为 %1，实际 %2")
                      .arg(kTrayPartIdRegsPerSlot)
                      .arg(regs.size()));
    return false;
  }

  const quint32 start = layout.tray.start_address +
                        static_cast<quint32>(slotIndex * kTrayPartIdRegsPerSlot);
  return client->writeHoldingRegisters(start, regs, err);
}

} // namespace core
