#include "login_dialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTextEdit>
#include <QVBoxLayout>

#include "mes_sys_client.hpp"

LoginDialog::LoginDialog(const core::AppConfig &cfg, const QString &iniPath,
                         QWidget *parent)
    : QDialog(parent), cfg_(cfg), iniPath_(iniPath) {
  setWindowTitle(QStringLiteral("登录 / MES验证"));
  resize(560, 360);
  setModal(true);

  auto *mainLayout = new QVBoxLayout(this);
  auto *tip = new QLabel(
      QStringLiteral("请输入本地登录信息，并点击“MES验证”校验 UserID / TechID / DeviceID。\n"
                     "密码仅在本地校验，不发送到 MES。"),
      this);
  tip->setWordWrap(true);
  mainLayout->addWidget(tip);

  auto *form = new QFormLayout();
  editSysid_ = new QLineEdit(this);
  editUserId_ = new QLineEdit(this);
  editPassword_ = new QLineEdit(this);
  editPassword_->setEchoMode(QLineEdit::Password);
  editTechId_ = new QLineEdit(this);
  editDeviceId_ = new QLineEdit(this);

  form->addRow(QStringLiteral("SYSID"), editSysid_);
  form->addRow(QStringLiteral("UserID"), editUserId_);
  form->addRow(QStringLiteral("密码（本地校验）"), editPassword_);
  form->addRow(QStringLiteral("TechID"), editTechId_);
  form->addRow(QStringLiteral("DeviceID"), editDeviceId_);
  mainLayout->addLayout(form);

  labelStatus_ = new QLabel(QStringLiteral("尚未验证"), this);
  mainLayout->addWidget(labelStatus_);

  textResult_ = new QTextEdit(this);
  textResult_->setReadOnly(true);
  mainLayout->addWidget(textResult_, 1);

  auto *btnRow = new QHBoxLayout();
  btnVerifyMes_ = new QPushButton(QStringLiteral("MES验证"), this);
  btnLogin_ = new QPushButton(QStringLiteral("登录"), this);
  auto *btnCancel = new QPushButton(QStringLiteral("取消"), this);
  btnRow->addWidget(btnVerifyMes_);
  btnRow->addStretch(1);
  btnRow->addWidget(btnLogin_);
  btnRow->addWidget(btnCancel);
  mainLayout->addLayout(btnRow);

  connect(btnVerifyMes_, &QPushButton::clicked, this, &LoginDialog::onVerifyMes);
  connect(btnLogin_, &QPushButton::clicked, this, &LoginDialog::onLogin);
  connect(btnCancel, &QPushButton::clicked, this, &LoginDialog::reject);

  loadLocalValues();
  setStatus(QStringLiteral("请输入参数后先做 MES 验证。"), false);
}

QString LoginDialog::sysid() const { return editSysid_->text().trimmed(); }
QString LoginDialog::userId() const { return editUserId_->text().trimmed(); }
QString LoginDialog::techId() const { return editTechId_->text().trimmed(); }
QString LoginDialog::deviceId() const { return editDeviceId_->text().trimmed(); }
QString LoginDialog::opNo() const { return verifiedOpNo_; }
bool LoginDialog::mesVerified() const { return mesVerified_; }

void LoginDialog::loadLocalValues() {
  QSettings s(iniPath_, QSettings::IniFormat);
  s.beginGroup(QStringLiteral("login"));
  requireMesVerify_ = (s.value(QStringLiteral("require_mes_verify"), 1).toInt() != 0);
  const QString sysid = s.value(QStringLiteral("last_sysid"), cfg_.mes.sysid).toString();
  const QString userId = s.value(QStringLiteral("last_user_id"), QString()).toString();
  const QString techId = s.value(QStringLiteral("last_tech_id"), QString()).toString();
  const QString deviceId = s.value(QStringLiteral("last_device_id"), QString()).toString();
  s.endGroup();

  editSysid_->setText(sysid);
  editUserId_->setText(userId);
  editTechId_->setText(techId);
  editDeviceId_->setText(deviceId);
  btnLogin_->setEnabled(!requireMesVerify_);
}

void LoginDialog::saveLocalValues() {
  QSettings s(iniPath_, QSettings::IniFormat);
  s.beginGroup(QStringLiteral("login"));
  s.setValue(QStringLiteral("last_sysid"), sysid());
  s.setValue(QStringLiteral("last_user_id"), userId());
  s.setValue(QStringLiteral("last_tech_id"), techId());
  s.setValue(QStringLiteral("last_device_id"), deviceId());
  s.endGroup();

  s.beginGroup(QStringLiteral("mes"));
  s.setValue(QStringLiteral("sysid"), sysid());
  s.endGroup();
  s.sync();
}

QString LoginDialog::configuredPasswordForUser(const QString &userId) const {
  QSettings s(iniPath_, QSettings::IniFormat);
  s.beginGroup(QStringLiteral("login_users"));
  const QString perUser = s.value(userId, QString()).toString();
  s.endGroup();
  if (!perUser.isEmpty())
    return perUser;

  s.beginGroup(QStringLiteral("login"));
  const QString shared = s.value(QStringLiteral("shared_password"), QString()).toString();
  s.endGroup();
  return shared;
}

bool LoginDialog::verifyLocalPassword(QString *err) const {
  const QString uid = userId();
  const QString pwd = editPassword_->text();
  if (uid.isEmpty()) {
    if (err)
      *err = QStringLiteral("请填写 UserID。");
    return false;
  }
  if (pwd.isEmpty()) {
    if (err)
      *err = QStringLiteral("请填写密码。");
    return false;
  }

  const QString configured = configuredPasswordForUser(uid);
  if (configured.isEmpty()) {
    if (err)
      *err = QStringLiteral("本地未配置该用户的密码，请先在 app.ini 的 [login_users] 或 [login]/shared_password 中维护。");
    return false;
  }
  if (configured != pwd) {
    if (err)
      *err = QStringLiteral("本地密码校验失败。");
    return false;
  }
  return true;
}

void LoginDialog::setStatus(const QString &text, bool ok) {
  labelStatus_->setText(text);
  labelStatus_->setStyleSheet(ok ? QStringLiteral("color:#17803d;font-weight:600;")
                                 : QStringLiteral("color:#b3261e;font-weight:600;"));
}

QString LoginDialog::summarizeTechState() const { return verifiedTechSummary_; }

void LoginDialog::onVerifyMes() {
  mesVerified_ = false;
  verifiedOpNo_.clear();
  verifiedTechSummary_.clear();
  btnLogin_->setEnabled(!requireMesVerify_);

  if (sysid().isEmpty() || userId().isEmpty() || techId().isEmpty() ||
      deviceId().isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("MES验证"),
                         QStringLiteral("请先完整填写 SYSID / UserID / TechID / DeviceID。"));
    return;
  }

  MesSysClient client(cfg_);
  textResult_->clear();

  const auto userRes = client.opCheckUser(sysid(), userId());
  textResult_->append(QStringLiteral("[OpCheckUser] HTTP=%1 success=%2")
                          .arg(userRes.http_code)
                          .arg(userRes.success_flag));
  if (!userRes.message.trimmed().isEmpty())
    textResult_->append(QStringLiteral("message: %1").arg(userRes.message));
  if (!userRes.business_ok) {
    const QString msg = userRes.error.isEmpty() ? QStringLiteral("OpCheckUser 校验失败。") : userRes.error;
    textResult_->append(msg);
    setStatus(QStringLiteral("MES验证失败"), false);
    QMessageBox::warning(this, QStringLiteral("MES验证"), msg);
    return;
  }

  const auto techRes = client.opCheckTechState(sysid(), userId(), techId(), deviceId());
  textResult_->append(QStringLiteral("\n[OpCheckTechState] HTTP=%1 success=%2")
                          .arg(techRes.http_code)
                          .arg(techRes.success_flag));
  if (!techRes.message.trimmed().isEmpty())
    textResult_->append(QStringLiteral("message: %1").arg(techRes.message));
  if (!techRes.business_ok) {
    const QString msg = techRes.error.isEmpty() ? QStringLiteral("OpCheckTechState 校验失败。") : techRes.error;
    textResult_->append(msg);
    setStatus(QStringLiteral("MES验证失败"), false);
    QMessageBox::warning(this, QStringLiteral("MES验证"), msg);
    return;
  }

  verifiedOpNo_ = techRes.op_no;
  QStringList parts;
  parts << QStringLiteral("OpNo=%1").arg(verifiedOpNo_.isEmpty() ? QStringLiteral("<empty>") : verifiedOpNo_);
  for (const auto &v : techRes.tech_state) {
    const auto obj = v.toObject();
    parts << QStringLiteral("%1:%2/%3/%4")
                 .arg(obj.value(QStringLiteral("type")).toString(),
                      obj.value(QStringLiteral("id")).toString(),
                      obj.value(QStringLiteral("techCode")).toString(),
                      obj.value(QStringLiteral("techPeriod")).toString());
  }
  verifiedTechSummary_ = parts.join(QStringLiteral(" | "));
  textResult_->append(QStringLiteral("校验通过：%1").arg(verifiedTechSummary_));

  mesVerified_ = true;
  btnLogin_->setEnabled(true);
  setStatus(QStringLiteral("MES验证通过"), true);
  saveLocalValues();
}

void LoginDialog::onLogin() {
  QString err;
  if (!verifyLocalPassword(&err)) {
    QMessageBox::warning(this, QStringLiteral("登录"), err);
    return;
  }
  if (requireMesVerify_ && !mesVerified_) {
    QMessageBox::warning(this, QStringLiteral("登录"),
                         QStringLiteral("请先完成 MES 验证。"));
    return;
  }
  saveLocalValues();
  accept();
}
