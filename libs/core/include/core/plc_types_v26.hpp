#pragma once
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace core {

struct PlcAxisStateV26 {
  int axis_index = -1;
  QString axis_name;
  bool enabled = false;
  bool homed = false;
  bool error = false;
  bool busy = false;
  bool done = false;
  quint16 error_id = 0;
  double act_position = 0.0;
  double act_velocity = 0.0;
};

struct PlcCylinderStateV26 {
  QString group;
  int index = -1;
  QString name;
  bool p = false;
  bool n = false;
  bool error = false;
  quint16 error_id = 0;
};

struct PlcMachineStateDecodedV26 {
  bool idle = false;
  bool auto_mode = false;
  bool manual = false;
  bool paused = false;
  bool fault = false;
  bool estop = false;
  QString text;
};

} // namespace core
