#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "core/config.hpp"

struct MesSysHeartbeatResult {
  bool transport_ok = false;
  bool business_ok = false;
  int http_code = 0;
  QString error;
  QString code;
  QString message;
  QString server_time;
  QString raw_body;
};

struct MesSysOpCheckUserResult {
  bool transport_ok = false;
  bool business_ok = false;
  int http_code = 0;
  QString error;
  QString code;
  QString message;
  QString success_flag;
  QString raw_body;
};

struct MesSysOpCheckTechStateResult {
  bool transport_ok = false;
  bool business_ok = false;
  int http_code = 0;
  QString error;
  QString code;
  QString message;
  QString success_flag;
  QString op_no;
  QJsonArray tech_state;
  QString raw_body;
};

class MesSysClient {
public:
  explicit MesSysClient(const core::AppConfig &cfg);

  MesSysHeartbeatResult heartbeat(const QString &sysid) const;
  MesSysOpCheckUserResult opCheckUser(const QString &sysid,
                                      const QString &userId) const;
  MesSysOpCheckTechStateResult opCheckTechState(const QString &sysid,
                                                const QString &userId,
                                                const QString &techId,
                                                const QString &deviceId) const;

private:
  core::AppConfig cfg_;
};
