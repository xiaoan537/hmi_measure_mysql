#include "core/plc_addresses_v26.hpp"
#include "core/plc_transport_v2.hpp"

#include <cstring>

#include "core/measurement_pipeline.hpp"

namespace core {
namespace {

void failWith(QString *err, const QString &msg) {
  if (err) {
    *err = msg;
  }
}

bool ensure(bool ok, QString *err, const QString &msg) {
  if (!ok) {
    failWith(err, msg);
    return false;
  }
  return true;
}

void appendUint16(QVector<quint16> *out, quint16 v) {
  out->push_back(v);
}

void appendUint32Abcd(QVector<quint16> *out, quint32 v) {
  out->push_back(static_cast<quint16>(v & 0xFFFFu));
  out->push_back(static_cast<quint16>((v >> 16) & 0xFFFFu));
}

bool mergeWindowRegs(const PlcRegisterWindowV2 &src, PlcRegisterWindowV2 *dst,
                     QString *err) {
  if (!dst) {
    failWith(err, QStringLiteral("mergeWindowRegs.dst 不能为空"));
    return false;
  }

  if (src.has_status_regs) {
    dst->has_status_regs = true;
    dst->status_regs = src.status_regs;
  }
  if (src.has_tray_regs) {
    dst->has_tray_regs = true;
    dst->tray_regs = src.tray_regs;
  }
  if (src.has_command_regs) {
    dst->has_command_regs = true;
    dst->command_regs = src.command_regs;
  }
  if (src.has_mailbox_regs) {
    dst->has_mailbox_regs = true;
    dst->mailbox_regs = src.mailbox_regs;
  }
  return true;
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

bool readPlcRegisterSpanV2(IPlcRegisterClientV2 *client,
                           const PlcRegisterSpanV2 &span,
                           QVector<quint16> *out,
                           QString *err) {
  if (!client) {
    failWith(err, QStringLiteral("readPlcRegisterSpanV2.client 不能为空"));
    return false;
  }
  if (!out) {
    failWith(err, QStringLiteral("readPlcRegisterSpanV2.out 不能为空"));
    return false;
  }
  if (!span.isValid(err)) {
    return false;
  }

  QVector<quint16> regs;
  if (!client->readHoldingRegisters(span.start_address, span.reg_count, &regs, err)) {
    return false;
  }
  if (regs.size() != span.reg_count) {
    failWith(err, QStringLiteral("读取区块 %1 返回寄存器数不匹配：期望 %2，实际 %3")
                      .arg(span.name.isEmpty() ? QStringLiteral("<unnamed>") : span.name)
                      .arg(span.reg_count)
                      .arg(regs.size()));
    return false;
  }

  *out = regs;
  return true;
}

bool readPlcRegisterWindowByPlanV2(IPlcRegisterClientV2 *client,
                                   const PlcAddressLayoutV2 &layout,
                                   const PlcPollPlanV2 &plan,
                                   PlcRegisterWindowV2 *out,
                                   QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("readPlcRegisterWindowByPlanV2.out 不能为空"));
    return false;
  }
  if (!layout.isValid(err)) {
    return false;
  }

  PlcRegisterWindowV2 window;

  if (plan.read_status) {
    if (!readPlcRegisterSpanV2(client, layout.status, &window.status_regs, err)) {
      return false;
    }
    window.has_status_regs = true;
  }

  if (plan.read_tray) {
    if (!readPlcRegisterSpanV2(client, layout.tray, &window.tray_regs, err)) {
      return false;
    }
    window.has_tray_regs = true;
  }

  if (plan.read_command) {
    if (!readPlcRegisterSpanV2(client, layout.command, &window.command_regs, err)) {
      return false;
    }
    window.has_command_regs = true;
  }

  if (plan.read_mailbox) {
    if (!readPlcRegisterSpanV2(client, layout.mailbox, &window.mailbox_regs, err)) {
      return false;
    }
    window.has_mailbox_regs = true;
  }

  *out = window;
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
    failWith(err, QStringLiteral("slotIndex 越界：%1")
                      .arg(slotIndex));
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

bool runPlcPollCycleV2(IPlcRegisterClientV2 *client,
                       const PlcAddressLayoutV2 &layout,
                       PlcPollCacheV2 *cache,
                       PlcPollRunResultV2 *out,
                       QString *err) {
  if (!client) {
    failWith(err, QStringLiteral("runPlcPollCycleV2.client 不能为空"));
    return false;
  }
  if (!cache) {
    failWith(err, QStringLiteral("runPlcPollCycleV2.cache 不能为空"));
    return false;
  }
  if (!out) {
    failWith(err, QStringLiteral("runPlcPollCycleV2.out 不能为空"));
    return false;
  }
  if (!layout.isValid(err)) {
    return false;
  }

  PlcPollRunResultV2 result;
  result.bootstrap_plan = makeInitialPlcPollPlanV2();

  PlcRegisterWindowV2 bootstrapWindow;
  if (!readPlcRegisterWindowByPlanV2(client, layout, result.bootstrap_plan,
                                     &bootstrapWindow, err)) {
    return false;
  }

  PlcPollStepResultV2 bootstrapStep;
  if (!processPlcPollStepV2(bootstrapWindow, *cache, &bootstrapStep, err)) {
    return false;
  }

  const PlcStatusBlockV2 *statusPtr = bootstrapStep.decoded.has_status
                                          ? &bootstrapStep.decoded.status
                                          : nullptr;
  result.final_plan = makeNextPlcPollPlanV2(*cache, statusPtr);

  PlcRegisterWindowV2 finalWindow = bootstrapWindow;
  PlcPollStepResultV2 finalStep = bootstrapStep;

  if (result.final_plan.read_tray || result.final_plan.read_mailbox) {
    PlcPollPlanV2 supplementPlan;
    supplementPlan.read_status = false;
    supplementPlan.read_command = false;
    supplementPlan.read_tray = result.final_plan.read_tray;
    supplementPlan.read_mailbox = result.final_plan.read_mailbox;
    supplementPlan.reason = result.final_plan.reason;

    PlcRegisterWindowV2 supplementWindow;
    if (!readPlcRegisterWindowByPlanV2(client, layout, supplementPlan,
                                       &supplementWindow, err)) {
      return false;
    }
    if (!mergeWindowRegs(supplementWindow, &finalWindow, err)) {
      return false;
    }
    if (!processPlcPollStepV2(finalWindow, *cache, &finalStep, err)) {
      return false;
    }
  }

  updatePlcPollCacheV2(finalStep.decoded, cache);

  result.window = finalWindow;
  result.step = finalStep;
  *out = result;
  return true;
}

} // namespace core
