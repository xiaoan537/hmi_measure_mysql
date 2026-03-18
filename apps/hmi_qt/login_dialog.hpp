#pragma once

#include <QDialog>
#include <QString>

#include "core/config.hpp"

class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

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

private:
  bool verifyLocalPassword(QString *err) const;
  QString configuredPasswordForUser(const QString &userId) const;
  void loadLocalValues();
  void saveLocalValues();
  void setStatus(const QString &text, bool ok);
  QString summarizeTechState() const;

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
  QPushButton *btnLogin_ = nullptr;

  bool requireMesVerify_ = true;
  bool mesVerified_ = false;
  QString verifiedOpNo_;
  QString verifiedTechSummary_;
};
