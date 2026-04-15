#pragma once

#include <QString>

#include "core/plc_repository_v26.hpp"

namespace core {

class PlcMotionServiceV26 {
public:
  explicit PlcMotionServiceV26(IPlcRegisterClientV2 *client = nullptr)
      : repo_(client) {}
  void setClient(IPlcRegisterClientV2 *client) { repo_.setClient(client); }

  bool readAxisState(int axisIndex, PlcAxisStateV26 *out,
                     QString *err = nullptr) const;
  bool axisSetEnable(int axisIndex, bool on, QString *err = nullptr) const;
  bool axisPulseAction(int axisIndex, const QString &action,
                       QString *err = nullptr) const;
  bool axisJog(int axisIndex, bool forward, bool active,
               QString *err = nullptr) const;
  bool axisMove(int axisIndex, bool relative, double acc, double dec, double pos,
                double vel, QString *err = nullptr) const;

  bool readCylinderState(const QString &group, int index,
                         PlcCylinderStateV26 *out,
                         QString *err = nullptr) const;
  bool cylinderAction(const QString &group, int index, const QString &action,
                      QString *err = nullptr) const;

private:
  PlcRepositoryV26 repo_;
};

} // namespace core
