#pragma once

#include <QDateTime>
#include <QDialog>
#include <QString>

#include "core/config.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QTimer;

class LoginDialog : public QDialog {
  Q_OBJECT
public:
  LoginDialog(const core::AppConfig &cfg, const QString &iniPath,
              QWidget *parent = nullptr);

  QString sysid() const;
  QString userId() const;
  QString techId() const;
  QString deviceId() const;
  QString opNo() const;
  bool mesVerified() const;

private slots:
  void onVerifyMes();
  void onLogin();
  void onRefreshMesTime();
  void onAutoRefreshMesTime();
  void onUiTick();

private:
  bool verifyLocalPassword(QString *err) const;
  QString configuredPasswordForUser(const QString &userId) const;
  void loadLocalValues();
  void saveLocalValues();
  void setStatus(const QString &text, bool ok);
  QString summarizeTechState() const;
  void updateLoginButtonState();
  void updateTimeInfoUi();
  void appendResultLine(const QString &line);
  bool refreshMesHeartbeat(bool interactive, QString *err = nullptr);
  QDateTime parseMesDateTime(const QString &text) const;
  QDateTime currentMesEstimate() const;
  qint64 currentTimeDiffSeconds() const;
  bool trySyncSystemTime(const QDateTime &target, QString *err) const;
  bool ensureTimeAligned(bool interactive, QString *err = nullptr);

  core::AppConfig cfg_;
  QString iniPath_;

  QLineEdit *editSysid_ = nullptr;
  QLineEdit *editUserId_ = nullptr;
  QLineEdit *editPassword_ = nullptr;
  QLineEdit *editTechId_ = nullptr;
  QLineEdit *editDeviceId_ = nullptr;
  QLabel *labelStatus_ = nullptr;
  QTextEdit *textResult_ = nullptr;
  QPushButton *btnVerifyMes_ = nullptr;
  QPushButton *btnRefreshMesTime_ = nullptr;
  QPushButton *btnLogin_ = nullptr;
  QLabel *labelHeartbeatState_ = nullptr;
  QLabel *labelMesTime_ = nullptr;
  QLabel *labelLocalTime_ = nullptr;
  QLabel *labelTimeDiff_ = nullptr;
  QLabel *labelSyncState_ = nullptr;
  QTimer *uiTimer_ = nullptr;
  QTimer *heartbeatTimer_ = nullptr;

  bool requireMesVerify_ = true;
  bool mesVerified_ = false;
  QString verifiedOpNo_;
  QString verifiedTechSummary_;

  bool heartbeatOk_ = false;
  QString heartbeatMessage_;
  QDateTime mesTimeSample_;
  QDateTime mesTimeSampleLocal_;
  QString mesTimeRaw_;
  QString lastTimeSyncMessage_;
  bool timeAligned_ = false;
};
