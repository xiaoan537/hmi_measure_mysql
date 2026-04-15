#include "core/plc_service_v26.hpp"
#include "core/plc_addresses_v26.hpp"

namespace core {

bool PlcServiceV26::readStatus(PlcStatusBlockV2 *out, QString *err) const {
  QVector<quint16> regs; if (!repo_.readStatusRaw(&regs, err)) return false; return buildPlcStatusBlockV25(regs, out, err);
}
bool PlcServiceV26::readCommand(PlcCommandBlockV2 *out, QString *err) const {
  QVector<quint16> regs; if (!repo_.readCommandRaw(&regs, err)) return false; return buildPlcCommandBlockV25(regs, out, err);
}
bool PlcServiceV26::readTrayCoding(PlcTrayPartIdBlockV2 *out, QString *err) const {
  QVector<quint16> regs; if (!repo_.readTrayCodingRaw(&regs, err)) return false; return buildPlcTrayAllCodingBlockV25(regs, out, err);
}
bool PlcServiceV26::readMailbox(QChar preferredPartType, PlcMailboxSnapshot *out, QString *err) const {
  QVector<quint16> regs; if (!repo_.readMailboxRaw(&regs, err)) return false; return buildSecondStageMailboxSnapshotV25(regs, preferredPartType, out, err);
}

bool PlcServiceV26::setControlMode(qint16 mode, QString *err) const { return repo_.writeMode(mode, err); }
bool PlcServiceV26::setPartType(qint16 partType, QString *err) const { return repo_.writeCategory(partType, err); }
bool PlcServiceV26::sendCommandBitmap(quint16 cmdBits, qint16 /*partType*/, QString *err) const {
  // v2.6：工件类型(iCategory_Mode @ MB7300) 与命令位图(wCmd_Code @ MB7302) 是独立写入。
  // 发命令时这里只写命令位图，不联动重写 category，也不先写 0，避免覆盖现场已选类型。
  return repo_.writeCommandWord(cmdBits, err);
}
bool PlcServiceV26::confirmIdCheckPassed(QString *err) const { return repo_.writeScanDone(0, err); }
bool PlcServiceV26::writePcAck(QString *err) const { return repo_.writePcAck(plc_v26::kJudgeOk, err); }
bool PlcServiceV26::writeJudgeResult(quint16 result, QString *err) const { return repo_.writeJudgeResult(result, err); }
bool PlcServiceV26::writeTrayPartIdSlot(int slotIndex, const QString &partId, QString *err) const { return repo_.writeTrayPartIdSlot(slotIndex, partId, err); }

bool PlcServiceV26::readAxisState(int axisIndex, PlcAxisStateV26 *out, QString *err) const { return repo_.readAxisState(axisIndex, out, err); }
bool PlcServiceV26::axisSetEnable(int axisIndex, bool on, QString *err) const { return repo_.writeAxisBoolByte(axisIndex, plc_v26::kAxisCtrlByteEnable, on ? 1 : 0, err); }
bool PlcServiceV26::axisPulseAction(int axisIndex, const QString &action, QString *err) const {
  quint32 off = plc_v26::kAxisCtrlByteReset;
  if (action == QStringLiteral("HOME")) off = plc_v26::kAxisCtrlByteHome;
  else if (action == QStringLiteral("ESTOP")) off = plc_v26::kAxisCtrlByteEStop;
  else if (action == QStringLiteral("STOP")) off = plc_v26::kAxisCtrlByteStop;
  else if (action == QStringLiteral("RESET")) off = plc_v26::kAxisCtrlByteReset;
  else return false;
  return repo_.pulseAxisBoolByte(axisIndex, off, err);
}
bool PlcServiceV26::axisJog(int axisIndex, bool forward, bool active, QString *err) const { return repo_.writeAxisBoolByte(axisIndex, forward ? plc_v26::kAxisCtrlByteJogForward : plc_v26::kAxisCtrlByteJogBackward, active ? 1 : 0, err); }
bool PlcServiceV26::axisMove(int axisIndex, bool relative, double acc, double dec, double pos, double vel, QString *err) const {
  if (!repo_.writeAxisMotionParams(axisIndex, acc, dec, pos, vel, err)) return false;
  return repo_.pulseAxisBoolByte(axisIndex, relative ? plc_v26::kAxisCtrlByteMoveRel : plc_v26::kAxisCtrlByteMoveAbs, err);
}

bool PlcServiceV26::readCylinderState(const QString &group, int index, PlcCylinderStateV26 *out, QString *err) const { return repo_.readCylinderState(group, index, out, err); }
bool PlcServiceV26::cylinderAction(const QString &group, int index, const QString &action, QString *err) const {
  int byte = 2; if (action == QStringLiteral("P")) byte = 0; else if (action == QStringLiteral("N")) byte = 1; return repo_.pulseCylinder(group, index, byte, err);
}

bool PlcServiceV26::pollStatusAndCommand(PlcStatusBlockV2 *status, PlcCommandBlockV2 *command, QString *err) const {
  if (status && !readStatus(status, err)) return false; if (command && !readCommand(command, err)) return false; return true;
}

} // namespace core
