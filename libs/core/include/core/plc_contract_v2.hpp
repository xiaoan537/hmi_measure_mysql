#pragma once

#include <QString>
#include <QtGlobal>
#include "core/plc_addresses_v26.hpp"
#include "core/plc_types_v26.hpp"

namespace core {

constexpr int kLogicalSlotCount = core::plc_v26::kLogicalSlotCount;
constexpr int kCalibrationSlotIndex = core::plc_v26::kCalibrationSlotIndex;

// v2.6 固定布局（地址和长度统一来自 plc_addresses_v26.hpp）
constexpr int kStatusBlockRegsV26 = core::plc_v26::kStatusRegs;
constexpr int kCommandBlockRegsV26 = core::plc_v26::kCommandRegs;
constexpr int kTrayAllCodingBytesV26 = core::plc_v26::kTrayAllCodingBytes;
constexpr int kTrayAllCodingRegsV26 = core::plc_v26::kTrayAllCodingRegs;
constexpr int kMailboxHeaderRegsV26 = core::plc_v26::kMailboxHeaderRegs;
constexpr int kMailboxDataRegsV26 =
    core::plc_v26::kMailboxKeyenceRegs + core::plc_v26::kMailboxChuantecRegs;
constexpr int kMailboxTotalRegsV26 = core::plc_v26::kMailboxTotalRegs;
constexpr int kStatusOffsetMachineStateV26 = core::plc_v26::kStatusOffMachineState;
constexpr int kStatusOffsetStepStateV26 = core::plc_v26::kStatusOffStepState;
constexpr int kStatusOffsetInterlockMaskV26 = core::plc_v26::kStatusOffErrorMask; // DWORD，2 regs
constexpr int kStatusOffsetAlarmCodeV26 = core::plc_v26::kStatusOffAlarmCode;
constexpr int kStatusOffsetTrayPresentMaskV26 = core::plc_v26::kStatusOffTrayPresent;
constexpr int kStatusOffsetScanDoneV26 = core::plc_v26::kStatusOffScanDone;
constexpr int kStatusOffsetActiveItemCountV26 = core::plc_v26::kStatusOffActiveItemCount;
constexpr int kStatusOffsetActiveSlotMaskV26 = core::plc_v26::kStatusOffActiveSlotMask;
constexpr int kStatusOffsetMailboxReadyV26 = core::plc_v26::kStatusOffMailboxReady;
constexpr int kStatusOffsetAfterMeasurementV26 = core::plc_v26::kStatusOffAfterMeasurement;
constexpr int kCommandOffsetCategoryModeV26 = core::plc_v26::kCommandOffCategoryMode;
constexpr int kCommandOffsetCmdCodeV26 = core::plc_v26::kCommandOffCmdCode;
constexpr int kCommandOffsetCmdResultV26 = core::plc_v26::kCommandOffCmdResult;
constexpr int kCommandOffsetCmdErrorCodeV26 = core::plc_v26::kCommandOffRejectInstruction;

constexpr quint16 kInvalidSlotIndex = 0xFFFFu;

struct PlcStatusBlockV2 {
  qint16 control_mode = 0;
  quint16 machine_state = 0;
  quint16 step_state = 0;
  quint32 state_seq = 0;
  quint32 interlock_mask = 0;
  quint16 alarm_code = 0;
  quint16 alarm_level = 0;

  quint16 tray_present_mask = 0; // bit0..bit15 = slot0..slot15

  quint16 scan_done = 0;         // 1=PLC 已完成本轮扫码，工件ID块已稳定
  quint16 active_item_count = 0;                  // 当前流程正在处理的工件数：0/1/2
  quint16 active_slot_index[2] = {kInvalidSlotIndex, kInvalidSlotIndex};
  quint16 active_slot_mask = 0;

  quint16 mailbox_ready = 0;     // 1=PLC 已冻结原始测量包，可读
  quint16 after_measurement_count = 0;
};

struct PlcTrayPartIdBlockV2 {
  QString part_ids[kLogicalSlotCount];
};

struct PlcCommandBlockV2 {
  qint16 category_mode = 0;
  quint16 cmd_code = 0;
  quint32 cmd_seq = 0;
  quint32 cmd_arg0 = 0;
  quint32 cmd_arg1 = 0;
  quint32 cmd_ack_seq = 0;
  quint16 cmd_result = 0;
  quint16 cmd_error_code = 0;
  quint16 judge_result = 0;
  quint16 pc_ack = 0;
};

} // namespace core
