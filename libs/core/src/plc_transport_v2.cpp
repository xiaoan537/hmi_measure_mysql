#include "core/plc_transport_v2.hpp"

#include "core/plc_addresses_v26.hpp"

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
  if (pc_ack.reg_count != core::plc_v26::kPcAckRegs) {
    failWith(err, QStringLiteral("pc_ack reg_count 应为 %1，当前为 %2")
                      .arg(core::plc_v26::kPcAckRegs)
                      .arg(pc_ack.reg_count));
    return false;
  }
  return true;
}

} // namespace core
