#pragma once

#include <QString>
#include <QtGlobal>

namespace core {

constexpr int kLogicalSlotCount = 16;
constexpr int kAutoSlotCount = 15;
constexpr int kCalibrationSlotIndex = 15;
constexpr int kTrayPartIdAsciiChars = 32;
constexpr int kTrayPartIdRegsPerSlot = 16;

constexpr int kMailboxHeaderUsedRegsV2 = 54;          // 当前有效字段长度
constexpr int kMailboxHeaderReservedTailRegsV2 = 4;   // 物理头块尾部预留
constexpr int kMailboxHeaderBlockRegsV2 =
    kMailboxHeaderUsedRegsV2 + kMailboxHeaderReservedTailRegsV2; // 54 + 4 = 58
constexpr int kMailboxArrayRegsReservedV2 = 2304;
constexpr int kMailboxArrayFloatCountReservedV2 = kMailboxArrayRegsReservedV2 / 2;
constexpr int kMailboxTotalRegsV2 = kMailboxHeaderBlockRegsV2 + kMailboxArrayRegsReservedV2;

constexpr quint16 kInvalidSlotIndex = 0xFFFFu;

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

  CalWaitLoadSlot15 = 200,
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
  SetModeAuto = 100,
  SetModeManual = 101,
  StartAuto = 110,
  StartCalibration = 111,
  Pause = 120,
  Resume = 121,
  Stop = 122,
  ResetAlarm = 130,
  HomeAll = 140,

  ContinueAfterIdCheck = 200,
  RequestRescanIds = 201,
  ContinueAfterNgConfirm = 202,
  StartRetestCurrent = 203,

  GripperOpen = 300,
  GripperClose = 301,
  ClampScan = 302,
  UnclampScan = 303,
  ClampLen = 304,
  UnclampLen = 305,
};

struct PlcStatusBlockV2 {
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

  quint16 mailbox_ready = 0;     // 1=PLC 已冻结原始测量包，可读
  quint32 meas_seq = 0;          // 每冻结一帧测量包自增
};

struct PlcTrayPartIdBlockV2 {
  QString part_ids[kLogicalSlotCount];
};

struct PlcMailboxHeaderV2 {
  quint32 meas_seq = 0;
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
