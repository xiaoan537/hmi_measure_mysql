#include "core/plc_polling_v2.hpp"

namespace core {
namespace {

void failWith(QString *err, const QString &msg) {
  if (err) {
    *err = msg;
  }
}

} // namespace

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
  PlcPollPlanV2 plan;
  plan.read_status = true;
  plan.read_command = true;
  plan.read_tray = false;
  plan.read_mailbox = false;

  if (!status) {
    plan.reason = cache.has_status ? QStringLiteral("状态未知：继续基础轮询 Status + Command")
                                   : QStringLiteral("尚未建立状态缓存：继续基础轮询 Status + Command");
    return plan;
  }

  if (status->scan_done != 0 && status->scan_seq != 0 &&
      (status->scan_seq != cache.last_scan_seq || cache.last_scan_done == 0)) {
    plan.read_tray = true;
  }

  if (status->mailbox_ready != 0 && status->meas_seq != 0 &&
      (status->meas_seq != cache.last_meas_seq || cache.last_mailbox_ready == 0)) {
    plan.read_mailbox = true;
  }

  if (plan.read_tray && plan.read_mailbox) {
    plan.reason = QStringLiteral("检测到新扫码结果与新 Mailbox：本拍读取 Status + Command + Tray + Mailbox");
  } else if (plan.read_tray) {
    plan.reason = QStringLiteral("检测到新扫码结果：本拍读取 Status + Command + Tray");
  } else if (plan.read_mailbox) {
    plan.reason = QStringLiteral("检测到新 Mailbox：本拍读取 Status + Command + Mailbox");
  } else {
    plan.reason = QStringLiteral("无新增扫描/测量帧：本拍仅轮询 Status + Command");
  }
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
    if (!buildPlcStatusBlockV2(window.status_regs, &decoded.status, err)) {
      return false;
    }
    decoded.has_status = true;
  }

  if (window.has_tray_regs) {
    if (!buildPlcTrayPartIdBlockV2(window.tray_regs, &decoded.tray, err)) {
      return false;
    }
    decoded.has_tray = true;
  }

  if (window.has_command_regs) {
    if (!buildPlcCommandBlockV2(window.command_regs, &decoded.command, err)) {
      return false;
    }
    decoded.has_command = true;
  }

  if (window.has_mailbox_regs) {
    if (!splitPlcMailboxRegisters(window.mailbox_regs, &decoded.mailbox_block, err)) {
      return false;
    }
    decoded.has_mailbox_block = true;

    if (!buildPlcMailboxRawFrame(decoded.mailbox_block, &decoded.mailbox_frame, err)) {
      return false;
    }
    decoded.has_mailbox_frame = true;

    if (!buildPlcMailboxSnapshot(decoded.mailbox_frame, &decoded.mailbox_snapshot, err)) {
      return false;
    }
    decoded.has_mailbox_snapshot = true;
  }

  *out = decoded;
  return true;
}

PlcPollEventsV2 detectPlcPollEventsV2(const PlcPollCacheV2 &cache,
                                      const PlcPollDecodedV2 &decoded) {
  PlcPollEventsV2 events;

  if (decoded.has_status) {
    events.state_seq_advanced = (!cache.has_status) ||
                                (decoded.status.state_seq != cache.last_state_seq);

    events.mailbox_ready = (decoded.status.mailbox_ready != 0);

    events.scan_ready = (decoded.status.scan_done != 0) &&
                        (((decoded.status.scan_seq != 0) &&
                          (decoded.status.scan_seq != cache.last_scan_seq)) ||
                         (cache.last_scan_done == 0));

    events.new_mailbox = (decoded.status.mailbox_ready != 0) &&
                         (((decoded.status.meas_seq != 0) &&
                           (decoded.status.meas_seq != cache.last_meas_seq)) ||
                          (cache.last_mailbox_ready == 0));
  }

  if (decoded.has_command) {
    events.command_ack_advanced = (!cache.has_command) ||
                                  (decoded.command.cmd_ack_seq != cache.last_cmd_ack_seq);
  }

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
  if (!decodePlcRegisterWindowV2(window, &step.decoded, err)) {
    return false;
  }
  step.events = detectPlcPollEventsV2(cache, step.decoded);
  *out = step;
  return true;
}

void updatePlcPollCacheV2(const PlcPollDecodedV2 &decoded,
                          PlcPollCacheV2 *cache) {
  if (!cache) {
    return;
  }

  if (decoded.has_status) {
    cache->has_status = true;
    cache->last_state_seq = decoded.status.state_seq;
    cache->last_scan_seq = decoded.status.scan_seq;
    cache->last_meas_seq = decoded.status.meas_seq;
    cache->last_scan_done = decoded.status.scan_done;
    cache->last_mailbox_ready = decoded.status.mailbox_ready;
  }

  if (decoded.has_command) {
    cache->has_command = true;
    cache->last_cmd_ack_seq = decoded.command.cmd_ack_seq;
  }
}

} // namespace core
