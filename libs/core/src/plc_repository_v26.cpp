#include "core/plc_repository_v26.hpp"
#include "core/plc_addresses_v26.hpp"
#include "core/plc_codec_v26.hpp"

#include <QThread>
#include <cstring>

namespace core {
namespace {
void setErr(QString *err, const QString &msg) { if (err) *err = msg; }
quint32 mbToReg(quint32 mb) { return mb / 2u; }
}

bool PlcRepositoryV26::readHolding(quint32 reg, quint16 count, QVector<quint16> *out, QString *err) const {
  if (!client_) { setErr(err, QStringLiteral("PLC client 未就绪")); return false; }
  return client_->readHoldingRegisters(reg, count, out, err);
}

bool PlcRepositoryV26::writeHolding(quint32 reg, const QVector<quint16> &values, QString *err) const {
  if (!client_) { setErr(err, QStringLiteral("PLC client 未就绪")); return false; }
  return client_->writeHoldingRegisters(reg, values, err);
}

bool PlcRepositoryV26::readStatusRaw(QVector<quint16> *out, QString *err) const { return readHolding(plc_v26::kRegStatusStart, plc_v26::kStatusRegs, out, err); }
bool PlcRepositoryV26::readCommandRaw(QVector<quint16> *out, QString *err) const { return readHolding(plc_v26::kRegCommandStart, plc_v26::kCommandRegs, out, err); }
bool PlcRepositoryV26::readTrayCodingRaw(QVector<quint16> *out, QString *err) const { return readHolding(plc_v26::kRegTrayAllCoding, static_cast<quint16>(plc_v26::kTrayAllCodingRegs), out, err); }
bool PlcRepositoryV26::readMailboxRaw(QVector<quint16> *out, QString *err) const {
  return readMailboxRaw(plc_v26::kMailboxPointCount72, out, err);
}
bool PlcRepositoryV26::readMailboxRaw(int pointCount, QVector<quint16> *out, QString *err) const {
  if (!out) { setErr(err, QStringLiteral("readMailboxRaw.out 不能为空")); return false; }
  if (!plc_v26::isValidMailboxPointCount(pointCount)) {
    setErr(err, QStringLiteral("采样点数只支持 72 或 180，当前=%1").arg(pointCount));
    return false;
  }
  QVector<quint16> fixedRegs;
  if (!readHolding(plc_v26::kRegMailboxStart, static_cast<quint16>(plc_v26::kMailboxFixedRegs), &fixedRegs, err)) return false;
  QVector<quint16> curveRegs;
  if (!readHolding(plc_v26::chuantecRegStartForPointCount(pointCount),
                   static_cast<quint16>(plc_v26::chuantecRegsForPointCount(pointCount)),
                   &curveRegs, err)) return false;
  out->clear();
  out->reserve(fixedRegs.size() + curveRegs.size());
  for (quint16 reg : fixedRegs) out->push_back(reg);
  for (quint16 reg : curveRegs) out->push_back(reg);
  return true;
}

bool PlcRepositoryV26::writeMode(qint16 mode, QString *err) const { return writeHolding(plc_v26::kRegMode, QVector<quint16>{static_cast<quint16>(mode)}, err); }
bool PlcRepositoryV26::writeCategory(qint16 category, QString *err) const { return writeHolding(plc_v26::kRegCommandStart + plc_v26::kCommandOffCategoryMode, QVector<quint16>{static_cast<quint16>(category)}, err); }
bool PlcRepositoryV26::writeCommandWord(quint16 cmdWord, QString *err) const { return writeHolding(plc_v26::kRegCommandStart + plc_v26::kCommandOffCmdCode, QVector<quint16>{cmdWord}, err); }
bool PlcRepositoryV26::writeSamplePointCount(int pointCount, QString *err) const {
  if (!plc_v26::isValidMailboxPointCount(pointCount)) {
    setErr(err, QStringLiteral("采样点数只支持 72 或 180，当前=%1").arg(pointCount));
    return false;
  }
  return writeHolding(plc_v26::kRegSamplePointCount,
                      QVector<quint16>{static_cast<quint16>(pointCount)}, err);
}
bool PlcRepositoryV26::writeScanDone(quint16 value, QString *err) const { return writeHolding(plc_v26::kRegStatusStart + plc_v26::kStatusOffScanDone, QVector<quint16>{value}, err); }
bool PlcRepositoryV26::writePcAck(quint16 value, QString *err) const { return writeHolding(plc_v26::kRegPcAck, QVector<quint16>{value}, err); }
bool PlcRepositoryV26::writeJudgeResult(quint16 value, QString *err) const { return writeHolding(plc_v26::kRegJudgeResult, QVector<quint16>{value}, err); }

bool PlcRepositoryV26::readMbBytes(quint32 mbByteAddress, quint16 byteCount, QByteArray *out, QString *err) const {
  if (!out) { setErr(err, QStringLiteral("readMbBytes.out 不能为空")); return false; }
  const quint32 startReg = mbByteAddress / 2u; const quint32 byteOffset = mbByteAddress % 2u; const quint32 regCount = (byteOffset + byteCount + 1u) / 2u;
  QVector<quint16> regs; if (!readHolding(startReg, static_cast<quint16>(regCount), &regs, err)) return false;
  *out = plc_codec_v26::regsToMbBytes(regs).mid(static_cast<int>(byteOffset), byteCount); return true;
}

bool PlcRepositoryV26::writeMbBytes(quint32 mbByteAddress, const QByteArray &bytes, QString *err) const {
  const quint32 startReg = mbByteAddress / 2u; const quint32 byteOffset = mbByteAddress % 2u; const quint32 regCount = (byteOffset + static_cast<quint32>(bytes.size()) + 1u) / 2u;
  QVector<quint16> regs; if (!readHolding(startReg, static_cast<quint16>(regCount), &regs, err)) return false;
  QByteArray raw = plc_codec_v26::regsToMbBytes(regs);
  for (int i = 0; i < bytes.size(); ++i) if (static_cast<int>(byteOffset) + i < raw.size()) raw[static_cast<int>(byteOffset) + i] = bytes.at(i);
  return writeHolding(startReg, plc_codec_v26::mbBytesToRegs(raw), err);
}

bool PlcRepositoryV26::writeMbBit(quint32 mbByteAddress, quint32 bitOffset,
                                  bool value, QString *err) const {
  const quint32 byteAddress = mbByteAddress + bitOffset / 8u;
  const quint8 mask = static_cast<quint8>(1u << (bitOffset % 8u));

  QByteArray bytes;
  if (!readMbBytes(byteAddress, 1, &bytes, err)) return false;
  if (bytes.size() < 1) {
    setErr(err, QStringLiteral("读取 MB 位所在字节失败"));
    return false;
  }

  quint8 byte = static_cast<quint8>(bytes.at(0));
  if (value) {
    byte = static_cast<quint8>(byte | mask);
  } else {
    byte = static_cast<quint8>(byte & ~mask);
  }
  return writeMbBytes(byteAddress, QByteArray(1, static_cast<char>(byte)), err);
}

bool PlcRepositoryV26::pulseMbBit(quint32 mbByteAddress, quint32 bitOffset,
                                  QString *err) const {
  if (!writeMbBit(mbByteAddress, bitOffset, true, err)) return false;
  QThread::msleep(50);
  return writeMbBit(mbByteAddress, bitOffset, false, err);
}

bool PlcRepositoryV26::writeTrayPartIdSlot(int slotIndex, const QString &partId, QString *err) const {
  if (slotIndex < 0 || slotIndex >= plc_v26::kLogicalSlotCount) { setErr(err, QStringLiteral("slotIndex 越界")); return false; }
  const QByteArray bytes = plc_codec_v26::asciiToMbBytes(partId, plc_v26::kTraySlotBytes);
  return writeMbBytes(plc_v26::kRegTrayAllCoding * 2u + static_cast<quint32>(slotIndex * plc_v26::kTraySlotBytes), bytes, err);
}

bool PlcRepositoryV26::readAxisState(int axisIndex, PlcAxisStateV26 *out, QString *err) const {
  if (!out) { setErr(err, QStringLiteral("readAxisState.out 不能为空")); return false; }
  if (!plc_v26::isValidAxisIndex(axisIndex)) { setErr(err, QStringLiteral("axisIndex 越界")); return false; }
  QVector<quint16> regs; if (!readHolding(mbToReg(plc_v26::axisStateMbAddress(axisIndex)), static_cast<quint16>(plc_v26::kAxisStateRegCount), &regs, err)) return false;
  QByteArray bytes = plc_codec_v26::regsToMbBytes(regs);
  auto u8=[&](int off){ return off < bytes.size() ? static_cast<unsigned char>(bytes.at(off)) : 0; };
  auto u16le=[&](int off){ return static_cast<quint16>(u8(off) | (u8(off+1)<<8)); };
  auto bit=[&](quint32 bitOffset){ return (u8(static_cast<int>(bitOffset / 8u)) & (1u << (bitOffset % 8u))) != 0; };
  PlcAxisStateV26 s; s.axis_index = axisIndex; s.axis_name = plc_v26::axisName(axisIndex); s.enabled = bit(plc_v26::kAxisStateBitEnabled); s.homed = bit(plc_v26::kAxisStateBitHomed); s.error = bit(plc_v26::kAxisStateBitError); s.busy = bit(plc_v26::kAxisStateBitBusy); s.done = bit(plc_v26::kAxisStateBitDone); s.error_id = u16le(plc_v26::kAxisStateByteErrorId); plc_codec_v26::readFloat64WordSwapped(regs, static_cast<int>(plc_v26::kAxisStateByteActPosition/2u), &s.act_position, nullptr); plc_codec_v26::readFloat64WordSwapped(regs, static_cast<int>(plc_v26::kAxisStateByteActVelocity/2u), &s.act_velocity, nullptr); *out = s; return true;
}

bool PlcRepositoryV26::writeAxisBoolByte(int axisIndex, quint32 byteOffset, quint8 value, QString *err) const {
  if (!plc_v26::isValidAxisIndex(axisIndex)) { setErr(err, QStringLiteral("axisIndex 越界")); return false; }
  return writeMbBit(plc_v26::axisCtrlMbAddress(axisIndex), byteOffset, value != 0, err);
}

bool PlcRepositoryV26::pulseAxisBoolByte(int axisIndex, quint32 byteOffset, QString *err) const {
  if (!plc_v26::isValidAxisIndex(axisIndex)) { setErr(err, QStringLiteral("axisIndex 越界")); return false; }
  return pulseMbBit(plc_v26::axisCtrlMbAddress(axisIndex), byteOffset, err);
}

bool PlcRepositoryV26::writeAxisMotionParams(int axisIndex, double acc, double dec, double pos, double vel, QString *err) const {
  if (!plc_v26::isValidAxisIndex(axisIndex)) { setErr(err, QStringLiteral("axisIndex 越界")); return false; }
  QByteArray bytes; auto add=[&](double v){ quint64 bits=0; std::memcpy(&bits,&v,sizeof(bits)); for(int i=0;i<8;++i) bytes.append(static_cast<char>((bits>>(8*i))&0xFFu)); }; add(acc); add(dec); add(pos); add(vel); return writeMbBytes(plc_v26::axisCtrlParamMbAddress(axisIndex, plc_v26::kAxisCtrlParamByteAcc), bytes, err);
}

bool PlcRepositoryV26::readCylinderState(const QString &group, int index, PlcCylinderStateV26 *out, QString *err) const {
  if (!out) { setErr(err, QStringLiteral("readCylinderState.out 不能为空")); return false; }
  QString effectiveGroup = group;
  int effectiveIndex = index;
  if (effectiveGroup == QStringLiteral("LM")) { effectiveGroup = QStringLiteral("CL"); effectiveIndex = 3; }
  quint32 mb = 0; if (effectiveGroup == QStringLiteral("CL")) mb = plc_v26::clStateMbAddress(effectiveIndex); else mb = plc_v26::gt2StateMbAddress(effectiveIndex);
  if (mb == 0u) { setErr(err, QStringLiteral("气缸索引越界")); return false; }
  QByteArray bytes; if (!readMbBytes(mb, static_cast<quint16>(plc_v26::kCylinderStateBytes), &bytes, err)) return false; auto u8=[&](int off){ return off < bytes.size() ? static_cast<unsigned char>(bytes.at(off)) : 0; };
  auto bit=[&](quint32 bitOffset){ return (u8(static_cast<int>(bitOffset / 8u)) & (1u << (bitOffset % 8u))) != 0; };
  PlcCylinderStateV26 s; s.group = effectiveGroup; s.index = effectiveIndex; s.name = plc_v26::cylinderName(effectiveGroup, effectiveIndex); s.p = bit(plc_v26::kCylinderBitP); s.n = bit(plc_v26::kCylinderBitN); s.error = bit(plc_v26::kCylinderBitError); s.error_id = static_cast<quint16>(u8(plc_v26::kCylinderStateByteErrorId) | (u8(plc_v26::kCylinderStateByteErrorId + 1) << 8)); *out = s; return true;
}

bool PlcRepositoryV26::pulseCylinder(const QString &group, int index, int whichByte, QString *err) const {
  QString effectiveGroup = group;
  int effectiveIndex = index;
  if (effectiveGroup == QStringLiteral("LM")) { effectiveGroup = QStringLiteral("CL"); effectiveIndex = 3; }
  quint32 mb = 0;
  if (effectiveGroup == QStringLiteral("CL")) {
    mb = plc_v26::clCtrlMbAddress(effectiveIndex);
  } else {
    mb = plc_v26::gt2CtrlMbAddress(effectiveIndex);
  }
  if (mb == 0u) { setErr(err, QStringLiteral("气缸索引越界")); return false; }
  if (whichByte < 0 || whichByte > static_cast<int>(plc_v26::kCylinderBitReset)) {
    setErr(err, QStringLiteral("气缸动作位越界"));
    return false;
  }
  return pulseMbBit(mb, static_cast<quint32>(whichByte), err);
}

} // namespace core
