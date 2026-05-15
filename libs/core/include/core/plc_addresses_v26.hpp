#pragma once
#include <QtGlobal>
#include <QString>
#include <QStringList>
#include <array>

namespace core::plc_v26 {

constexpr int kLogicalSlotCount = 16;
constexpr int kAutoSlotCount = 16;
constexpr int kCalibrationSlotIndex = 0; // logical index, protocol slot 1 (calibration only)
constexpr int kProtocolInvalidSlot = 0;
constexpr int kProtocolMinSlot = 1;
constexpr int kProtocolMaxSlot = 16;

constexpr quint16 kModeManual = 1;
constexpr quint16 kModeAuto = 2;
constexpr quint16 kModeSingleStep = 3;

constexpr quint16 kPartTypeB = 1;
constexpr quint16 kPartTypeA = 2;

constexpr quint16 kCmdInitializeBit = 0x0001;
constexpr quint16 kCmdStartMeasureBit = 0x0002;
constexpr quint16 kCmdStartCalibrationBit = 0x0004;
constexpr quint16 kCmdStopBit = 0x0008;
constexpr quint16 kCmdResetBit = 0x0010;
constexpr quint16 kCmdRetestCurrentBit = 0x0020;
constexpr quint16 kCmdContinueWithoutRetestBit = 0x0040;
constexpr quint16 kCmdAlarmMuteBit = 0x4000;
constexpr quint16 kCmdRejectMask = 0x8000;

constexpr quint16 kJudgeUnknown = 0;
constexpr quint16 kJudgeOk = 1;
constexpr quint16 kJudgeNg = 2;

// register addresses (0-based holding register)
constexpr quint32 kRegMode = 3600;              // MB7200
constexpr quint32 kRegStatusStart = 3601;       // MB7202
constexpr quint32 kRegCommandStart = 3650;      // MB7300
constexpr quint32 kRegPcAck = 3675;             // MB7350
constexpr quint32 kRegJudgeResult = 3676;       // MB7352
constexpr quint32 kRegSamplePointCount = 3677;  // MB7354
constexpr quint32 kRegTrayAllCoding = 3700;     // MB7400

constexpr quint32 kRegMailboxStart = 1197;      // MB2394
constexpr quint32 kRegKeyenceStart = 1280;      // MB2560
constexpr quint32 kRegChuantecStart = 1296;     // MB2592, 72-point data area
constexpr quint32 kRegChuantec72Start = kRegChuantecStart;
constexpr quint32 kRegChuantec180Start = 10080; // MB20160, 180-point data area
constexpr quint32 kRegRPosStart = 2448;         // MB4896

constexpr int kStatusRegs = 16;
constexpr int kCommandRegs = 4;
constexpr int kPcAckRegs = 1;
constexpr int kJudgeResultRegs = 1;
constexpr int kTraySlotBytes = 81;
constexpr int kTrayAllCodingBytes = kLogicalSlotCount * kTraySlotBytes;
constexpr int kTrayAllCodingRegs = (kTrayAllCodingBytes + 1) / 2;
constexpr int kMailboxHeaderBytes = 2 + 2 + (2 * kTraySlotBytes); // item_count + slot_mask + 2 strings
constexpr int kMailboxHeaderRegs = (kMailboxHeaderBytes + 1) / 2;  // 83 regs
constexpr int kMailboxKeyenceRegs = 16; // 4 x LREAL
constexpr int kMailboxFixedRegs = kMailboxHeaderRegs + kMailboxKeyenceRegs;
constexpr int kMailboxPointCount72 = 72;
constexpr int kMailboxPointCount180 = 180;
constexpr int kMailboxCurveChannels = 8;
constexpr int kMailboxChuantec72Regs = kMailboxCurveChannels * kMailboxPointCount72 * 2; // 8 x 72 x REAL / 2 regs
constexpr int kMailboxChuantec180Regs = kMailboxCurveChannels * kMailboxPointCount180 * 2; // 8 x 180 x REAL / 2 regs
constexpr int kMailboxChuantecRegs = kMailboxChuantec72Regs; // historical 72-point layout
constexpr int kMailboxTotalRegs = kMailboxHeaderRegs + kMailboxKeyenceRegs + kMailboxChuantecRegs;

// status offsets from kRegStatusStart
constexpr int kStatusOffMachineState = 0;
constexpr int kStatusOffStepState = 1;
constexpr int kStatusOffErrorMask = 2;          // DWORD = 2 regs
constexpr int kStatusOffAlarmCode = 9;
constexpr int kStatusOffTrayPresent = 10;
constexpr int kStatusOffScanDone = 11;
constexpr int kStatusOffActiveItemCount = 12;
constexpr int kStatusOffActiveSlotMask = 13;
constexpr int kStatusOffMailboxReady = 14;
constexpr int kStatusOffAfterMeasurement = 15;

// command offsets from kRegCommandStart
constexpr int kCommandOffCategoryMode = 0;
constexpr int kCommandOffCmdCode = 1;
constexpr int kCommandOffCmdResult = 2;
constexpr int kCommandOffRejectInstruction = 3;

constexpr float kInvalidRawThreshold = 5.0f;

inline bool isValidMailboxPointCount(int pointCount) {
  return pointCount == kMailboxPointCount72 || pointCount == kMailboxPointCount180;
}
inline int normalizeMailboxPointCount(int pointCount) {
  return pointCount == kMailboxPointCount180 ? kMailboxPointCount180 : kMailboxPointCount72;
}
inline quint32 chuantecRegStartForPointCount(int pointCount) {
  return normalizeMailboxPointCount(pointCount) == kMailboxPointCount180 ? kRegChuantec180Start : kRegChuantec72Start;
}
inline int chuantecRegsForPointCount(int pointCount) {
  return normalizeMailboxPointCount(pointCount) == kMailboxPointCount180 ? kMailboxChuantec180Regs : kMailboxChuantec72Regs;
}
inline int chuantecFloatCountForPointCount(int pointCount) {
  return kMailboxCurveChannels * normalizeMailboxPointCount(pointCount);
}

constexpr int kAxisCount = 11;
constexpr quint32 kAxisCtrlMbBase = 0u;       // g_aPC_AxisCtrl[1] = MB0
constexpr quint32 kAxisCtrlStrideBytes = 40u; // AxisIN stride
constexpr quint32 kAxisStateMbBase = 600u;    // g_aPC_AxisSta[1] = MB600
constexpr quint32 kAxisStateStrideBytes = 24u; // AxisOUT stride

constexpr int kClCylinderCount = 4;  // g_aPC_CLCtrl[4] is the original grab cylinder.
constexpr int kGt2CylinderCount = 4;
constexpr quint32 kClCtrlMbBase = 1000u;
constexpr quint32 kGt2CtrlMbBase = 1004u;
constexpr quint32 kClStateMbBase = 1200u;
constexpr quint32 kGt2StateMbBase = 1216u;
constexpr quint32 kCylinderStateStrideBytes = 4u;

// Historical names kept for service compatibility. These are bit offsets now,
// matching %MX(base).0 ... %MX(base+1).0 in the v2.6 motion table.
constexpr quint32 kAxisCtrlByteEnable = 0;
constexpr quint32 kAxisCtrlByteReset = 1;
constexpr quint32 kAxisCtrlByteHome = 2;
constexpr quint32 kAxisCtrlByteEStop = 3;
constexpr quint32 kAxisCtrlByteStop = 4;
constexpr quint32 kAxisCtrlByteMoveAbs = 5;
constexpr quint32 kAxisCtrlByteMoveRel = 6;
constexpr quint32 kAxisCtrlByteJogForward = 7;
constexpr quint32 kAxisCtrlByteJogBackward = 8;

constexpr quint32 kAxisCtrlParamByteAcc = 8;
constexpr quint32 kAxisCtrlParamByteDec = 16;
constexpr quint32 kAxisCtrlParamBytePosition = 24;
constexpr quint32 kAxisCtrlParamByteVelocity = 32;

constexpr quint32 kAxisStateBytes = 24;
constexpr quint32 kAxisStateRegCount = 12;
constexpr quint32 kAxisStateBitEnabled = 0;
constexpr quint32 kAxisStateBitHomed = 1;
constexpr quint32 kAxisStateBitError = 2;
constexpr quint32 kAxisStateBitBusy = 3;
constexpr quint32 kAxisStateBitDone = 4;
constexpr quint32 kAxisStateByteErrorId = 2;
constexpr quint32 kAxisStateByteActPosition = 8;
constexpr quint32 kAxisStateByteActVelocity = 16;

constexpr quint32 kCylinderCtrlBytes = 1;
constexpr quint32 kCylinderStateBytes = 4;
constexpr quint32 kCylinderBitP = 0;
constexpr quint32 kCylinderBitN = 1;
constexpr quint32 kCylinderBitReset = 2;
constexpr quint32 kCylinderBitError = 2;
constexpr quint32 kCylinderStateByteErrorId = 2;

inline bool isValidAxisIndex(int axisIndex) {
  return axisIndex >= 0 && axisIndex < kAxisCount;
}
inline bool isValidClCylinderIndex(int index) {
  return index >= 0 && index < kClCylinderCount;
}
inline bool isValidGt2CylinderIndex(int index) {
  return index >= 0 && index < kGt2CylinderCount;
}

inline quint32 axisCtrlMbAddress(int axisIndex) {
  return isValidAxisIndex(axisIndex) ? kAxisCtrlMbBase + static_cast<quint32>(axisIndex) * kAxisCtrlStrideBytes : 0u;
}
inline quint32 axisCtrlBoolMbAddress(int axisIndex, quint32 bitOffset) {
  return axisCtrlMbAddress(axisIndex) + bitOffset / 8u;
}
inline quint32 axisCtrlParamMbAddress(int axisIndex, quint32 byteOffset) {
  return axisCtrlMbAddress(axisIndex) + byteOffset;
}
inline quint32 axisStateMbAddress(int axisIndex) {
  return isValidAxisIndex(axisIndex) ? kAxisStateMbBase + static_cast<quint32>(axisIndex) * kAxisStateStrideBytes : 0u;
}
inline quint32 clCtrlMbAddress(int index) {
  return isValidClCylinderIndex(index) ? kClCtrlMbBase + static_cast<quint32>(index) : 0u;
}
inline quint32 gt2CtrlMbAddress(int index) {
  return isValidGt2CylinderIndex(index) ? kGt2CtrlMbBase + static_cast<quint32>(index) : 0u;
}
inline quint32 clStateMbAddress(int index) {
  return isValidClCylinderIndex(index) ? kClStateMbBase + static_cast<quint32>(index) * kCylinderStateStrideBytes : 0u;
}
inline quint32 gt2StateMbAddress(int index) {
  return isValidGt2CylinderIndex(index) ? kGt2StateMbBase + static_cast<quint32>(index) * kCylinderStateStrideBytes : 0u;
}

inline QString axisName(int axisIndex) {
  static const QStringList names = {QStringLiteral("龙门X轴"), QStringLiteral("龙门Y轴"), QStringLiteral("龙门Z轴"), QStringLiteral("测量X1轴"), QStringLiteral("测量X2轴"), QStringLiteral("测量X3轴"), QStringLiteral("内外径R1轴"), QStringLiteral("内外径R2轴"), QStringLiteral("跳动R3轴"), QStringLiteral("跳动R4轴"), QStringLiteral("轴11")};
  return (axisIndex >= 0 && axisIndex < names.size()) ? names.at(axisIndex) : QStringLiteral("轴%1").arg(axisIndex + 1);
}
inline QString cylinderName(const QString &group, int index) {
  if (group == QStringLiteral("CL")) {
    static const QStringList names = {QStringLiteral("内外径夹持"), QStringLiteral("跳动夹持"), QStringLiteral("长度夹持"), QStringLiteral("抓料气缸")};
    return (index >= 0 && index < names.size()) ? names.at(index) : QStringLiteral("夹持%1").arg(index + 1);
  }
  if (group == QStringLiteral("LM")) return QStringLiteral("抓料气缸");
  return QStringLiteral("GT2_%1").arg(index + 1);
}

} // namespace core::plc_v26
