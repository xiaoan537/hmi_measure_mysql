#include "settings_widget.hpp"

#include <QMessageBox>
#include <QSettings>

#include "core/db.hpp"

#include "ui_settings_widget.h"

SettingsWidget::SettingsWidget(const core::AppConfig& cfg, const QString& iniPath, QWidget* parent)
  : QWidget(parent), ui_(new Ui::SettingsWidget), cfg_(cfg), iniPath_(iniPath)
{
  ui_->setupUi(this);
  loadToUi(cfg_);

  connect(ui_->btnReload, &QPushButton::clicked, this, &SettingsWidget::onReloadClicked);
  connect(ui_->btnApply, &QPushButton::clicked, this, &SettingsWidget::onApplyClicked);
  connect(ui_->btnSave, &QPushButton::clicked, this, &SettingsWidget::onSaveClicked);
  connect(ui_->btnTestDb, &QPushButton::clicked, this, &SettingsWidget::onTestDbClicked);

  ui_->lblIniPath->setText(iniPath_.isEmpty() ? "(unknown)" : iniPath_);
}

SettingsWidget::~SettingsWidget()
{
  delete ui_;
}

void SettingsWidget::loadToUi(const core::AppConfig& c)
{
  // Paths
  ui_->editDataRoot->setText(c.paths.data_root);
  ui_->editRawDir->setText(c.paths.raw_dir);
  ui_->editLogDir->setText(c.paths.log_dir);

  // DB
  ui_->editDbDriver->setText(c.db.driver);
  ui_->editDbHost->setText(c.db.host);
  ui_->spinDbPort->setValue(c.db.port);
  ui_->editDbName->setText(c.db.name);
  ui_->editDbUser->setText(c.db.user);
  ui_->editDbPass->setText(c.db.pass);
  ui_->editDbOptions->setText(c.db.options);

  // MES
  ui_->chkMesEnabled->setChecked(c.mes.enabled);
  ui_->chkMesManual->setChecked(c.mes.manual_enabled);
  ui_->chkMesAuto->setChecked(c.mes.auto_enabled);
  ui_->spinMesInterval->setValue(c.mes.auto_interval_ms);
  ui_->editMesUrl->setText(c.mes.url);
  ui_->editMesToken->setText(c.mes.auth_token);
  ui_->spinMesTimeout->setValue(c.mes.timeout_ms);
  ui_->spinRetryBase->setValue(c.mes.retry_base_seconds);
  ui_->spinRetryMax->setValue(c.mes.retry_max_seconds);

  // Scan A
  ui_->spinARings->setValue(c.scan_a.rings);
  ui_->spinAPoints->setValue(c.scan_a.points_per_ring);
  ui_->doubleAAngle->setValue(c.scan_a.angle_step_deg);
  ui_->spinAOrder->setValue(c.scan_a.order_code);

  // Scan B
  ui_->spinBRings->setValue(c.scan_b.rings);
  ui_->spinBPoints->setValue(c.scan_b.points_per_ring);
  ui_->doubleBAngle->setValue(c.scan_b.angle_step_deg);
  ui_->spinBOrder->setValue(c.scan_b.order_code);
}

core::AppConfig SettingsWidget::readFromUi() const
{
  core::AppConfig c = cfg_;

  c.paths.data_root = ui_->editDataRoot->text().trimmed();
  c.paths.raw_dir = ui_->editRawDir->text().trimmed();
  c.paths.log_dir = ui_->editLogDir->text().trimmed();

  c.db.driver = ui_->editDbDriver->text().trimmed();
  c.db.host = ui_->editDbHost->text().trimmed();
  c.db.port = ui_->spinDbPort->value();
  c.db.name = ui_->editDbName->text().trimmed();
  c.db.user = ui_->editDbUser->text().trimmed();
  c.db.pass = ui_->editDbPass->text();
  c.db.options = ui_->editDbOptions->text().trimmed();

  c.mes.enabled = ui_->chkMesEnabled->isChecked();
  c.mes.manual_enabled = ui_->chkMesManual->isChecked();
  c.mes.auto_enabled = ui_->chkMesAuto->isChecked();
  c.mes.auto_interval_ms = ui_->spinMesInterval->value();
  c.mes.url = ui_->editMesUrl->text().trimmed();
  c.mes.auth_token = ui_->editMesToken->text();
  c.mes.timeout_ms = ui_->spinMesTimeout->value();
  c.mes.retry_base_seconds = ui_->spinRetryBase->value();
  c.mes.retry_max_seconds = ui_->spinRetryMax->value();

  c.scan_a.rings = ui_->spinARings->value();
  c.scan_a.points_per_ring = ui_->spinAPoints->value();
  c.scan_a.angle_step_deg = ui_->doubleAAngle->value();
  c.scan_a.order_code = (quint16)ui_->spinAOrder->value();

  c.scan_b.rings = ui_->spinBRings->value();
  c.scan_b.points_per_ring = ui_->spinBPoints->value();
  c.scan_b.angle_step_deg = ui_->doubleBAngle->value();
  c.scan_b.order_code = (quint16)ui_->spinBOrder->value();

  return c;
}

void SettingsWidget::onReloadClicked()
{
  loadToUi(cfg_);
}

void SettingsWidget::onApplyClicked()
{
  const auto c = readFromUi();
  emit configApplied(c);
  QMessageBox::information(this, "Settings", "Applied to runtime (in-memory).\nNote: services may need restart to take effect.");
}

bool SettingsWidget::saveToIni(const core::AppConfig& c, QString* err)
{
  if (iniPath_.isEmpty())
  {
    if (err) *err = "iniPath is empty";
    return false;
  }
  QSettings s(iniPath_, QSettings::IniFormat);

  s.beginGroup("db");
  s.setValue("driver", c.db.driver);
  s.setValue("host", c.db.host);
  s.setValue("port", c.db.port);
  s.setValue("name", c.db.name);
  s.setValue("user", c.db.user);
  s.setValue("pass", c.db.pass);
  s.setValue("options", c.db.options);
  s.endGroup();

  s.beginGroup("paths");
  s.setValue("data_root", c.paths.data_root);
  s.setValue("raw_dir", c.paths.raw_dir);
  s.setValue("log_dir", c.paths.log_dir);
  s.endGroup();

  s.beginGroup("mes");
  s.setValue("enabled", c.mes.enabled ? 1 : 0);
  s.setValue("manual_enabled", c.mes.manual_enabled ? 1 : 0);
  s.setValue("auto_enabled", c.mes.auto_enabled ? 1 : 0);
  s.setValue("auto_interval_ms", c.mes.auto_interval_ms);
  s.setValue("url", c.mes.url);
  s.setValue("auth_token", c.mes.auth_token);
  s.setValue("timeout_ms", c.mes.timeout_ms);
  s.setValue("retry_base_seconds", c.mes.retry_base_seconds);
  s.setValue("retry_max_seconds", c.mes.retry_max_seconds);
  s.endGroup();

  s.beginGroup("scan_a");
  s.setValue("rings", c.scan_a.rings);
  s.setValue("points_per_ring", c.scan_a.points_per_ring);
  s.setValue("angle_step_deg", c.scan_a.angle_step_deg);
  s.setValue("order_code", (int)c.scan_a.order_code);
  s.endGroup();

  s.beginGroup("scan_b");
  s.setValue("rings", c.scan_b.rings);
  s.setValue("points_per_ring", c.scan_b.points_per_ring);
  s.setValue("angle_step_deg", c.scan_b.angle_step_deg);
  s.setValue("order_code", (int)c.scan_b.order_code);
  s.endGroup();

  s.sync();
  if (s.status() != QSettings::NoError)
  {
    if (err) *err = "QSettings sync failed";
    return false;
  }
  return true;
}

void SettingsWidget::onSaveClicked()
{
  const auto c = readFromUi();
  QString err;
  if (!saveToIni(c, &err))
  {
    QMessageBox::warning(this, "Save", err);
    return;
  }
  cfg_ = c;
  emit configSaved(iniPath_);
  QMessageBox::information(this, "Save", "Saved to app.ini");
}

void SettingsWidget::onTestDbClicked()
{
  core::Db db;
  QString err;
  auto c = readFromUi();
  if (!db.open(c.db, &err))
  {
    QMessageBox::warning(this, "DB", err);
    return;
  }
  QMessageBox::information(this, "DB", "DB connection OK");
}
