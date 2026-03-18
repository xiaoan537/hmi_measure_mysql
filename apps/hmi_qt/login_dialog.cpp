#include "login_dialog.hpp"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <limits>

#include "mes_sys_client.hpp"

namespace {

QString formatDateTime(const QDateTime &dt) {
  return dt.isValid() ? dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                      : QStringLiteral("--");
}

QString runCommandAndCapture(const QString &program, const QStringList &args,
                             int timeoutMs, int *exitCode) {
  QProcess proc;
  proc.start(program, args);
  if (!proc.waitForStarted(timeoutMs)) {
    if (exitCode)
      *exitCode = -1;
    return QStringLiteral("启动失败: %1").arg(program);
  }
  if (!proc.waitForFinished(timeoutMs)) {
    proc.kill();
    proc.waitForFinished(1000);
    if (exitCode)
      *exitCode = -1;
    return QStringLiteral("执行超时: %1").arg(program);
  }
  if (exitCode)
    *exitCode = proc.exitCode();
  QString out = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
  const QString err = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
  if (!err.isEmpty()) {
    if (!out.isEmpty())
      out += QStringLiteral("\n");
    out += err;
  }
  return out;
}

} // namespace

LoginDialog::LoginDialog(const core::AppConfig &cfg, const QString &iniPath,
                         QWidget *parent)
    : QDialog(parent), cfg_(cfg), iniPath_(iniPath) {
  setWindowTitle(QStringLiteral("登录 / MES验证"));
  resize(680, 520);
  setModal(true);

  auto *mainLayout = new QVBoxLayout(this);
  auto *tip = new QLabel(
      QStringLiteral("请输入本地登录信息。密码仅在本地校验；MES 会校验 UserID / TechID / DeviceID。\n"
                     "登录框会每 10 秒自动刷新一次 MES 心跳时间，并在时间差超过阈值时尝试自动校时。"),
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

  auto *timeBox = new QVBoxLayout();
  timeBox->setSpacing(4);
  labelHeartbeatState_ = new QLabel(QStringLiteral("MES 心跳：未检测"), this);
  labelMesTime_ = new QLabel(QStringLiteral("MES 时间：--"), this);
  labelLocalTime_ = new QLabel(QStringLiteral("本机时间：--"), this);
  labelTimeDiff_ = new QLabel(QStringLiteral("时间差：--"), this);
  labelSyncState_ = new QLabel(QStringLiteral("校时状态：未执行"), this);
  timeBox->addWidget(labelHeartbeatState_);
  timeBox->addWidget(labelMesTime_);
  timeBox->addWidget(labelLocalTime_);
  timeBox->addWidget(labelTimeDiff_);
  timeBox->addWidget(labelSyncState_);
  mainLayout->addLayout(timeBox);

  labelStatus_ = new QLabel(QStringLiteral("尚未验证"), this);
  mainLayout->addWidget(labelStatus_);

  textResult_ = new QTextEdit(this);
  textResult_->setReadOnly(true);
  mainLayout->addWidget(textResult_, 1);

  auto *btnRow = new QHBoxLayout();
  btnVerifyMes_ = new QPushButton(QStringLiteral("MES验证"), this);
  btnRefreshMesTime_ = new QPushButton(QStringLiteral("刷新MES时间"), this);
  btnLogin_ = new QPushButton(QStringLiteral("登录"), this);
  auto *btnCancel = new QPushButton(QStringLiteral("取消"), this);
  btnRow->addWidget(btnVerifyMes_);
  btnRow->addWidget(btnRefreshMesTime_);
  btnRow->addStretch(1);
  btnRow->addWidget(btnLogin_);
  btnRow->addWidget(btnCancel);
  mainLayout->addLayout(btnRow);

  connect(btnVerifyMes_, &QPushButton::clicked, this,
          &LoginDialog::onVerifyMes);
  connect(btnRefreshMesTime_, &QPushButton::clicked, this,
          &LoginDialog::onRefreshMesTime);
  connect(btnLogin_, &QPushButton::clicked, this, &LoginDialog::onLogin);
  connect(btnCancel, &QPushButton::clicked, this, &LoginDialog::reject);

  uiTimer_ = new QTimer(this);
  uiTimer_->setInterval(1000);
  connect(uiTimer_, &QTimer::timeout, this, &LoginDialog::onUiTick);

  heartbeatTimer_ = new QTimer(this);
  heartbeatTimer_->setInterval(
      qMax(1000, cfg_.mes.heartbeat_interval_ms > 0
                      ? cfg_.mes.heartbeat_interval_ms
                      : 10000));
  connect(heartbeatTimer_, &QTimer::timeout, this,
          &LoginDialog::onAutoRefreshMesTime);

  loadLocalValues();
  setStatus(requireMesVerify_ ? QStringLiteral("请先同步 MES 时间并完成 MES 验证。")
                              : QStringLiteral("当前未强制 MES 验证，可直接使用本地密码登录。"),
            !requireMesVerify_);
  updateTimeInfoUi();
  updateLoginButtonState();

  uiTimer_->start();
  heartbeatTimer_->start();
  QTimer::singleShot(0, this, &LoginDialog::onAutoRefreshMesTime);
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
  requireMesVerify_ =
      (s.value(QStringLiteral("require_mes_verify"), 1).toInt() != 0);
  const QString sysid =
      s.value(QStringLiteral("last_sysid"), cfg_.mes.sysid).toString();
  const QString userId =
      s.value(QStringLiteral("last_user_id"), QString()).toString();
  const QString techId =
      s.value(QStringLiteral("last_tech_id"), QString()).toString();
  const QString deviceId =
      s.value(QStringLiteral("last_device_id"), QString()).toString();
  s.endGroup();

  editSysid_->setText(sysid);
  editUserId_->setText(userId);
  editTechId_->setText(techId);
  editDeviceId_->setText(deviceId);
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
  const QString shared =
      s.value(QStringLiteral("shared_password"), QString()).toString();
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

void LoginDialog::appendResultLine(const QString &line) {
  if (!line.trimmed().isEmpty())
    textResult_->append(line);
}

QDateTime LoginDialog::parseMesDateTime(const QString &text) const {
  const QString raw = text.trimmed();
  if (raw.isEmpty())
    return QDateTime();

  const QList<QString> patterns = {
      QStringLiteral("yyyy-MM-dd HH:mm:ss"),
      QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"),
      QStringLiteral("yyyy/MM/dd HH:mm:ss"),
      QStringLiteral("yyyy/MM/dd HH:mm:ss.zzz"),
      QStringLiteral("yyyy-MM-ddTHH:mm:ss"),
      QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz")};
  for (const QString &p : patterns) {
    QDateTime dt = QDateTime::fromString(raw, p);
    if (dt.isValid())
      return dt;
  }

  QDateTime iso = QDateTime::fromString(raw, Qt::ISODate);
  if (iso.isValid())
    return iso.toLocalTime();
  iso = QDateTime::fromString(raw, Qt::ISODateWithMs);
  if (iso.isValid())
    return iso.toLocalTime();
  return QDateTime();
}

QDateTime LoginDialog::currentMesEstimate() const {
  if (!mesTimeSample_.isValid() || !mesTimeSampleLocal_.isValid())
    return QDateTime();
  return mesTimeSample_.addMSecs(
      mesTimeSampleLocal_.msecsTo(QDateTime::currentDateTime()));
}

qint64 LoginDialog::currentTimeDiffSeconds() const {
  const QDateTime mesNow = currentMesEstimate();
  if (!mesNow.isValid())
    return std::numeric_limits<qint64>::max();
  return qAbs(mesNow.secsTo(QDateTime::currentDateTime()));
}

bool LoginDialog::trySyncSystemTime(const QDateTime &target, QString *err) const {
  if (!target.isValid()) {
    if (err)
      *err = QStringLiteral("MES 时间无效，无法自动校时。");
    return false;
  }

  const QString targetText = target.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  int code = -1;
  QString out = runCommandAndCapture(QStringLiteral("timedatectl"),
                                     {QStringLiteral("set-ntp"),
                                      QStringLiteral("false")},
                                     5000, &code);
  Q_UNUSED(out);

  QString result = runCommandAndCapture(
      QStringLiteral("timedatectl"),
      {QStringLiteral("set-time"), targetText}, 8000, &code);
  if (code == 0)
    return true;

  QString fallback = runCommandAndCapture(QStringLiteral("date"),
                                          {QStringLiteral("-s"), targetText},
                                          8000, &code);
  if (code == 0)
    return true;

  if (err) {
    QStringList parts;
    if (!result.trimmed().isEmpty())
      parts << result.trimmed();
    if (!fallback.trimmed().isEmpty() && fallback.trimmed() != result.trimmed())
      parts << fallback.trimmed();
    if (parts.isEmpty())
      parts << QStringLiteral("系统未授予修改时间的权限。");
    *err = parts.join(QStringLiteral(" | "));
  }
  return false;
}

bool LoginDialog::ensureTimeAligned(bool interactive, QString *err) {
  timeAligned_ = false;
  if (!heartbeatOk_ || !mesTimeSample_.isValid()) {
    const QString msg = heartbeatMessage_.isEmpty()
                            ? QStringLiteral("尚未获取到有效 MES 时间。")
                            : heartbeatMessage_;
    lastTimeSyncMessage_ = msg;
    if (err)
      *err = msg;
    updateTimeInfoUi();
    return false;
  }

  const int threshold = qMax(1, cfg_.mes.max_time_diff_seconds);
  qint64 diffSec = currentTimeDiffSeconds();
  if (diffSec <= threshold) {
    lastTimeSyncMessage_ = QStringLiteral("时间差在允许范围内。无需校时。");
    timeAligned_ = true;
    updateTimeInfoUi();
    return true;
  }

  appendResultLine(QStringLiteral("检测到本机与 MES 时间差 %1 秒，开始自动校时...").arg(diffSec));
  QString syncErr;
  const QDateTime mesNow = currentMesEstimate();
  if (trySyncSystemTime(mesNow, &syncErr)) {
    diffSec = currentTimeDiffSeconds();
    if (diffSec <= threshold) {
      lastTimeSyncMessage_ = QStringLiteral("自动校时成功。");
      appendResultLine(lastTimeSyncMessage_);
      timeAligned_ = true;
      updateTimeInfoUi();
      return true;
    }
    syncErr = QStringLiteral("自动校时后时间差仍为 %1 秒，请检查系统权限或手动校时。")
                  .arg(diffSec);
  }

  lastTimeSyncMessage_ = QStringLiteral("自动校时失败：%1").arg(syncErr);
  appendResultLine(lastTimeSyncMessage_);
  appendResultLine(QStringLiteral("请按界面显示的 MES 时间手动校准系统时间，然后点击“刷新MES时间”。"));
  if (interactive) {
    QMessageBox::warning(this, QStringLiteral("MES时间校准"),
                         QStringLiteral("MES 时间：%1\n本机时间：%2\n时间差：%3 秒\n\n自动校时失败：%4\n请手动设置系统时间后再次刷新。")
                             .arg(formatDateTime(currentMesEstimate()),
                                  formatDateTime(QDateTime::currentDateTime()),
                                  QString::number(currentTimeDiffSeconds()), syncErr));
  }
  if (err)
    *err = lastTimeSyncMessage_;
  updateTimeInfoUi();
  return false;
}

void LoginDialog::updateTimeInfoUi() {
  const QDateTime localNow = QDateTime::currentDateTime();
  const QDateTime mesNow = currentMesEstimate();
  labelHeartbeatState_->setText(QStringLiteral("MES 心跳：%1")
                                    .arg(heartbeatOk_ ? QStringLiteral("在线")
                                                      : QStringLiteral("离线 / 未就绪")));
  labelHeartbeatState_->setStyleSheet(
      heartbeatOk_ ? QStringLiteral("color:#17803d;font-weight:600;")
                   : QStringLiteral("color:#b3261e;font-weight:600;"));
  labelMesTime_->setText(QStringLiteral("MES 时间：%1")
                             .arg(mesNow.isValid() ? formatDateTime(mesNow)
                                                   : QStringLiteral("--")));
  labelLocalTime_->setText(
      QStringLiteral("本机时间：%1").arg(formatDateTime(localNow)));
  if (mesNow.isValid()) {
    const qint64 diff = currentTimeDiffSeconds();
    labelTimeDiff_->setText(
        QStringLiteral("时间差：%1 秒（阈值 %2 秒）")
            .arg(diff)
            .arg(qMax(1, cfg_.mes.max_time_diff_seconds)));
    labelTimeDiff_->setStyleSheet(
        diff <= qMax(1, cfg_.mes.max_time_diff_seconds)
            ? QStringLiteral("color:#17803d;")
            : QStringLiteral("color:#b3261e;font-weight:600;"));
  } else {
    labelTimeDiff_->setText(QStringLiteral("时间差：--"));
    labelTimeDiff_->setStyleSheet(QString());
  }

  QString syncText = lastTimeSyncMessage_.isEmpty()
                         ? QStringLiteral("校时状态：未执行")
                         : QStringLiteral("校时状态：%1").arg(lastTimeSyncMessage_);
  if (!mesTimeRaw_.trimmed().isEmpty()) {
    syncText += QStringLiteral("（MES原始时间：%1）").arg(mesTimeRaw_);
  }
  labelSyncState_->setText(syncText);
  labelSyncState_->setStyleSheet(
      timeAligned_ ? QStringLiteral("color:#17803d;")
                   : QStringLiteral("color:#b3261e;"));
  updateLoginButtonState();
}

void LoginDialog::updateLoginButtonState() {
  if (!requireMesVerify_) {
    btnLogin_->setEnabled(true);
    return;
  }
  btnLogin_->setEnabled(mesVerified_ && timeAligned_ && heartbeatOk_);
}

bool LoginDialog::refreshMesHeartbeat(bool interactive, QString *err) {
  heartbeatOk_ = false;
  timeAligned_ = false;
  heartbeatMessage_.clear();
  mesTimeRaw_.clear();
  const QString sid = sysid();
  if (sid.isEmpty()) {
    heartbeatMessage_ = QStringLiteral("请先填写 SYSID，再刷新 MES 时间。");
    if (err)
      *err = heartbeatMessage_;
    updateTimeInfoUi();
    if (interactive)
      QMessageBox::warning(this, QStringLiteral("刷新MES时间"), heartbeatMessage_);
    return false;
  }

  MesSysClient client(cfg_);
  const auto hb = client.heartbeat(sid);
  mesTimeRaw_ = hb.server_time;
  if (!hb.business_ok) {
    heartbeatMessage_ = hb.error.isEmpty() ? QStringLiteral("Heartbeat 校验失败。")
                                           : hb.error;
    lastTimeSyncMessage_ = QStringLiteral("未拿到有效 MES 时间。请检查网络或接口配置。");
    appendResultLine(QStringLiteral("[Heartbeat] HTTP=%1 message=%2")
                         .arg(hb.http_code)
                         .arg(heartbeatMessage_));
    if (err)
      *err = heartbeatMessage_;
    updateTimeInfoUi();
    if (interactive)
      QMessageBox::warning(this, QStringLiteral("刷新MES时间"), heartbeatMessage_);
    return false;
  }

  const QDateTime serverDt = parseMesDateTime(hb.server_time);
  if (!serverDt.isValid()) {
    heartbeatMessage_ = QStringLiteral("Heartbeat 成功，但 MES 返回时间格式无法解析：%1")
                            .arg(hb.server_time);
    lastTimeSyncMessage_ = heartbeatMessage_;
    appendResultLine(heartbeatMessage_);
    if (err)
      *err = heartbeatMessage_;
    updateTimeInfoUi();
    if (interactive)
      QMessageBox::warning(this, QStringLiteral("刷新MES时间"), heartbeatMessage_);
    return false;
  }

  heartbeatOk_ = true;
  heartbeatMessage_ = QStringLiteral("Heartbeat 成功");
  mesTimeSample_ = serverDt;
  mesTimeSampleLocal_ = QDateTime::currentDateTime();
  appendResultLine(QStringLiteral("[Heartbeat] MES=%1, Local=%2")
                       .arg(formatDateTime(mesTimeSample_),
                            formatDateTime(mesTimeSampleLocal_)));

  QString alignErr;
  const bool aligned = ensureTimeAligned(interactive, &alignErr);
  if (!aligned && err)
    *err = alignErr;
  updateTimeInfoUi();
  return aligned;
}

void LoginDialog::onRefreshMesTime() {
  QString err;
  refreshMesHeartbeat(true, &err);
  if (heartbeatOk_ && timeAligned_) {
    setStatus(requireMesVerify_ && !mesVerified_
                  ? QStringLiteral("MES 时间已同步，请继续执行 MES 验证。")
                  : QStringLiteral("MES 时间正常，可登录。"),
              true);
  } else if (!err.trimmed().isEmpty()) {
    setStatus(err, false);
  }
}

void LoginDialog::onAutoRefreshMesTime() {
  QString err;
  refreshMesHeartbeat(false, &err);
  if (heartbeatOk_ && timeAligned_) {
    setStatus(requireMesVerify_ && !mesVerified_
                  ? QStringLiteral("MES 时间已同步，请继续执行 MES 验证。")
                  : QStringLiteral("MES 时间正常，可登录。"),
              true);
  } else if (!err.trimmed().isEmpty() && requireMesVerify_) {
    setStatus(err, false);
  }
}

void LoginDialog::onUiTick() { updateTimeInfoUi(); }

void LoginDialog::onVerifyMes() {
  mesVerified_ = false;
  verifiedOpNo_.clear();
  verifiedTechSummary_.clear();
  updateLoginButtonState();

  if (sysid().isEmpty() || userId().isEmpty() || techId().isEmpty() ||
      deviceId().isEmpty()) {
    QMessageBox::warning(
        this, QStringLiteral("MES验证"),
        QStringLiteral("请先完整填写 SYSID / UserID / TechID / DeviceID。"));
    return;
  }

  QString hbErr;
  if (!refreshMesHeartbeat(true, &hbErr)) {
    setStatus(hbErr.isEmpty() ? QStringLiteral("MES 时间校准失败") : hbErr,
              false);
    return;
  }

  MesSysClient client(cfg_);
  textResult_->append(QString());

  const auto userRes = client.opCheckUser(sysid(), userId());
  textResult_->append(QStringLiteral("[OpCheckUser] HTTP=%1 success=%2")
                          .arg(userRes.http_code)
                          .arg(userRes.success_flag));
  if (!userRes.message.trimmed().isEmpty())
    textResult_->append(QStringLiteral("message: %1").arg(userRes.message));
  if (!userRes.business_ok) {
    const QString msg = userRes.error.isEmpty()
                            ? QStringLiteral("OpCheckUser 校验失败。")
                            : userRes.error;
    textResult_->append(msg);
    setStatus(QStringLiteral("MES验证失败"), false);
    QMessageBox::warning(this, QStringLiteral("MES验证"), msg);
    return;
  }

  const auto techRes =
      client.opCheckTechState(sysid(), userId(), techId(), deviceId());
  textResult_->append(QStringLiteral("\n[OpCheckTechState] HTTP=%1 success=%2")
                          .arg(techRes.http_code)
                          .arg(techRes.success_flag));
  if (!techRes.message.trimmed().isEmpty())
    textResult_->append(QStringLiteral("message: %1").arg(techRes.message));
  if (!techRes.business_ok) {
    const QString msg = techRes.error.isEmpty()
                            ? QStringLiteral("OpCheckTechState 校验失败。")
                            : techRes.error;
    textResult_->append(msg);
    setStatus(QStringLiteral("MES验证失败"), false);
    QMessageBox::warning(this, QStringLiteral("MES验证"), msg);
    return;
  }

  verifiedOpNo_ = techRes.op_no;
  QStringList parts;
  parts << QStringLiteral("OpNo=%1").arg(
      verifiedOpNo_.isEmpty() ? QStringLiteral("<empty>") : verifiedOpNo_);
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
  updateLoginButtonState();
  setStatus(QStringLiteral("MES验证通过，可登录。"), true);
  saveLocalValues();
}

void LoginDialog::onLogin() {
  QString err;
  if (!verifyLocalPassword(&err)) {
    QMessageBox::warning(this, QStringLiteral("登录"), err);
    return;
  }

  if (requireMesVerify_) {
    QString hbErr;
    if (!refreshMesHeartbeat(true, &hbErr)) {
      QMessageBox::warning(this, QStringLiteral("登录"),
                           hbErr.isEmpty() ? QStringLiteral("MES 时间未就绪，请先刷新。")
                                           : hbErr);
      return;
    }
    if (!mesVerified_) {
      QMessageBox::warning(this, QStringLiteral("登录"),
                           QStringLiteral("请先完成 MES 验证。"));
      return;
    }
    if (!timeAligned_) {
      QMessageBox::warning(this, QStringLiteral("登录"),
                           QStringLiteral("MES 时间与本机时间仍不一致，请手动校时后再刷新。"));
      return;
    }
  }

  saveLocalValues();
  accept();
}
