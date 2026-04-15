#pragma once
#include <QString>
#include <QVector>
#include "core/plc_repository_v26.hpp"
#include "core/plc_polling_v2.hpp"

namespace core {

class PlcServiceV26 {
public:
  explicit PlcServiceV26(IPlcRegisterClientV2 *client = nullptr) : repo_(client) {}
  void setClient(IPlcRegisterClientV2 *client) { repo_.setClient(client); }

  bool readStatus(PlcStatusBlockV2 *out, QString *err = nullptr) const;
  bool readCommand(PlcCommandBlockV2 *out, QString *err = nullptr) const;
  bool readTrayCoding(PlcTrayPartIdBlockV2 *out, QString *err = nullptr) const;
  bool readMailbox(QChar preferredPartType, PlcMailboxSnapshot *out, QString *err = nullptr) const;

  bool setControlMode(qint16 mode, QString *err = nullptr) const;
  bool setPartType(qint16 partType, QString *err = nullptr) const;
  bool sendCommandBitmap(quint16 cmdBits, qint16 partType, QString *err = nullptr) const;
  bool confirmIdCheckPassed(QString *err = nullptr) const;
  bool writePcAck(QString *err = nullptr) const;
  bool writeJudgeResult(quint16 result, QString *err = nullptr) const;
  bool writeTrayPartIdSlot(int slotIndex, const QString &partId, QString *err = nullptr) const;

  bool readAxisState(int axisIndex, PlcAxisStateV26 *out, QString *err = nullptr) const;
  bool axisSetEnable(int axisIndex, bool on, QString *err = nullptr) const;
  bool axisPulseAction(int axisIndex, const QString &action, QString *err = nullptr) const;
  bool axisJog(int axisIndex, bool forward, bool active, QString *err = nullptr) const;
  bool axisMove(int axisIndex, bool relative, double acc, double dec, double pos, double vel, QString *err = nullptr) const;

  bool readCylinderState(const QString &group, int index, PlcCylinderStateV26 *out, QString *err = nullptr) const;
  bool cylinderAction(const QString &group, int index, const QString &action, QString *err = nullptr) const;

  bool pollStatusAndCommand(PlcStatusBlockV2 *status, PlcCommandBlockV2 *command, QString *err = nullptr) const;

private:
  PlcRepositoryV26 repo_;
};

} // namespace core
