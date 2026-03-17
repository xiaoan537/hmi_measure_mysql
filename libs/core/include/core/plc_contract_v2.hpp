#pragma once

#include <QString>
#include <QtGlobal>

namespace core {

constexpr int kLogicalSlotCount = 16;
constexpr int kAutoSlotCount = 15;
constexpr int kCalibrationSlotIndex = 15;

// 当前协议的核心原则：
// 1) PLC 负责运动控制、扫码、槽位有无、冻结测量包；
// 2) PC 负责任务卡比对、人工纠错、算法计算、公差判定、落盘/MES；
// 3) PLC 不再作为最终 OK/NG 的真相源；PC 也不回写计算结果块，只写 pc_ack。
// 4) 生产业务模式由 PC 在 START_AUTO 时声明：NORMAL/SECOND/THIRD/MIL；复测不属于顶部模式，而是 NG 后即时动作。

enum class PlcRunKind : quint16 {
  None = 0,
  Auto = 1,
  Calibration = 2,
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

  quint16 run_kind = 0;          // PlcRunKind
  quint16 tray_present_mask = 0; // bit0..bit15 = slot0..slot15

  quint16 scan_done = 0;         // 1=PLC 已完成本轮扫码，工件ID块已稳定
  quint32 scan_seq = 0;          // 每完成一轮扫码自增

  quint16 mailbox_ready = 0;     // 1=PLC 已冻结原始测量包，可读
  quint32 meas_seq = 0;          // 每冻结一帧测量包自增
};

struct PlcTrayPartIdBlockV2 {
  QString part_ids[kLogicalSlotCount];
};

struct PlcMailboxHeaderV2 {
  quint32 meas_seq = 0;
  quint16 run_kind = 0;   // PlcRunKind
  quint16 part_type = 0;  // 1=A, 2=B
  quint16 item_count = 0; // 1 or 2

  quint16 slot_index[2] = {0, 0};
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
