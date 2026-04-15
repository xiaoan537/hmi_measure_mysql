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
bool PlcServiceV26::sendCommandBitmap(quint16 cmdBits, qint16 partType, QString *err) const {
  // 约定：命令触发使用 0->bit 脉冲；category 与命令同一时刻下发，避免PLC侧读到旧类别。
  if (!repo_.writeCategory(partType, err)) {
    return false;
  }
  if (!repo_.writeCommandWord(0, err)) {
    return false;
  }
  return repo_.writeCommandWord(cmdBits, err);
}
bool PlcServiceV26::sendInitialize(qint16 partType, QString *err) const { return sendCommandBitmap(plc_v26::kCmdInitializeBit, partType, err); }
bool PlcServiceV26::sendStartMeasure(qint16 partType, QString *err) const { return sendCommandBitmap(plc_v26::kCmdStartMeasureBit, partType, err); }
bool PlcServiceV26::sendStartCalibration(qint16 partType, QString *err) const { return sendCommandBitmap(plc_v26::kCmdStartCalibrationBit, partType, err); }
bool PlcServiceV26::sendStop(qint16 partType, QString *err) const { return sendCommandBitmap(plc_v26::kCmdStopBit, partType, err); }
bool PlcServiceV26::sendReset(qint16 partType, QString *err) const { return sendCommandBitmap(plc_v26::kCmdResetBit, partType, err); }
bool PlcServiceV26::sendRetestCurrent(qint16 partType, QString *err) const { return sendCommandBitmap(plc_v26::kCmdRetestCurrentBit, partType, err); }
bool PlcServiceV26::confirmIdCheckPassed(QString *err) const { return repo_.writeScanDone(0, err); }
bool PlcServiceV26::writePcAck(QString *err) const { return repo_.writePcAck(plc_v26::kJudgeOk, err); }
bool PlcServiceV26::writeJudgeResult(quint16 result, QString *err) const { return repo_.writeJudgeResult(result, err); }
bool PlcServiceV26::writeTrayPartIdSlot(int slotIndex, const QString &partId, QString *err) const { return repo_.writeTrayPartIdSlot(slotIndex, partId, err); }

bool PlcServiceV26::pollStatusAndCommand(PlcStatusBlockV2 *status, PlcCommandBlockV2 *command, QString *err) const {
  if (status && !readStatus(status, err)) return false; if (command && !readCommand(command, err)) return false; return true;
}

} // namespace core
