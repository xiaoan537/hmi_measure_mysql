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
constexpr quint32 kRegTrayAllCoding = 3700;     // MB7400

constexpr quint32 kRegMailboxStart = 1197;      // MB2394
constexpr quint32 kRegKeyenceStart = 1280;      // MB2560
constexpr quint32 kRegChuantecStart = 1296;     // MB2592
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
constexpr int kMailboxChuantecRegs = 1152; // 8 x 72 x REAL / 2 regs
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

constexpr float kInvalidRawValue = 2147.48364f;

constexpr std::array<quint32, 10> kAxisCtrlBoolMbBase = {162u,370u,578u,786u,994u,1202u,1410u,1618u,1826u,2034u};
constexpr std::array<quint32, 10> kAxisCtrlParamMbBase = {176u,384u,592u,800u,1008u,1216u,1424u,1632u,1840u,2048u};
constexpr std::array<quint32, 10> kAxisStateMbBase = {2080u,2104u,2128u,2152u,2176u,2200u,2224u,2248u,2272u,2296u};

constexpr std::array<quint32, 3> kClCtrlMbBase = {2324u,2327u,2330u};
constexpr std::array<quint32, 4> kGt2CtrlMbBase = {2334u,2337u,2340u,2343u};
constexpr quint32 kLmCtrlMbBase = 2320u;

constexpr quint32 kLmStateMbBase = 2346u;
constexpr std::array<quint32, 3> kClStateMbBase = {2352u,2358u,2364u};
constexpr std::array<quint32, 4> kGt2StateMbBase = {2370u,2376u,2382u,2388u};

constexpr quint32 kAxisCtrlByteEnable = 0;
constexpr quint32 kAxisCtrlByteReset = 1;
constexpr quint32 kAxisCtrlByteHome = 2;
constexpr quint32 kAxisCtrlByteEStop = 3;
constexpr quint32 kAxisCtrlByteStop = 4;
constexpr quint32 kAxisCtrlByteMoveAbs = 5;
constexpr quint32 kAxisCtrlByteMoveRel = 6;
constexpr quint32 kAxisCtrlByteJogForward = 7;
constexpr quint32 kAxisCtrlByteJogBackward = 8;

constexpr quint32 kAxisCtrlParamByteAcc = 0;
constexpr quint32 kAxisCtrlParamByteDec = 8;
constexpr quint32 kAxisCtrlParamBytePosition = 16;
constexpr quint32 kAxisCtrlParamByteVelocity = 24;

constexpr quint32 kAxisStateBytes = 24;
constexpr quint32 kAxisStateRegCount = 12;
constexpr quint32 kAxisStateByteEnabled = 0;
constexpr quint32 kAxisStateByteHomed = 1;
constexpr quint32 kAxisStateByteError = 2;
constexpr quint32 kAxisStateByteBusy = 3;
constexpr quint32 kAxisStateByteDone = 4;
constexpr quint32 kAxisStateByteErrorId = 6;
constexpr quint32 kAxisStateByteActPosition = 8;
constexpr quint32 kAxisStateByteActVelocity = 16;

constexpr quint32 kCylinderCtrlBytes = 3;
constexpr quint32 kCylinderStateBytes = 6;

inline quint32 axisCtrlBoolMbAddress(int axisIndex, quint32 byteOffset) {
  return (axisIndex >= 0 && axisIndex < static_cast<int>(kAxisCtrlBoolMbBase.size())) ? kAxisCtrlBoolMbBase[static_cast<size_t>(axisIndex)] + byteOffset : 0u;
}
inline quint32 axisCtrlParamMbAddress(int axisIndex, quint32 byteOffset) {
  return (axisIndex >= 0 && axisIndex < static_cast<int>(kAxisCtrlParamMbBase.size())) ? kAxisCtrlParamMbBase[static_cast<size_t>(axisIndex)] + byteOffset : 0u;
}
inline quint32 axisStateMbAddress(int axisIndex) {
  return (axisIndex >= 0 && axisIndex < static_cast<int>(kAxisStateMbBase.size())) ? kAxisStateMbBase[static_cast<size_t>(axisIndex)] : 0u;
}
inline quint32 clCtrlMbAddress(int index) {
  return (index >= 0 && index < static_cast<int>(kClCtrlMbBase.size())) ? kClCtrlMbBase[static_cast<size_t>(index)] : 0u;
}
inline quint32 gt2CtrlMbAddress(int index) {
  return (index >= 0 && index < static_cast<int>(kGt2CtrlMbBase.size())) ? kGt2CtrlMbBase[static_cast<size_t>(index)] : 0u;
}
inline quint32 clStateMbAddress(int index) {
  return (index >= 0 && index < static_cast<int>(kClStateMbBase.size())) ? kClStateMbBase[static_cast<size_t>(index)] : 0u;
}
inline quint32 gt2StateMbAddress(int index) {
  return (index >= 0 && index < static_cast<int>(kGt2StateMbBase.size())) ? kGt2StateMbBase[static_cast<size_t>(index)] : 0u;
}

inline QString axisName(int axisIndex) {
  static const QStringList names = {QStringLiteral("龙门X轴"), QStringLiteral("龙门Y轴"), QStringLiteral("龙门Z轴"), QStringLiteral("测量X1轴"), QStringLiteral("测量X2轴"), QStringLiteral("测量X3轴"), QStringLiteral("内外径R1轴"), QStringLiteral("内外径R2轴"), QStringLiteral("跳动R3轴"), QStringLiteral("跳动R4轴")};
  return (axisIndex >= 0 && axisIndex < names.size()) ? names.at(axisIndex) : QStringLiteral("轴%1").arg(axisIndex + 1);
}
inline QString cylinderName(const QString &group, int index) {
  if (group == QStringLiteral("LM")) return QStringLiteral("抓料气缸");
  if (group == QStringLiteral("CL")) {
    static const QStringList names = {QStringLiteral("内外径夹持"), QStringLiteral("跳动夹持"), QStringLiteral("长度夹持")};
    return (index >= 0 && index < names.size()) ? names.at(index) : QStringLiteral("夹持%1").arg(index + 1);
  }
  return QStringLiteral("GT2_%1").arg(index + 1);
}

} // namespace core::plc_v26
