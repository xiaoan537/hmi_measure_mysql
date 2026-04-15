#include "core/plc_polling_v2.hpp"

namespace core {
namespace {
void failWith(QString *err, const QString &msg) { if (err) *err = msg; }
}

PlcPollPlanV2 makeInitialPlcPollPlanV2() {
  PlcPollPlanV2 plan;
  plan.read_status = true;
  plan.read_command = true;
  plan.read_tray = false;
  plan.read_mailbox = false;
  plan.reason = QStringLiteral("初始轮询：先读 Status + Command");
  return plan;
}

PlcPollPlanV2 makeNextPlcPollPlanV2(const PlcPollCacheV2 &cache,
                                    const PlcStatusBlockV2 *status) {
  PlcPollPlanV2 plan = makeInitialPlcPollPlanV2();
  if (!status) {
    plan.reason = QStringLiteral("状态未知：继续轮询 Status + Command");
    return plan;
  }
  plan.read_tray = (status->scan_done != 0 && cache.last_scan_done == 0);
  plan.read_mailbox = (status->mailbox_ready != 0 && cache.last_mailbox_ready == 0);
  if (plan.read_tray && plan.read_mailbox) plan.reason = QStringLiteral("检测到新扫码结果和新测量包");
  else if (plan.read_tray) plan.reason = QStringLiteral("检测到新扫码结果");
  else if (plan.read_mailbox) plan.reason = QStringLiteral("检测到新测量包");
  else plan.reason = QStringLiteral("仅轮询 Status + Command");
  return plan;
}

bool decodePlcRegisterWindowV2(const PlcRegisterWindowV2 &window,
                               PlcPollDecodedV2 *out,
                               QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("decodePlcRegisterWindowV2.out 不能为空"));
    return false;
  }
  PlcPollDecodedV2 decoded;
  if (window.has_status_regs) {
    if (!buildPlcStatusBlockV25(window.status_regs, &decoded.status, err)) return false;
    decoded.has_status = true;
  }
  if (window.has_tray_regs) {
    if (!buildPlcTrayAllCodingBlockV25(window.tray_regs, &decoded.tray, err)) return false;
    decoded.has_tray = true;
  }
  if (window.has_command_regs) {
    if (!buildPlcCommandBlockV25(window.command_regs, &decoded.command, err)) return false;
    decoded.has_command = true;
  }
  if (window.has_mailbox_regs) {
    decoded.has_mailbox_block = false;
  }
  *out = decoded;
  return true;
}

PlcPollEventsV2 detectPlcPollEventsV2(const PlcPollCacheV2 &cache,
                                      const PlcPollDecodedV2 &decoded) {
  PlcPollEventsV2 events;
  if (decoded.has_status) {
    events.scan_ready = (decoded.status.scan_done != 0 && cache.last_scan_done == 0);
    events.mailbox_ready = (decoded.status.mailbox_ready != 0);
    events.new_mailbox = (decoded.status.mailbox_ready != 0 && cache.last_mailbox_ready == 0);
    events.state_seq_advanced = false;
  }
  events.command_ack_advanced = false;
  return events;
}

bool processPlcPollStepV2(const PlcRegisterWindowV2 &window,
                          const PlcPollCacheV2 &cache,
                          PlcPollStepResultV2 *out,
                          QString *err) {
  if (!out) {
    failWith(err, QStringLiteral("processPlcPollStepV2.out 不能为空"));
    return false;
  }
  PlcPollStepResultV2 step;
  if (!decodePlcRegisterWindowV2(window, &step.decoded, err)) return false;
  step.events = detectPlcPollEventsV2(cache, step.decoded);
  *out = step;
  return true;
}

void updatePlcPollCacheV2(const PlcPollDecodedV2 &decoded,
                          PlcPollCacheV2 *cache) {
  if (!cache) return;
  if (decoded.has_status) {
    cache->has_status = true;
    cache->last_scan_done = decoded.status.scan_done;
    cache->last_mailbox_ready = decoded.status.mailbox_ready;
    cache->last_scan_seq = decoded.status.scan_done;
    cache->last_meas_seq = decoded.status.mailbox_ready;
  }
  if (decoded.has_command) {
    cache->has_command = true;
  }
}

} // namespace core
