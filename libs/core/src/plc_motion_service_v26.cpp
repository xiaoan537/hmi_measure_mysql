#include "core/plc_motion_service_v26.hpp"

#include "core/plc_addresses_v26.hpp"

namespace core {

bool PlcMotionServiceV26::readAxisState(int axisIndex, PlcAxisStateV26 *out,
                                        QString *err) const {
  return repo_.readAxisState(axisIndex, out, err);
}

bool PlcMotionServiceV26::axisSetEnable(int axisIndex, bool on,
                                        QString *err) const {
  return repo_.writeAxisBoolByte(axisIndex, plc_v26::kAxisCtrlByteEnable,
                                 on ? 1 : 0, err);
}

bool PlcMotionServiceV26::axisPulseAction(int axisIndex, const QString &action,
                                          QString *err) const {
  quint32 off = plc_v26::kAxisCtrlByteReset;
  if (action == QStringLiteral("HOME")) {
    off = plc_v26::kAxisCtrlByteHome;
  } else if (action == QStringLiteral("ESTOP")) {
    off = plc_v26::kAxisCtrlByteEStop;
  } else if (action == QStringLiteral("STOP")) {
    off = plc_v26::kAxisCtrlByteStop;
  } else if (action == QStringLiteral("RESET")) {
    off = plc_v26::kAxisCtrlByteReset;
  } else {
    return false;
  }
  return repo_.pulseAxisBoolByte(axisIndex, off, err);
}

bool PlcMotionServiceV26::axisJog(int axisIndex, bool forward, bool active,
                                  QString *err) const {
  return repo_.writeAxisBoolByte(
      axisIndex,
      forward ? plc_v26::kAxisCtrlByteJogForward
              : plc_v26::kAxisCtrlByteJogBackward,
      active ? 1 : 0, err);
}

bool PlcMotionServiceV26::axisMove(int axisIndex, bool relative, double acc,
                                   double dec, double pos, double vel,
                                   QString *err) const {
  if (!repo_.writeAxisMotionParams(axisIndex, acc, dec, pos, vel, err)) {
    return false;
  }
  return repo_.pulseAxisBoolByte(
      axisIndex,
      relative ? plc_v26::kAxisCtrlByteMoveRel : plc_v26::kAxisCtrlByteMoveAbs,
      err);
}

bool PlcMotionServiceV26::readCylinderState(const QString &group, int index,
                                            PlcCylinderStateV26 *out,
                                            QString *err) const {
  return repo_.readCylinderState(group, index, out, err);
}

bool PlcMotionServiceV26::cylinderAction(const QString &group, int index,
                                         const QString &action,
                                         QString *err) const {
  int byte = 2;
  if (action == QStringLiteral("P")) {
    byte = 0;
  } else if (action == QStringLiteral("N")) {
    byte = 1;
  }
  return repo_.pulseCylinder(group, index, byte, err);
}

} // namespace core
