#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
#include "core/plc_qt_modbus_v2.hpp"
#include "core/plc_types_v26.hpp"
#include "core/measurement_pipeline.hpp"

namespace core {

class PlcRepositoryV26 {
public:
  explicit PlcRepositoryV26(IPlcRegisterClientV2 *client = nullptr) : client_(client) {}
  void setClient(IPlcRegisterClientV2 *client) { client_ = client; }

  bool readStatusRaw(QVector<quint16> *out, QString *err = nullptr) const;
  bool readCommandRaw(QVector<quint16> *out, QString *err = nullptr) const;
  bool readTrayCodingRaw(QVector<quint16> *out, QString *err = nullptr) const;
  bool readMailboxRaw(QVector<quint16> *out, QString *err = nullptr) const;

  bool writeMode(qint16 mode, QString *err = nullptr) const;
  bool writeCategory(qint16 category, QString *err = nullptr) const;
  bool writeCommandWord(quint16 cmdWord, QString *err = nullptr) const;
  bool writeScanDone(quint16 value, QString *err = nullptr) const;
  bool writePcAck(quint16 value, QString *err = nullptr) const;
  bool writeJudgeResult(quint16 value, QString *err = nullptr) const;
  bool writeTrayPartIdSlot(int slotIndex, const QString &partId, QString *err = nullptr) const;

  bool readMbBytes(quint32 mbByteAddress, quint16 byteCount, QByteArray *out, QString *err = nullptr) const;
  bool writeMbBytes(quint32 mbByteAddress, const QByteArray &bytes, QString *err = nullptr) const;
  bool readHolding(quint32 reg, quint16 count, QVector<quint16> *out, QString *err = nullptr) const;
  bool writeHolding(quint32 reg, const QVector<quint16> &values, QString *err = nullptr) const;

  bool readAxisState(int axisIndex, PlcAxisStateV26 *out, QString *err = nullptr) const;
  bool writeAxisBoolByte(int axisIndex, quint32 byteOffset, quint8 value, QString *err = nullptr) const;
  bool pulseAxisBoolByte(int axisIndex, quint32 byteOffset, QString *err = nullptr) const;
  bool writeAxisMotionParams(int axisIndex, double acc, double dec, double pos, double vel, QString *err = nullptr) const;

  bool readCylinderState(const QString &group, int index, PlcCylinderStateV26 *out, QString *err = nullptr) const;
  bool pulseCylinder(const QString &group, int index, int whichByte, QString *err = nullptr) const;

private:
  IPlcRegisterClientV2 *client_ = nullptr;
};

} // namespace core
