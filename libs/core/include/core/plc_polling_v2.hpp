#pragma once

#include <QString>
#include <QtGlobal>
#include <QVector>

#include "core/measurement_pipeline.hpp"

namespace core {

// 一次轮询从 PLC 读回来的原始寄存器窗口。
// 这里不关心具体 Modbus/TCP 客户端，只承接“已经读到的 regs”。
struct PlcRegisterWindowV2 {
  bool has_status_regs = false;
  QVector<quint16> status_regs;

  bool has_tray_regs = false;
  QVector<quint16> tray_regs;

  bool has_command_regs = false;
  QVector<quint16> command_regs;

  bool has_mailbox_regs = false;
  QVector<quint16> mailbox_regs;
};

// 统一读取服务在两次轮询之间维护的轻量缓存。
// 作用：用 seq / ready 位做去重，避免反复处理同一轮扫码、同一帧 Mailbox。
struct PlcPollCacheV2 {
  bool has_status = false;
  bool has_command = false;

  quint32 last_state_seq = 0;
  quint32 last_scan_seq = 0;
  quint32 last_meas_seq = 0;
  quint32 last_cmd_ack_seq = 0;

  quint16 last_scan_done = 0;
  quint16 last_mailbox_ready = 0;
};

// 下一拍应该去读哪些区块。
// 说明：当前阶段先做“计划骨架”，还不绑定具体寄存器地址，也不直接发 Modbus。
struct PlcPollPlanV2 {
  bool read_status = true;
  bool read_command = true;
  bool read_tray = false;
  bool read_mailbox = false;

  QString reason;
};

// 把一拍轮询读到的寄存器窗口解析成结构化对象。
struct PlcPollDecodedV2 {
  bool has_status = false;
  PlcStatusBlockV2 status;

  bool has_tray = false;
  PlcTrayPartIdBlockV2 tray;

  bool has_command = false;
  PlcCommandBlockV2 command;

  bool has_mailbox_block = false;
  PlcMailboxRegisterBlock mailbox_block;

  bool has_mailbox_frame = false;
  PlcMailboxRawFrame mailbox_frame;

  bool has_mailbox_snapshot = false;
  PlcMailboxSnapshot mailbox_snapshot;
};

// 相比上一次缓存，本拍有哪些“值得上层处理”的事件。
struct PlcPollEventsV2 {
  bool state_seq_advanced = false;
  bool scan_ready = false;          // scan_done=1 且 scan_seq 前进
  bool mailbox_ready = false;       // mailbox_ready=1
  bool new_mailbox = false;         // mailbox_ready=1 且 meas_seq 前进
  bool command_ack_advanced = false;
};

struct PlcPollStepResultV2 {
  PlcPollDecodedV2 decoded;
  PlcPollEventsV2 events;
};

PlcPollPlanV2 makeInitialPlcPollPlanV2();
PlcPollPlanV2 makeNextPlcPollPlanV2(const PlcPollCacheV2 &cache,
                                    const PlcStatusBlockV2 *status = nullptr);

bool decodePlcRegisterWindowV2(const PlcRegisterWindowV2 &window,
                               PlcPollDecodedV2 *out,
                               QString *err = nullptr);

PlcPollEventsV2 detectPlcPollEventsV2(const PlcPollCacheV2 &cache,
                                      const PlcPollDecodedV2 &decoded);

bool processPlcPollStepV2(const PlcRegisterWindowV2 &window,
                          const PlcPollCacheV2 &cache,
                          PlcPollStepResultV2 *out,
                          QString *err = nullptr);

void updatePlcPollCacheV2(const PlcPollDecodedV2 &decoded,
                          PlcPollCacheV2 *cache);

} // namespace core
