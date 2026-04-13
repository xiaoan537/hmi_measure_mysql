#pragma once

#include <QString>
#include <QtGlobal>

namespace core {

constexpr int kLogicalSlotCount = 16;
constexpr int kAutoSlotCount = 15;
// UI / DB / 本地内存仍使用 0..15 的逻辑槽位索引；协议层槽位号使用 1..16。
constexpr int kCalibrationSlotIndex = 15;      // 本地逻辑索引，对应协议槽位 16
constexpr int kProtocolSlotMinV24 = 1;
constexpr int kProtocolSlotMaxV24 = 16;
constexpr int kProtocolCalibrationSlotV24 = 16;
constexpr int kTrayPartIdAsciiChars = 32;
constexpr int kTrayPartIdRegsPerSlot = 16;

// 第一阶段联调：直接读取 PLC 功能块并在 PC 侧组装 v2.4 Mailbox。
constexpr int kFirstStageCodingRegsV24 = 81;
constexpr int kFirstStageKeyenceRegsV24 = 16;
constexpr int kFirstStageChuantecRegsV24 = 1152;
constexpr int kFirstStageRPosRegsV24 = 1152;
constexpr float kFirstStageInvalidRawValueV24 = 2147.48364f;

// 第二阶段（v2.5）固定布局
constexpr int kStatusBlockRegsV25 = 16;   // 从 MB7202 开始到 MB7232，按寄存器窗口读取
constexpr int kCommandBlockRegsV25 = 4;   // iCategory / wCmd_Code / wCmd_result / wReject_Instruction
constexpr int kTrayAllCodingBytesV25 = 16 * 81;
constexpr int kTrayAllCodingRegsV25 = kTrayAllCodingBytesV25 / 2;
constexpr int kMailboxHeaderRegsV25 = 83;  // item_count(1) + slot_mask(1) + 2*STRING(81B)=83regs
constexpr int kMailboxDataRegsV25 = 16 + 1152;
constexpr int kMailboxTotalRegsV25 = kMailboxHeaderRegsV25 + kMailboxDataRegsV25;
constexpr int kStatusOffsetMachineStateV25 = 0;
constexpr int kStatusOffsetStepStateV25 = 1;
constexpr int kStatusOffsetInterlockMaskV25 = 2; // DWORD，使用 2 个寄存器
constexpr int kStatusOffsetAlarmCodeV25 = 9;
constexpr int kStatusOffsetTrayPresentMaskV25 = 10;
constexpr int kStatusOffsetScanDoneV25 = 11;
constexpr int kStatusOffsetActiveItemCountV25 = 12;
constexpr int kStatusOffsetActiveSlotMaskV25 = 13;
constexpr int kStatusOffsetMailboxReadyV25 = 14;
constexpr int kStatusOffsetAfterMeasurementV25 = 15;
constexpr int kCommandOffsetCategoryModeV25 = 0;
constexpr int kCommandOffsetCmdCodeV25 = 1;
constexpr int kCommandOffsetCmdResultV25 = 2;
constexpr int kCommandOffsetCmdErrorCodeV25 = 3;

constexpr int kMailboxHeaderUsedRegsV2 = 54;          // 当前有效字段长度
constexpr int kMailboxHeaderReservedTailRegsV2 = 4;   // 物理头块尾部预留
constexpr int kMailboxHeaderBlockRegsV2 =
    kMailboxHeaderUsedRegsV2 + kMailboxHeaderReservedTailRegsV2; // 54 + 4 = 58
constexpr int kMailboxArrayRegsReservedV2 = 2304;
constexpr int kMailboxArrayFloatCountReservedV2 = kMailboxArrayRegsReservedV2 / 2;
constexpr int kMailboxTotalRegsV2 = kMailboxHeaderBlockRegsV2 + kMailboxArrayRegsReservedV2;

constexpr quint16 kInvalidSlotIndex = 0xFFFFu;
constexpr quint16 kProtocolInvalidSlotIndexV24 = 0u;

inline bool isValidProtocolSlotV24(quint16 slot) { return slot >= kProtocolSlotMinV24 && slot <= kProtocolSlotMaxV24; }
inline int logicalSlotIndexFromProtocolSlotV24(quint16 slot) { return isValidProtocolSlotV24(slot) ? static_cast<int>(slot) - 1 : -1; }
inline quint16 protocolSlotFromLogicalIndexV24(int logicalIndex) { return (logicalIndex >= 0 && logicalIndex < kLogicalSlotCount) ? static_cast<quint16>(logicalIndex + 1) : kProtocolInvalidSlotIndexV24; }

constexpr int kStatusBlockRegsV2 = 18;
constexpr int kStatusOffsetMachineState = 0;      // uint16
constexpr int kStatusOffsetStepState = 1;         // uint16
constexpr int kStatusOffsetStateSeq = 2;          // uint32, 2 regs
constexpr int kStatusOffsetInterlockMask = 4;     // uint32, 2 regs
constexpr int kStatusOffsetAlarmCode = 6;         // uint16
constexpr int kStatusOffsetAlarmLevel = 7;        // uint16
constexpr int kStatusOffsetTrayPresentMask = 8;   // uint16
constexpr int kStatusOffsetScanDone = 9;          // uint16/bit
constexpr int kStatusOffsetScanSeq = 10;          // uint32, 2 regs
constexpr int kStatusOffsetActiveItemCount = 12;  // uint16
constexpr int kStatusOffsetActiveSlotIndex0 = 13; // uint16
constexpr int kStatusOffsetActiveSlotIndex1 = 14; // uint16
constexpr int kStatusOffsetMailboxReady = 15;     // uint16/bit
constexpr int kStatusOffsetMeasSeq = 16;          // uint32, 2 regs

constexpr int kTrayPartIdBlockRegsV2 = kLogicalSlotCount * kTrayPartIdRegsPerSlot; // 16 * 16 = 256

constexpr int kCommandBlockRegsV2 = 11;
constexpr int kCommandOffsetCmdCode = 0;       // uint16
constexpr int kCommandOffsetCmdSeq = 1;        // uint32, 2 regs
constexpr int kCommandOffsetCmdArg0 = 3;       // uint32, 2 regs
constexpr int kCommandOffsetCmdArg1 = 5;       // uint32, 2 regs
constexpr int kCommandOffsetCmdAckSeq = 7;     // uint32, 2 regs
constexpr int kCommandOffsetCmdResult = 9;     // uint16
constexpr int kCommandOffsetCmdErrorCode = 10; // uint16

constexpr int kMailboxOffsetMeasSeq = 0;          // uint32, 2 regs
constexpr int kMailboxOffsetPartType = 2;         // uint16
constexpr int kMailboxOffsetItemCount = 3;        // uint16
constexpr int kMailboxOffsetSlotIndex0 = 4;       // uint16
constexpr int kMailboxOffsetSlotIndex1 = 5;       // uint16
constexpr int kMailboxOffsetPartIdAscii0 = 6;     // ASCII32, 16 regs
constexpr int kMailboxOffsetPartIdAscii1 = 22;    // ASCII32, 16 regs
constexpr int kMailboxOffsetTotalLen0 = 38;       // float32, 2 regs
constexpr int kMailboxOffsetTotalLen1 = 40;       // float32, 2 regs
constexpr int kMailboxOffsetAdLen0 = 42;          // float32, 2 regs
constexpr int kMailboxOffsetAdLen1 = 44;          // float32, 2 regs
constexpr int kMailboxOffsetBcLen0 = 46;          // float32, 2 regs
constexpr int kMailboxOffsetBcLen1 = 48;          // float32, 2 regs
constexpr int kMailboxOffsetRawLayoutVer = 50;    // uint16
constexpr int kMailboxOffsetRingCount = 51;       // uint16
constexpr int kMailboxOffsetPointCount = 52;      // uint16
constexpr int kMailboxOffsetChannelCount = 53;    // uint16
constexpr int kMailboxOffsetReservedTail0 = 54;    // uint16 reserved
constexpr int kMailboxOffsetReservedTail1 = 55;    // uint16 reserved
constexpr int kMailboxOffsetReservedTail2 = 56;    // uint16 reserved
constexpr int kMailboxOffsetReservedTail3 = 57;    // uint16 reserved

// 当前协议的核心原则：
// 1) PLC 负责运动控制、扫码、槽位有无、冻结测量包；
// 2) PC 负责任务卡比对、人工纠错、算法计算、公差判定、落盘/MES；
// 3) PLC 不再作为最终 OK/NG 的真相源；PC 也不回写计算结果块，只写 pc_ack。
// 4) 生产业务模式由 PC 在 START_AUTO 时声明：NORMAL/SECOND/THIRD/MIL；复测不属于顶部模式，而是 NG 后即时动作。
// 5) 当前正在处理哪一个/哪两个槽位，属于 PLC 实时状态，应放在 Status Block；
//    Mailbox 里的 slot_index[0/1] 仅表示最终冻结测量包对应的槽位。
// 6) Mailbox Header 当前有效字段长度为 54 regs，但物理头块固定预留 58 regs；
//    尾部 4 regs 作为 reserved，Arrays 起始偏移保持在 58，便于后续扩展且不打乱数组区。

enum class PlcControlModeV25 : qint16 {
  Manual = 1,
  Auto = 2,
  SingleStep = 3,
};

enum class PlcMachineState : quint16 {
  Idle = 0,
  Auto = 1,
  Manual = 2,
  Paused = 3,
  Fault = 900,
  EStop = 910,
};

enum class PlcStepStateV2 : quint16 {
  WaitStart = 0,
  WaitTrayReady = 10,
  ScanTrayIds = 20,
  WaitPcIdCheck = 30,
  PickFromTray = 40,
  MoveToStations = 50,
  PlaceToStations = 60,
  MeasureActive = 70,
  GenerateMailbox = 80,
  WaitPcRead = 90,
  ReturnToTray = 100,
  CycleComplete = 110,

  CalWaitLoadSlot16 = 200,
  CalWaitPcConfirm = 210,
  CalMeasure = 220,
  CalWaitPcRead = 230,
  CalComplete = 240,

  Fault = 900,
  EStop = 910,
};


enum class ProductionMeasureModeV2 : quint16 {
  Normal = 1,
  Second = 2,
  Third = 3,
  Mil = 9,
};

enum class PlcCommandCodeV2 : quint16 {
  SetModeAuto = 0,
  SetModeManual = 0,
  Initialize = 0x0001,
  StartAuto = 0x0002,
  StartCalibration = 0x0004,
  Stop = 0x0008,
  ResetAlarm = 0x0010,
  HomeAll = 0x0010,

  // 以下命令当前仍保留为扩展位；若 PLC 后续给出明确位图定义，再同步收敛。
  Pause = 0x0020,
  Resume = 0x0040,
  ContinueAfterIdCheck = 0x0080,
  RequestRescanIds = 0x0100,
  ContinueAfterNgConfirm = 0x0200,
  StartRetestCurrent = 0x0400,

  GripperOpen = 0x0800,
  GripperClose = 0x1000,
  ClampScan = 0x2000,
  UnclampScan = 0x4000,
  ClampLen = 0x8000,
  UnclampLen = 0x8000,
};

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
  quint32 scan_seq = 0;          // 每完成一轮扫码自增

  quint16 active_item_count = 0;                  // 当前流程正在处理的工件数：0/1/2
  quint16 active_slot_index[2] = {kInvalidSlotIndex, kInvalidSlotIndex};
  quint16 active_slot_mask = 0;

  quint16 mailbox_ready = 0;     // 1=PLC 已冻结原始测量包，可读
  quint32 meas_seq = 0;          // 每冻结一帧测量包自增
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

struct PlcMailboxHeaderV2 {
  quint32 meas_seq = 0;
  quint16 active_slot_mask = 0;
  quint16 part_type = 0;  // 1=A, 2=B
  quint16 item_count = 0; // 1 or 2

  quint16 slot_index[2] = {0, kInvalidSlotIndex};
  QString part_id_ascii[2];

  float total_len_mm[2] = {0.0f, 0.0f};
  float ad_len_mm[2] = {0.0f, 0.0f};
  float bc_len_mm[2] = {0.0f, 0.0f};

  quint16 raw_layout_ver = 1;
  quint16 ring_count = 0;
  quint16 point_count = 0;
  quint16 channel_count = 0;
};

} // namespace core
