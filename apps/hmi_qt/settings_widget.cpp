#include "settings_widget.hpp"

#include <QCheckBox>
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
  connect(ui_->checkAlgoAUseExplicitKOut, &QCheckBox::toggled, this, [this](bool on) {
    ui_->doubleAlgoAKOut->setEnabled(on);
    ui_->doubleAlgoACKOut->setEnabled(on);
  });

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

  // Scan B
  ui_->spinBRings->setValue(c.scan_b.rings);
  ui_->spinBPoints->setValue(c.scan_b.points_per_ring);
  ui_->doubleBAngle->setValue(c.scan_b.angle_step_deg);

  // Algorithm
  ui_->doubleAlgoAKIn->setValue(c.algo.a_b_k_in_mm);
  ui_->doubleAlgoAKOut->setValue(c.algo.a_b_k_out_mm);
  ui_->doubleAlgoACKIn->setValue(c.algo.a_c_k_in_mm);
  ui_->doubleAlgoACKOut->setValue(c.algo.a_c_k_out_mm);
  ui_->doubleAlgoAInnerInputOffset->setValue(c.algo.a_inner_input_offset_mm);
  ui_->doubleAlgoAOuterInputOffset->setValue(c.algo.a_outer_input_offset_mm);
  ui_->checkAlgoAUseExplicitKOut->setChecked(c.algo.a_use_explicit_k_out);
  ui_->doubleAlgoAKOut->setEnabled(c.algo.a_use_explicit_k_out);
  ui_->doubleAlgoACKOut->setEnabled(c.algo.a_use_explicit_k_out);
  ui_->doubleAlgoAProbeBase->setValue(c.algo.a_probe_base_mm);
  ui_->doubleAlgoAAngleOffset->setValue(c.algo.a_angle_offset_deg);
  ui_->doubleAlgoAResidualIn->setValue(c.algo.a_residual_threshold_in_mm);
  ui_->doubleAlgoAResidualOut->setValue(c.algo.a_residual_threshold_out_mm);

  ui_->doubleAlgoBKRunout->setValue(c.algo.b_a_k_runout_mm);
  ui_->doubleAlgoBKRunoutD->setValue(c.algo.b_d_k_runout_mm);
  ui_->doubleAlgoBAngleOffset->setValue(c.algo.b_angle_offset_deg);
  ui_->doubleAlgoBResidual->setValue(c.algo.b_residual_threshold_mm);
  ui_->doubleAlgoBVAngle->setValue(c.algo.b_v_block_angle_deg);
  ui_->spinAlgoBInterp->setValue(c.algo.b_interpolation_factor);
  {
    const QString metric = c.algo.runout_metric.trimmed().toUpper();
    ui_->comboAlgoBRunoutMetric->setCurrentIndex(metric == QStringLiteral("VBLOCK") ? 1 : 0);
  }
  ui_->spinInvalidPointLimit->setValue(c.algo.invalid_point_limit);

  ui_->doubleSpecATotalLenStd->setValue(c.algo.spec_a_total_len.standard_mm);
  ui_->doubleSpecATotalLenTol->setValue(c.algo.spec_a_total_len.tolerance_mm);
  ui_->doubleSpecAIdLeftStd->setValue(c.algo.spec_a_id_left.standard_mm);
  ui_->doubleSpecAIdLeftTol->setValue(c.algo.spec_a_id_left.tolerance_mm);
  ui_->doubleSpecAOdLeftStd->setValue(c.algo.spec_a_od_left.standard_mm);
  ui_->doubleSpecAOdLeftTol->setValue(c.algo.spec_a_od_left.tolerance_mm);
  ui_->doubleSpecAIdRightStd->setValue(c.algo.spec_a_id_right.standard_mm);
  ui_->doubleSpecAIdRightTol->setValue(c.algo.spec_a_id_right.tolerance_mm);
  ui_->doubleSpecAOdRightStd->setValue(c.algo.spec_a_od_right.standard_mm);
  ui_->doubleSpecAOdRightTol->setValue(c.algo.spec_a_od_right.tolerance_mm);

  ui_->doubleSpecBAdLenStd->setValue(c.algo.spec_b_ad_len.standard_mm);
  ui_->doubleSpecBAdLenTol->setValue(c.algo.spec_b_ad_len.tolerance_mm);
  ui_->doubleSpecBBcLenStd->setValue(c.algo.spec_b_bc_len.standard_mm);
  ui_->doubleSpecBBcLenTol->setValue(c.algo.spec_b_bc_len.tolerance_mm);
  ui_->doubleSpecBRunoutLeftStd->setValue(c.algo.spec_b_runout_left.standard_mm);
  ui_->doubleSpecBRunoutLeftTol->setValue(c.algo.spec_b_runout_left.tolerance_mm);
  ui_->doubleSpecBRunoutRightStd->setValue(c.algo.spec_b_runout_right.standard_mm);
  ui_->doubleSpecBRunoutRightTol->setValue(c.algo.spec_b_runout_right.tolerance_mm);
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

  c.scan_b.rings = ui_->spinBRings->value();
  c.scan_b.points_per_ring = ui_->spinBPoints->value();
  c.scan_b.angle_step_deg = ui_->doubleBAngle->value();

  c.algo.a_b_k_in_mm = ui_->doubleAlgoAKIn->value();
  c.algo.a_b_k_out_mm = ui_->doubleAlgoAKOut->value();
  c.algo.a_c_k_in_mm = ui_->doubleAlgoACKIn->value();
  c.algo.a_c_k_out_mm = ui_->doubleAlgoACKOut->value();
  // 兼容旧字段：保留B端值
  c.algo.a_k_in_mm = c.algo.a_b_k_in_mm;
  c.algo.a_k_out_mm = c.algo.a_b_k_out_mm;
  c.algo.a_inner_input_offset_mm = ui_->doubleAlgoAInnerInputOffset->value();
  c.algo.a_outer_input_offset_mm = ui_->doubleAlgoAOuterInputOffset->value();
  c.algo.a_use_explicit_k_out = ui_->checkAlgoAUseExplicitKOut->isChecked();
  c.algo.a_probe_base_mm = ui_->doubleAlgoAProbeBase->value();
  c.algo.a_angle_offset_deg = ui_->doubleAlgoAAngleOffset->value();
  c.algo.a_residual_threshold_in_mm = ui_->doubleAlgoAResidualIn->value();
  c.algo.a_residual_threshold_out_mm = ui_->doubleAlgoAResidualOut->value();

  c.algo.b_a_k_runout_mm = ui_->doubleAlgoBKRunout->value();
  c.algo.b_d_k_runout_mm = ui_->doubleAlgoBKRunoutD->value();
  // 兼容旧字段：保留A点值
  c.algo.b_k_runout_mm = c.algo.b_a_k_runout_mm;
  c.algo.b_angle_offset_deg = ui_->doubleAlgoBAngleOffset->value();
  c.algo.b_residual_threshold_mm = ui_->doubleAlgoBResidual->value();
  c.algo.b_v_block_angle_deg = ui_->doubleAlgoBVAngle->value();
  c.algo.b_interpolation_factor = ui_->spinAlgoBInterp->value();
  c.algo.runout_metric = (ui_->comboAlgoBRunoutMetric->currentIndex() == 1)
                       ? QStringLiteral("VBLOCK")
                       : QStringLiteral("TIR_AXIS");
  c.algo.invalid_point_limit = ui_->spinInvalidPointLimit->value();

  c.algo.spec_a_total_len.standard_mm = ui_->doubleSpecATotalLenStd->value();
  c.algo.spec_a_total_len.tolerance_mm = ui_->doubleSpecATotalLenTol->value();
  c.algo.spec_a_id_left.standard_mm = ui_->doubleSpecAIdLeftStd->value();
  c.algo.spec_a_id_left.tolerance_mm = ui_->doubleSpecAIdLeftTol->value();
  c.algo.spec_a_od_left.standard_mm = ui_->doubleSpecAOdLeftStd->value();
  c.algo.spec_a_od_left.tolerance_mm = ui_->doubleSpecAOdLeftTol->value();
  c.algo.spec_a_id_right.standard_mm = ui_->doubleSpecAIdRightStd->value();
  c.algo.spec_a_id_right.tolerance_mm = ui_->doubleSpecAIdRightTol->value();
  c.algo.spec_a_od_right.standard_mm = ui_->doubleSpecAOdRightStd->value();
  c.algo.spec_a_od_right.tolerance_mm = ui_->doubleSpecAOdRightTol->value();

  c.algo.spec_b_ad_len.standard_mm = ui_->doubleSpecBAdLenStd->value();
  c.algo.spec_b_ad_len.tolerance_mm = ui_->doubleSpecBAdLenTol->value();
  c.algo.spec_b_bc_len.standard_mm = ui_->doubleSpecBBcLenStd->value();
  c.algo.spec_b_bc_len.tolerance_mm = ui_->doubleSpecBBcLenTol->value();
  c.algo.spec_b_runout_left.standard_mm = ui_->doubleSpecBRunoutLeftStd->value();
  c.algo.spec_b_runout_left.tolerance_mm = ui_->doubleSpecBRunoutLeftTol->value();
  c.algo.spec_b_runout_right.standard_mm = ui_->doubleSpecBRunoutRightStd->value();
  c.algo.spec_b_runout_right.tolerance_mm = ui_->doubleSpecBRunoutRightTol->value();

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

  s.beginGroup("algorithm");
  s.setValue("a_k_in_mm", c.algo.a_k_in_mm);
  s.setValue("a_k_out_mm", c.algo.a_k_out_mm);
  s.setValue("a_b_k_in_mm", c.algo.a_b_k_in_mm);
  s.setValue("a_b_k_out_mm", c.algo.a_b_k_out_mm);
  s.setValue("a_c_k_in_mm", c.algo.a_c_k_in_mm);
  s.setValue("a_c_k_out_mm", c.algo.a_c_k_out_mm);
  s.setValue("a_inner_input_offset_mm", c.algo.a_inner_input_offset_mm);
  s.setValue("a_outer_input_offset_mm", c.algo.a_outer_input_offset_mm);
  s.setValue("a_use_explicit_k_out", c.algo.a_use_explicit_k_out ? 1 : 0);
  s.setValue("a_probe_base_mm", c.algo.a_probe_base_mm);
  s.setValue("a_angle_offset_deg", c.algo.a_angle_offset_deg);
  s.setValue("a_residual_threshold_in_mm", c.algo.a_residual_threshold_in_mm);
  s.setValue("a_residual_threshold_out_mm", c.algo.a_residual_threshold_out_mm);
  s.setValue("b_k_runout_mm", c.algo.b_k_runout_mm);
  s.setValue("b_a_k_runout_mm", c.algo.b_a_k_runout_mm);
  s.setValue("b_d_k_runout_mm", c.algo.b_d_k_runout_mm);
  s.setValue("b_angle_offset_deg", c.algo.b_angle_offset_deg);
  s.setValue("b_residual_threshold_mm", c.algo.b_residual_threshold_mm);
  s.setValue("b_v_block_angle_deg", c.algo.b_v_block_angle_deg);
  s.setValue("b_interpolation_factor", c.algo.b_interpolation_factor);
  s.setValue("runout_metric", c.algo.runout_metric);
  s.setValue("invalid_point_limit", c.algo.invalid_point_limit);
  s.setValue("spec_a_total_len_standard_mm", c.algo.spec_a_total_len.standard_mm);
  s.setValue("spec_a_total_len_tolerance_mm", c.algo.spec_a_total_len.tolerance_mm);
  s.setValue("spec_a_id_left_standard_mm", c.algo.spec_a_id_left.standard_mm);
  s.setValue("spec_a_id_left_tolerance_mm", c.algo.spec_a_id_left.tolerance_mm);
  s.setValue("spec_a_od_left_standard_mm", c.algo.spec_a_od_left.standard_mm);
  s.setValue("spec_a_od_left_tolerance_mm", c.algo.spec_a_od_left.tolerance_mm);
  s.setValue("spec_a_id_right_standard_mm", c.algo.spec_a_id_right.standard_mm);
  s.setValue("spec_a_id_right_tolerance_mm", c.algo.spec_a_id_right.tolerance_mm);
  s.setValue("spec_a_od_right_standard_mm", c.algo.spec_a_od_right.standard_mm);
  s.setValue("spec_a_od_right_tolerance_mm", c.algo.spec_a_od_right.tolerance_mm);

  s.setValue("spec_b_ad_len_standard_mm", c.algo.spec_b_ad_len.standard_mm);
  s.setValue("spec_b_ad_len_tolerance_mm", c.algo.spec_b_ad_len.tolerance_mm);
  s.setValue("spec_b_bc_len_standard_mm", c.algo.spec_b_bc_len.standard_mm);
  s.setValue("spec_b_bc_len_tolerance_mm", c.algo.spec_b_bc_len.tolerance_mm);
  s.setValue("spec_b_runout_left_standard_mm", c.algo.spec_b_runout_left.standard_mm);
  s.setValue("spec_b_runout_left_tolerance_mm", c.algo.spec_b_runout_left.tolerance_mm);
  s.setValue("spec_b_runout_right_standard_mm", c.algo.spec_b_runout_right.standard_mm);
  s.setValue("spec_b_runout_right_tolerance_mm", c.algo.spec_b_runout_right.tolerance_mm);
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
    QMessageBox::warning(this, "数据库", err);
    return;
  }
  QMessageBox::information(this, "数据库", "DB connection OK");
}
