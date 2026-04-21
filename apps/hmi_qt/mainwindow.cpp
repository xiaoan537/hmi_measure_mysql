#include "alarm_widget.hpp"
#include "calibration_widget.hpp"
#include "data_widget.hpp"
#include "dev_tools_widget.hpp"
#include "manual_maintain_widget.hpp"
#include "diagnostics_widget.hpp"
#include "mainwindow.hpp"
#include "mes_upload_widget.hpp"
#include "mes_worker.hpp"
#include "plc_step_rules_v26.hpp"
#include "production_widget.hpp"
#include "raw_viewer_widget.hpp"
#include "settings_widget.hpp"
#include "todo_widget.hpp"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QAbstractButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVariantMap>
#include <QStringList>

#include <QSizePolicy>

#include "ui_mainwindow.h"

#include <cmath>
#include <memory>

#include "core/measurement_geometry_algorithms.hpp"
#include "core/measurement_pipeline.hpp"
#include "core/plc_contract_v2.hpp"
#include "core/plc_runtime_v2.hpp"
#include "core/plc_addresses_v26.hpp"
#include "core/plc_codec_v26.hpp"

namespace {
QString mailboxPartTypeText(const core::PlcMailboxSnapshot &snapshot) {
  const QChar pt = snapshot.part_type.toUpper();
  if (pt == QChar('A') || pt == QChar('B')) {
    return QString(pt);
  }
  return QStringLiteral("-");
}

QString productionStepStateText(quint16 stepState, quint16 /*scanDone*/) {
  return plc_step_rules_v26::productionStepText(stepState);
}

QString calibrationStepStateText(quint16 stepState) {
  return plc_step_rules_v26::calibrationStepText(stepState);
}

QString stepStateText(quint16 stepState, quint16 scanDone, bool calibrationContext) {
  return calibrationContext ? calibrationStepStateText(stepState)
                            : productionStepStateText(stepState, scanDone);
}


QString plcModeTextV26(int mode) { return core::plc_codec_v26::plcModeText(static_cast<qint16>(mode)); }

QString axisNameByIndex(int axisIndex) { return core::plc_v26::axisName(axisIndex); }

QString machineStateText(quint16 machineState) { return core::plc_codec_v26::decodeMachineState(machineState).text; }

QString interlockMaskText(quint32 mask) {
  const QStringList reasons = core::plc_codec_v26::decodeInterlockBits(mask);
  return reasons.isEmpty() ? QStringLiteral("无") : reasons.join(QStringLiteral(" | "));
}

bool isCalibrationStepCode(quint16 stepState) {
  return plc_step_rules_v26::isCalibrationStep(stepState);
}

bool isCalibrationContext(bool hasLastStatus, bool calibrationFlowExpected, quint16 stepState) {
  return hasLastStatus && calibrationFlowExpected && isCalibrationStepCode(stepState);
}

quint32 mapArg(const QVariantMap &args, const QString &key, quint32 def = 0) {
  const QVariant v = args.value(key);
  if (!v.isValid()) return def;
  bool ok = false;
  const quint32 out = v.toUInt(&ok);
  return ok ? out : def;
}

QString cmdBitsText(quint16 bits) {
  QStringList names;
  if (bits & core::plc_v26::kCmdInitializeBit) names << QStringLiteral("初始化");
  if (bits & core::plc_v26::kCmdStartMeasureBit) names << QStringLiteral("开始测量");
  if (bits & core::plc_v26::kCmdStartCalibrationBit) names << QStringLiteral("开始标定");
  if (bits & core::plc_v26::kCmdStopBit) names << QStringLiteral("停止");
  if (bits & core::plc_v26::kCmdResetBit) names << QStringLiteral("复位");
  if (bits & core::plc_v26::kCmdRetestCurrentBit) names << QStringLiteral("当前件复测");
  if (bits & core::plc_v26::kCmdContinueWithoutRetestBit) names << QStringLiteral("继续(不复测)");
  if (bits & core::plc_v26::kCmdAlarmMuteBit) names << QStringLiteral("报警静音");
  if (bits & core::plc_v26::kCmdRejectMask) names << QStringLiteral("拒绝标志");
  if (names.isEmpty()) return QStringLiteral("NONE");
  return names.join(QStringLiteral("|"));
}

QString rejectInstructionText(quint16 bits) {
  QStringList reasons;
  if (bits & 0x0001u) reasons << QStringLiteral("已在执行(请勿重复操作)");
  if (bits & 0x0002u) reasons << QStringLiteral("存在错误,请先复位");
  const quint16 unknown = static_cast<quint16>(bits & ~0x0003u);
  if (unknown != 0) {
    reasons << QStringLiteral("其他位=0x%1").arg(QString::number(unknown, 16).toUpper());
  }
  if (reasons.isEmpty()) return QStringLiteral("无");
  return reasons.join(QStringLiteral("|"));
}

struct Channel72Series {
  QVector<double> values_mm;
  QVector<bool> valid_mask;
  int invalid_points = 0;
  bool invalid_too_many = false;
};

QString formatNumber(double v, int prec = 6) {
  return std::isfinite(v) ? QString::number(v, 'f', prec) : QStringLiteral("--");
}

QString normalizedRunoutMetric(const QString &v) {
  const QString metric = v.trimmed().toUpper();
  return (metric == QStringLiteral("VBLOCK")) ? metric : QStringLiteral("TIR_AXIS");
}

double selectedRunoutValue(const core::RunoutResult &r, const QString &metric) {
  if (!r.success) return qQNaN();
  return (metric == QStringLiteral("VBLOCK")) ? r.runout_vblock_mm : r.tir_axis_mm;
}

struct JudgeEvalState {
  bool any_checked = false;
  bool length_fail = false;
  bool geometry_fail = false;
  QStringList reasons;
};

void evaluateSpecItem(const QString &name,
                      double value,
                      const core::AlgorithmConfig::SpecValueConfig &spec,
                      bool isLengthItem,
                      JudgeEvalState *state) {
  if (!state) return;
  if (spec.tolerance_mm < 0.0) return;
  state->any_checked = true;
  if (!std::isfinite(value)) {
    state->reasons << QStringLiteral("%1 无有效结果").arg(name);
    if (isLengthItem) state->length_fail = true;
    else state->geometry_fail = true;
    return;
  }
  const double tol = std::fabs(spec.tolerance_mm);
  const double delta = std::fabs(value - spec.standard_mm);
  if (delta > tol) {
    state->reasons << QStringLiteral("%1 超差(值=%2, 标准=%3, 公差=±%4)")
                          .arg(name)
                          .arg(formatNumber(value))
                          .arg(formatNumber(spec.standard_mm))
                          .arg(formatNumber(tol));
    if (isLengthItem) state->length_fail = true;
    else state->geometry_fail = true;
  }
}

void finalizeSlotJudgement(core::ProductionSlotSummary *slot,
                           JudgeEvalState *state) {
  if (!slot || !state) return;
  if (!slot->compute.valid) {
    state->geometry_fail = true;
    if (!slot->compute.fail_reason_text.isEmpty()) {
      state->reasons.prepend(QStringLiteral("计算失败: %1").arg(slot->compute.fail_reason_text));
    } else {
      state->reasons.prepend(QStringLiteral("计算失败"));
    }
  }

  slot->judgement_known = true;
  slot->judgement_ok = slot->compute.valid && state->reasons.isEmpty();
  slot->fail_reason_text = slot->judgement_ok ? QString() : state->reasons.join(QStringLiteral("；"));

  if (slot->judgement_ok) {
    slot->compute.judgement = core::MeasurementJudgement::Ok;
    slot->compute.fail_class = core::MeasurementFailClass::None;
    slot->compute.fail_reason_text.clear();
  } else {
    slot->compute.judgement = core::MeasurementJudgement::Ng;
    if (state->length_fail && state->geometry_fail) {
      slot->compute.fail_class = core::MeasurementFailClass::Mixed;
    } else if (state->length_fail) {
      slot->compute.fail_class = core::MeasurementFailClass::Length;
    } else {
      slot->compute.fail_class = core::MeasurementFailClass::Geometry;
    }
    slot->compute.fail_reason_text = slot->fail_reason_text;
  }
}

QString slotTextByIndex(int slotIndex) {
  return (slotIndex >= 0) ? QString::number(slotIndex + 1) : QStringLiteral("-");
}

int countValidMask(const QVector<bool> &mask) {
  int n = 0;
  for (bool v : mask) {
    if (v) ++n;
  }
  return n;
}

Channel72Series buildChannel72Series(const QVector<float> &all, int offset, int invalidLimit) {
  Channel72Series s;
  s.values_mm.reserve(72);
  s.valid_mask.reserve(72);
  for (int i = 0; i < 72; ++i) {
    const int idx = offset + i;
    const double v = (idx >= 0 && idx < all.size()) ? static_cast<double>(all.at(idx)) : qQNaN();
    const bool valid = std::isfinite(v);
    s.values_mm.push_back(v);
    s.valid_mask.push_back(valid);
    if (!valid) ++s.invalid_points;
  }
  s.invalid_too_many = (s.invalid_points > invalidLimit);
  if (s.invalid_too_many) {
    for (int i = 0; i < s.valid_mask.size(); ++i) s.valid_mask[i] = false;
  }
  return s;
}

void applyChannelOffset(Channel72Series *s, double offset_mm) {
  if (!s || offset_mm == 0.0) return;
  for (int i = 0; i < s->values_mm.size(); ++i) {
    if (i < s->valid_mask.size() && s->valid_mask.at(i) && std::isfinite(s->values_mm.at(i))) {
      s->values_mm[i] += offset_mm;
    }
  }
}

core::DiameterAlgoParams buildDiameterAlgoParams(const core::AlgorithmConfig &cfg,
                                                 int minValidPoints,
                                                 double kInMm,
                                                 double kOutMm) {
  core::DiameterAlgoParams p;
  p.k_in_mm = kInMm;
  p.k_out_mm = kOutMm;
  p.use_explicit_k_out = cfg.a_use_explicit_k_out;
  p.probe_base_mm = cfg.a_probe_base_mm;
  p.angle_offset_deg = cfg.a_angle_offset_deg;
  p.inner_fit.residual_threshold_mm = cfg.a_residual_threshold_in_mm;
  p.outer_fit.residual_threshold_mm = cfg.a_residual_threshold_out_mm;
  p.inner_fit.min_valid_points = minValidPoints;
  p.outer_fit.min_valid_points = minValidPoints;
  p.harmonic.max_order = 8;
  p.harmonic.remove_mean = false;
  return p;
}

core::RunoutAlgoParams buildRunoutAlgoParams(const core::AlgorithmConfig &cfg,
                                             int minValidPoints,
                                             double kRunoutMm) {
  core::RunoutAlgoParams p;
  p.k_runout_mm = kRunoutMm;
  p.angle_offset_deg = cfg.b_angle_offset_deg;
  p.fit_options.residual_threshold_mm = cfg.b_residual_threshold_mm;
  p.fit_options.min_valid_points = minValidPoints;
  p.v_block_angle_deg = cfg.b_v_block_angle_deg;
  p.interpolation_factor = cfg.b_interpolation_factor;
  p.harmonic.max_order = 8;
  p.harmonic.remove_mean = false;
  return p;
}

} // namespace

MainWindow::MainWindow(const core::AppConfig &cfg, const QString &iniPath,
                       MesWorker *worker, QWidget *parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow), iniPath_(iniPath), appCfg_(cfg) {
  const QString idCheckCfg = appCfg_.mes.id_check_strategy.trimmed().toUpper();
  if (idCheckCfg == QStringLiteral("LOCAL_MOCK")) {
    idCheckStrategy_ = IdCheckStrategy::LocalMock;
  } else if (idCheckCfg == QStringLiteral("MES_STRICT")) {
    idCheckStrategy_ = IdCheckStrategy::MesStrict;
  } else {
    idCheckStrategy_ = IdCheckStrategy::Bypass;
  }
  ui_->setupUi(this);

  setMinimumSize(800, 500);
  setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

  if (centralWidget()) {
    centralWidget()->setMinimumSize(0, 0);
    centralWidget()->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
  }

  ui_->stackedWidget->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
  ui_->navList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

  setWindowTitle(QStringLiteral("工件自动测量装置"));

  ui_->navList->clear();
  ui_->navList->addItems(
      {QStringLiteral("生产"), QStringLiteral("标定"), QStringLiteral("数据"),
       QStringLiteral("MES上传"), QStringLiteral("设置"),
       QStringLiteral("报警/事件"), QStringLiteral("诊断"),
       QStringLiteral("RAW查看"), QStringLiteral("开发调试"),
       QStringLiteral("手动/维护"), QStringLiteral("报表(待做)"),
       QStringLiteral("用户权限(待做)")});
  ui_->navList->setIconSize(QSize(18,18));
  ui_->navList->setSpacing(6);
  ui_->navList->setStyleSheet(QStringLiteral("QListWidget{background:#f8fafc;border:1px solid #e5e7eb;border-radius:12px;padding:8px;} QListWidget::item{padding:10px 12px;border-radius:10px;font-weight:600;} QListWidget::item:selected{background:#dbeafe;color:#111827;} QListWidget::item:hover{background:#eff6ff;}"));
  const QList<QStyle::StandardPixmap> navIcons = {QStyle::SP_DesktopIcon, QStyle::SP_FileDialogDetailedView, QStyle::SP_DirIcon, QStyle::SP_DialogSaveButton, QStyle::SP_FileDialogContentsView, QStyle::SP_MessageBoxWarning, QStyle::SP_MessageBoxInformation, QStyle::SP_DriveHDIcon, QStyle::SP_ComputerIcon, QStyle::SP_ToolBarHorizontalExtensionButton, QStyle::SP_FileDialogListView, QStyle::SP_DialogOpenButton};
  for (int i = 0; i < ui_->navList->count() && i < navIcons.size(); ++i) { ui_->navList->item(i)->setIcon(style()->standardIcon(navIcons.at(i))); }

  productionWidget_ = new ProductionWidget(cfg, ui_->stackedWidget);
  calibrationWidget_ = new CalibrationWidget(cfg, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(productionWidget_);
  ui_->stackedWidget->addWidget(calibrationWidget_);
  ui_->stackedWidget->addWidget(new DataWidget(cfg, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(
      new MesUploadWidget(cfg, worker, ui_->stackedWidget));
  settingsWidget_ = new SettingsWidget(cfg, iniPath_, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(settingsWidget_);
  connect(settingsWidget_, &SettingsWidget::configApplied, this, [this](const core::AppConfig &newCfg){
    appCfg_ = newCfg;
    const QString idCheckCfg = appCfg_.mes.id_check_strategy.trimmed().toUpper();
    if (idCheckCfg == QStringLiteral("LOCAL_MOCK")) {
      idCheckStrategy_ = IdCheckStrategy::LocalMock;
    } else if (idCheckCfg == QStringLiteral("MES_STRICT")) {
      idCheckStrategy_ = IdCheckStrategy::MesStrict;
    } else {
      idCheckStrategy_ = IdCheckStrategy::Bypass;
    }
    if (idCheckStrategy_ == IdCheckStrategy::LocalMock) {
      mesExpectedPartIds_.clear();
    }
    appendProductionLog(QStringLiteral("设置已应用：算法参数已更新；ID核对策略=%1").arg(idCheckStrategyText()));
  });
  connect(settingsWidget_, &SettingsWidget::configSaved, this, [this](const QString &path){
    appendProductionLog(QStringLiteral("配置已保存：%1").arg(path));
  });
  ui_->stackedWidget->addWidget(new AlarmWidget(cfg, ui_->stackedWidget));
  diagnosticsWidget_ = new DiagnosticsWidget(cfg, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(diagnosticsWidget_);
  ui_->stackedWidget->addWidget(new RawViewerWidget(cfg, ui_->stackedWidget));
  devToolsWidget_ = new DevToolsWidget(cfg, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(devToolsWidget_);

  manualMaintainWidget_ = new ManualMaintainWidget(ui_->stackedWidget);
  ui_->stackedWidget->addWidget(manualMaintainWidget_);
  ui_->stackedWidget->addWidget(new TodoWidget(
      QStringLiteral("统计报表（TODO）"),
      QStringLiteral("后续将增加良率、趋势、SPC/CPK 等统计视图。"),
      ui_->stackedWidget));
  ui_->stackedWidget->addWidget(
      new TodoWidget(QStringLiteral("用户与权限（TODO）"),
                     QStringLiteral("后续将增加登录、角色权限控制与操作审计。"),
                     ui_->stackedWidget));

  connect(ui_->navList, &QListWidget::currentRowChanged, ui_->stackedWidget,
          &QStackedWidget::setCurrentIndex);

  ui_->navList->setCurrentRow(0);

  lbDb_ = new QLabel(QStringLiteral("数据库: 已连接"), this);
  lbPlc_ = new QLabel(QStringLiteral("PLC: 未启用"), this);
  lbMes_ = new QLabel(QStringLiteral("MES: -"), this);
  statusBar()->addPermanentWidget(lbDb_);
  statusBar()->addPermanentWidget(lbPlc_);
  statusBar()->addPermanentWidget(lbMes_);

  auto *actToggleFullScreen = new QAction(this);
  actToggleFullScreen->setShortcut(QKeySequence(Qt::Key_F11));
  addAction(actToggleFullScreen);
  connect(actToggleFullScreen, &QAction::triggered, this, [this] {
    if (isFullScreen()) {
      showNormal();
    } else {
      showFullScreen();
    }
  });

  auto *actExitFullScreen = new QAction(this);
  actExitFullScreen->setShortcut(QKeySequence(Qt::Key_Escape));
  addAction(actExitFullScreen);
  connect(actExitFullScreen, &QAction::triggered, this, [this] {
    if (isFullScreen()) {
      showNormal();
    }
  });

  setupPlcRuntime(cfg);
  setupDiagnosticsBindings();
  setupBusinessPageBindings();
  appendProductionLog(QStringLiteral("ID核对策略：%1").arg(idCheckStrategyText()));
  if (idCheckStrategy_ == IdCheckStrategy::LocalMock) {
    appendProductionLog(QStringLiteral("LOCAL_MOCK 文件：%1").arg(resolveIdCheckMockFilePath()));
  }
}

MainWindow::~MainWindow() {
  if (plcRuntime_) {
    plcRuntime_->stop();
  }
  delete ui_;
}

void MainWindow::setupPlcRuntime(const core::AppConfig &cfg) {
  plcRuntime_ = std::make_unique<core::PlcRuntimeServiceV2>(cfg, this);
  updatePlcStatusLabel();

  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::connectionChanged,
          this, [this](bool connected) {
            updatePlcStatusLabel();
            if (productionWidget_) productionWidget_->setPlcConnected(connected);
            if (calibrationWidget_) calibrationWidget_->setPlcConnected(connected);
            if (!connected) {
              awaitingCmdReply_ = false;
              pendingCmdBits_ = 0;
              hasLastMailboxSnapshot_ = false;
              lastMailboxSnapshot_.reset();
              hasLastTray_ = false;
              calibrationFlowExpected_ = false;
              calibrationAutoState_ = CalibrationAutoState::Idle;
            }
            if (manualMaintainWidget_) {
              const bool calCtx = hasLastStatus_
                                  && calibrationFlowExpected_
                                  && isCalibrationStepCode(lastStatus_.step_state);
              manualMaintainWidget_->setRuntimeSummary(connected,
                                                       hasLastStatus_ ? machineStateText(lastStatus_.machine_state) : QStringLiteral("-"),
                                                       hasLastStatus_ ? stepStateText(lastStatus_.step_state, lastStatus_.scan_done, calCtx) : QStringLiteral("-"));
            }
            if (!lastPlcConnectedKnown_ || lastPlcConnected_ != connected) {
              appendProductionLog(connected ? QStringLiteral("PLC 连接成功") : QStringLiteral("PLC 已断开"));
              lastPlcConnectedKnown_ = true;
              lastPlcConnected_ = connected;
              reconnectAttemptLogged_ = false;
            }
          });
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::errorOccurred,
          this, &MainWindow::handlePlcRuntimeError);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::statsUpdated,
          this, &MainWindow::onPlcStatsUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::statusUpdated,
          this, &MainWindow::onPlcStatusUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::commandUpdated,
          this, &MainWindow::onPlcCommandUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::trayUpdated,
          this, &MainWindow::onPlcTrayUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::mailboxSnapshotUpdated,
          this, &MainWindow::onPlcMailboxSnapshotUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::plcEventsRaised,
          this, &MainWindow::onPlcEventsRaised);

  if (!cfg.plc.enabled) {
    return;
  }

  appendProductionLog(QStringLiteral("正在连接 PLC..."));
  QString err;
  if (!plcRuntime_->start(&err)) {
    handlePlcRuntimeError(err);
    updatePlcStatusLabel();
    return;
  }

  if (!plcRuntime_->connectNow(&err)) {
    handlePlcRuntimeError(err);
  }
  updatePlcStatusLabel();
}

void MainWindow::setupDiagnosticsBindings() {
  if (!plcRuntime_) {
    return;
  }

  if (diagnosticsWidget_) {
    connect(diagnosticsWidget_, &DiagnosticsWidget::requestRefresh,
            plcRuntime_.get(), &core::PlcRuntimeServiceV2::pollOnce);
    connect(diagnosticsWidget_, &DiagnosticsWidget::requestReadMailbox,
            this, [this] { handleReadMailboxRequested(productionWidget_ ? productionWidget_->selectedPartTypeText().trimmed().isEmpty() ? QChar('A') : productionWidget_->selectedPartTypeText().at(0) : QChar('A')); });
    connect(diagnosticsWidget_, &DiagnosticsWidget::requestAckMailbox,
            this, [this] { handleAckMailboxRequested(false); });
  }

  if (manualMaintainWidget_) {
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestSetPlcMode, this, [this](int mode) {
      QString err;
      if (!plcRuntime_->writePlcMode(static_cast<qint16>(mode), &err)) { handlePlcRuntimeError(err); return; }
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV26(mode)));
      plcRuntime_->pollOnce();
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestPlcReloadSlotIds,
            this, [this] {
              core::PlcTrayPartIdBlockV2 tray; QString err;
              if (!plcRuntime_->readSecondStageTrayIds(&tray, &err)) { handlePlcRuntimeError(err); return; }
              if (manualMaintainWidget_) {
                manualMaintainWidget_->appendLog(QStringLiteral("读取扫码ID成功："));
                for (int i = 0; i < core::kLogicalSlotCount; ++i) {
                  const QString id = tray.part_ids[i].trimmed().isEmpty() ? QStringLiteral("NG") : tray.part_ids[i].trimmed();
                  manualMaintainWidget_->appendLog(QStringLiteral("  槽位%1: %2").arg(i + 1).arg(id));
                }
              }
            });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestPlcReadMailbox,
            this, [this] { handleReadMailboxRequested(productionWidget_ ? productionWidget_->selectedPartTypeText().at(0) : QChar('A')); });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestPlcAckMailbox,
            this, [this] { handleAckMailboxRequested(false); });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestPlcContinueAfterIdCheck,
            this, [this] {
              QString err;
              if (!plcRuntime_->writeScanDone(0, &err)) { handlePlcRuntimeError(err); return; }
              if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 scan_done=0（ID核对通过）"));
              plcRuntime_->pollOnce();
            });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestPlcNamedCommand, this, &MainWindow::handleUiCommandRequested);
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestAxisCommand, this, [this](int axisIndex, const QString &action) {
      if (!plcRuntime_) return;
      QString err;
      if (action == QStringLiteral("ENABLE_ON")) {
        if (!plcRuntime_->axisSetEnable(axisIndex, true, &err)) { handlePlcRuntimeError(err); return; }
      } else if (action == QStringLiteral("ENABLE_OFF")) {
        if (!plcRuntime_->axisSetEnable(axisIndex, false, &err)) { handlePlcRuntimeError(err); return; }
      } else {
        if (!plcRuntime_->axisPulseAction(axisIndex, action, &err)) { handlePlcRuntimeError(err); return; }
      }
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写轴控制：%1 action=%2").arg(axisNameByIndex(axisIndex), action));
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestAxisJog, this, [this](int axisIndex, const QString &direction, bool active) {
      if (!plcRuntime_) return;
      QString err;
      const bool forward = (direction != QStringLiteral("JOG_BWD"));
      if (!plcRuntime_->axisJog(axisIndex, forward, active, &err)) { handlePlcRuntimeError(err); }
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestAxisMove, this, [this](int axisIndex, const QString &action, double acc, double dec, double pos, double vel) {
      if (!plcRuntime_) return;
      QString err;
      const bool relative = (action == QStringLiteral("MOVE_REL"));
      if (!plcRuntime_->axisMove(axisIndex, relative, acc, dec, pos, vel, &err)) { handlePlcRuntimeError(err); return; }
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写轴运动：%1 action=%2 acc=%3 dec=%4 pos=%5 vel=%6").arg(axisNameByIndex(axisIndex)).arg(action).arg(acc,0,'f',3).arg(dec,0,'f',3).arg(pos,0,'f',3).arg(vel,0,'f',3));
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestCylinderCommand, this, [this](const QString &group, int index, const QString &action) {
      if (!plcRuntime_) return;
      QString err;
      if (!plcRuntime_->cylinderAction(group, index, action, &err)) { handlePlcRuntimeError(err); return; }
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写气缸控制：%1 action=%2").arg(core::plc_v26::cylinderName(group, index), action));
    });
  }
}

void MainWindow::setupBusinessPageBindings() {
  if (!plcRuntime_) {
    return;
  }

  if (productionWidget_) {
    connect(productionWidget_, &ProductionWidget::requestReadMailbox,
            this, [this] { handleReadMailboxRequested(productionWidget_ ? productionWidget_->selectedPartTypeText().at(0) : QChar('A'), false); });
    connect(productionWidget_, &ProductionWidget::requestReloadSlotIds,
            this, [this] {
              core::PlcTrayPartIdBlockV2 tray; QString err;
              if (!plcRuntime_->readSecondStageTrayIds(&tray, &err)) { handlePlcRuntimeError(err); return; }
              onPlcTrayUpdated(tray);
              const quint16 presentMask = hasLastStatus_ ? lastStatus_.tray_present_mask : 0xFFFFu;
              productionWidget_->appendPlcLogMessage(QStringLiteral("读取扫码ID成功："));
              for (int i = 0; i < core::kLogicalSlotCount; ++i) {
                const bool present = ((presentMask >> i) & 0x1u) != 0;
                if (!present) continue;
                const QString id = tray.part_ids[i].trimmed().isEmpty()
                                       ? QStringLiteral("NG")
                                       : tray.part_ids[i].trimmed();
                productionWidget_->appendPlcLogMessage(QStringLiteral("  槽位%1：%2").arg(i + 1).arg(id));
              }
            });
    connect(productionWidget_, &ProductionWidget::requestAckMailbox,
            this, [this] { handleAckMailboxRequested(false); });
    connect(productionWidget_, &ProductionWidget::requestContinueAfterIdCheck,
            this, [this]{ handleUiCommandRequested(QStringLiteral("CONTINUE_AFTER_ID_CHECK"), {}); });
    connect(productionWidget_, &ProductionWidget::requestSetPlcMode,
            this, [this](int mode){ if (!plcRuntime_) return; QString err; if (!plcRuntime_->writePlcMode(static_cast<qint16>(mode), &err)) { handlePlcRuntimeError(err); return; } appendProductionLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV26(mode))); plcRuntime_->pollOnce(); });
    connect(productionWidget_, &ProductionWidget::requestWriteCategoryMode,
            this, [this](int partTypeArg){ if (!plcRuntime_) return; QString err; if (!plcRuntime_->setCategoryMode(static_cast<qint16>(partTypeArg), &err)) { handlePlcRuntimeError(err); return; } appendProductionLog(QStringLiteral("写工件类型到 PLC：%1").arg(partTypeArg == 1 ? QStringLiteral("B型") : QStringLiteral("A型"))); });
    connect(productionWidget_, &ProductionWidget::requestWriteSlotId,
            this, &MainWindow::handleWriteTrayPartIdRequested);
    connect(productionWidget_, &ProductionWidget::uiCommandRequested,
            this, &MainWindow::handleUiCommandRequested);
    connect(productionWidget_, &ProductionWidget::requestReconnectPlc,
            this, [this]{ attemptReconnectPlc(true); });
  }

  if (calibrationWidget_) {
    connect(calibrationWidget_, &CalibrationWidget::requestReconnectPlc,
            this, [this]{
              if (!plcRuntime_) return;
              appendCalibrationLog(QStringLiteral("手动重连 PLC..."));
              QString err;
              plcRuntime_->disconnectNow();
              if (!plcRuntime_->connectNow(&err)) {
                handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("PLC 重连失败") : err);
                return;
              }
              reconnectAttemptLogged_ = false;
              updatePlcStatusLabel();
              plcRuntime_->pollOnce();
            });
    connect(calibrationWidget_, &CalibrationWidget::requestSetPlcMode,
            this, [this](int mode){
              if (!plcRuntime_) return;
              QString err;
              if (!plcRuntime_->writePlcMode(static_cast<qint16>(mode), &err)) {
                handlePlcRuntimeError(err);
                return;
              }
              appendCalibrationLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV26(mode)));
              plcRuntime_->pollOnce();
            });
    connect(calibrationWidget_, &CalibrationWidget::requestWriteCategoryMode,
            this, [this](int partTypeArg){
              if (!plcRuntime_) return;
              QString err;
              if (!plcRuntime_->setCategoryMode(static_cast<qint16>(partTypeArg), &err)) {
                handlePlcRuntimeError(err);
                return;
              }
              appendCalibrationLog(QStringLiteral("写工件类型到 PLC：%1")
                                       .arg(partTypeArg == 1 ? QStringLiteral("B型") : QStringLiteral("A型")));
            });
    connect(calibrationWidget_, &CalibrationWidget::requestReadMailbox,
            this, [this] { handleReadMailboxRequested(calibrationWidget_ ? calibrationWidget_->selectedPartTypeText().at(0) : QChar('A'), true); });
    connect(calibrationWidget_, &CalibrationWidget::requestAckMailbox,
            this, [this] { handleAckMailboxRequested(true); });
    connect(calibrationWidget_, &CalibrationWidget::uiCommandRequested,
            this, &MainWindow::handleUiCommandRequested);
  }
}


void MainWindow::appendProductionLog(const QString &text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) return;
  if (productionWidget_) productionWidget_->appendPlcLogMessage(trimmed);
}

void MainWindow::appendCalibrationLog(const QString &text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) return;
  if (calibrationWidget_) calibrationWidget_->appendLogMessage(trimmed);
}

void MainWindow::attemptReconnectPlc(bool manual) {
  if (!plcRuntime_) return;
  if (manual) appendProductionLog(QStringLiteral("手动重连 PLC..."));
  QString err;
  plcRuntime_->disconnectNow();
  if (!plcRuntime_->connectNow(&err)) {
    handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("PLC 重连失败") : err);
    return;
  }
  reconnectAttemptLogged_ = false;
  updatePlcStatusLabel();
  plcRuntime_->pollOnce();
}

void MainWindow::updatePlcStatusLabel() {
  if (!lbPlc_) {
    return;
  }
  if (!plcRuntime_) {
    lbPlc_->setText(QStringLiteral("PLC: -"));
    return;
  }
  const auto &cfg = plcRuntime_->config().plc;
  if (!cfg.enabled) {
    lbPlc_->setText(QStringLiteral("PLC: 未启用"));
    return;
  }
  const QString mode = QStringLiteral("Real");
  const QString conn = plcRuntime_->isConnected() ? QStringLiteral("已连接") : QStringLiteral("未连接");
  lbPlc_->setText(QStringLiteral("PLC: %1 / %2").arg(mode, conn));
}

void MainWindow::handlePlcRuntimeError(const QString &message) {
  if (!message.trimmed().isEmpty()) {
    statusBar()->showMessage(QStringLiteral("PLC: %1").arg(message), 5000);
    const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
    if (calibrationContext) appendCalibrationLog(message);
    else appendProductionLog(message);
  }
}

void MainWindow::onPlcStatsUpdated(const core::PlcRuntimeStatsV2 &stats) {
  updatePlcStatusLabel();
  if (productionWidget_) {
    productionWidget_->setPlcConnected(stats.connected);
    if (hasLastStatus_) productionWidget_->setCurrentPlcMode(lastStatus_.control_mode);
  }
  if (calibrationWidget_) {
    calibrationWidget_->setPlcConnected(stats.connected);
    if (hasLastStatus_) calibrationWidget_->setCurrentPlcMode(lastStatus_.control_mode);
  }
  if (diagnosticsWidget_) {
    const int pollHz = (stats.poll_interval_ms > 0) ? (1000 / stats.poll_interval_ms) : 0;
    diagnosticsWidget_->setCommStats(pollHz, stats.last_poll_ms,
                                     stats.poll_ok_count, stats.poll_error_count);
  }
  if (!stats.connected && plcRuntime_ && plcRuntime_->config().plc.auto_reconnect && !reconnectAttemptLogged_) {
    const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
    if (calibrationContext) appendCalibrationLog(QStringLiteral("尝试重新连接 PLC..."));
    else appendProductionLog(QStringLiteral("尝试重新连接 PLC..."));
    reconnectAttemptLogged_ = true;
  }
  if (manualMaintainWidget_) {
    const bool calCtx = hasLastStatus_
                        && calibrationFlowExpected_
                        && isCalibrationStepCode(lastStatus_.step_state);
    manualMaintainWidget_->setRuntimeSummary(stats.connected,
                                             hasLastStatus_ ? machineStateText(lastStatus_.machine_state) : QStringLiteral("-"),
                                             hasLastStatus_ ? stepStateText(lastStatus_.step_state, lastStatus_.scan_done, calCtx) : QStringLiteral("-"));
    if (hasLastStatus_) manualMaintainWidget_->setCurrentPlcMode(lastStatus_.control_mode);
  }
}

void MainWindow::onPlcStatusUpdated(const core::PlcStatusBlockV2 &status) {
  const bool hadPrev = hasLastStatus_;
  const core::PlcStatusBlockV2 prev = lastStatus_;
  lastStatus_ = status;
  hasLastStatus_ = true;
  const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, status.step_state);
  auto appendFlowLog = [this, calibrationContext](const QString &line) {
    if (calibrationContext) appendCalibrationLog(line);
    else appendProductionLog(line);
  };
  updateCalibrationAutoState(status.step_state);
  if (manualMaintainWidget_) {
    manualMaintainWidget_->setRuntimeSummary(plcRuntime_ ? plcRuntime_->isConnected() : false,
                                             machineStateText(status.machine_state),
                                             stepStateText(status.step_state, status.scan_done, calibrationContext));
    manualMaintainWidget_->setCurrentPlcMode(status.control_mode);
    refreshManualMaintainLiveStatus();
  }
  if (diagnosticsWidget_) {
    diagnosticsWidget_->setStatusFields(static_cast<int>(status.step_state),
                                        static_cast<int>(status.machine_state),
                                        static_cast<int>(status.alarm_code),
                                        0,
                                        static_cast<quint32>(status.interlock_mask),
                                        0);
  }

  if (hadPrev && prev.step_state != status.step_state) {
    appendFlowLog(QStringLiteral("流程步骤变化：%1 -> %2 (code=%3)")
                      .arg(stepStateText(prev.step_state, prev.scan_done, calibrationContext))
                      .arg(stepStateText(status.step_state, status.scan_done, calibrationContext))
                      .arg(status.step_state));
  }
  if (hadPrev && prev.interlock_mask != status.interlock_mask) {
    if (status.interlock_mask == 0) {
      appendFlowLog(QStringLiteral("互锁位图恢复正常"));
    } else {
      appendFlowLog(QStringLiteral("互锁位图触发：0x%1 (%2)")
                        .arg(QString::number(status.interlock_mask, 16).toUpper())
                        .arg(interlockMaskText(status.interlock_mask)));
    }
  }

  if (productionWidget_ && !calibrationContext) {
    productionWidget_->setMachineState(status.machine_state, machineStateText(status.machine_state));
    productionWidget_->setStateSeq(0);
    productionWidget_->setAlarm(status.alarm_code, 0);
    productionWidget_->setInterlockMask(status.interlock_mask);
    productionWidget_->setCurrentPlcMode(status.control_mode);
    productionWidget_->applyPlcRuntimeSnapshot(status.step_state,
                                               status.scan_done,
                                               status.tray_present_mask,
                                               status.active_slot_mask,
                                               calibrationContext,
                                               status.mailbox_ready);
  }

  if (calibrationWidget_) {
    calibrationWidget_->setStepState(status.step_state);
    calibrationWidget_->setAlarm(status.alarm_code, 0);
    calibrationWidget_->setCurrentPlcMode(status.control_mode);
    calibrationWidget_->setTrayPresentMask(status.tray_present_mask);
    // 标定页日志仅记录标定上下文内的 mailbox 变化，避免生产流程日志串入标定页。
    if (calibrationContext && status.mailbox_ready != lastMailboxReady_) {
      calibrationWidget_->setMailboxReady(status.mailbox_ready != 0);
    }
    lastMailboxReady_ = status.mailbox_ready;
  }
}

void MainWindow::onPlcCommandUpdated(const core::PlcCommandBlockV2 &command) {
  if (command.category_mode == core::plc_v26::kPartTypeA ||
      command.category_mode == core::plc_v26::kPartTypeB) {
    lastCategoryMode_ = command.category_mode;
  }
  const bool unchanged = hasLastCommandSample_
                      && lastCmdResult_ == command.cmd_result
                      && lastRejectInstruction_ == command.cmd_error_code;
  if (unchanged && !awaitingCmdReply_) {
    return;
  }
  hasLastCommandSample_ = true;
  lastCmdResult_ = command.cmd_result;
  lastRejectInstruction_ = command.cmd_error_code;

  if (!awaitingCmdReply_) {
    return;
  }
  if (command.cmd_result == 0) {
    return;
  }

  const QString resultHex = QStringLiteral("0x%1").arg(QString::number(command.cmd_result, 16).toUpper());
  const QString pendingHex = QStringLiteral("0x%1").arg(QString::number(pendingCmdBits_, 16).toUpper());
  const bool rejected = (command.cmd_result & core::plc_v26::kCmdRejectMask) != 0;

  QString line;
  if (rejected) {
    line = QStringLiteral("PLC 命令回复：拒绝 result=%1 pending=%2 reject=0x%3 reason=%4")
               .arg(resultHex)
               .arg(pendingHex)
               .arg(QString::number(command.cmd_error_code, 16).toUpper())
               .arg(rejectInstructionText(command.cmd_error_code));
  } else if ((command.cmd_result & pendingCmdBits_) == pendingCmdBits_) {
    line = QStringLiteral("PLC 命令回复：接收成功 result=%1 (%2)")
               .arg(resultHex)
               .arg(cmdBitsText(command.cmd_result));
  } else {
    return;
  }

  const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0)
                               || calibrationFlowExpected_;
  if (calibrationContext) appendCalibrationLog(line);
  else appendProductionLog(line);
  if (manualMaintainWidget_) manualMaintainWidget_->appendLog(line);
  awaitingCmdReply_ = false;
  pendingCmdBits_ = 0;
}


void MainWindow::refreshManualMaintainLiveStatus() {
  if (!manualMaintainWidget_ || !plcRuntime_ || !plcRuntime_->isConnected()) return;
  QString err;
  QStringList axisLines;
  for (int axisIndex = 0; axisIndex < 10; ++axisIndex) {
    core::PlcAxisStateV26 s;
    if (!plcRuntime_->readAxisState(axisIndex, &s, &err)) { handlePlcRuntimeError(err); break; }
    axisLines << QStringLiteral("%1 | En=%2 Homed=%3 Err=%4 Busy=%5 Done=%6 ErrId=%7 Pos=%8 Vel=%9")
                     .arg(s.axis_name)
                     .arg(s.enabled ? 1 : 0)
                     .arg(s.homed ? 1 : 0)
                     .arg(s.error ? 1 : 0)
                     .arg(s.busy ? 1 : 0)
                     .arg(s.done ? 1 : 0)
                     .arg(s.error_id)
                     .arg(s.act_position, 0, 'f', 3)
                     .arg(s.act_velocity, 0, 'f', 3);
  }
  manualMaintainWidget_->setAxisStatesText(axisLines.join(QStringLiteral("\n")));

  QStringList cylLines;
  auto appendCyl = [&](const QString &group, int index) -> bool {
    core::PlcCylinderStateV26 s;
    if (!plcRuntime_->readCylinderState(group, index, &s, &err)) return false;
    cylLines << QStringLiteral("%1 | P=%2 N=%3 Err=%4 ErrId=%5")
                    .arg(s.name)
                    .arg(s.p ? 1 : 0)
                    .arg(s.n ? 1 : 0)
                    .arg(s.error ? 1 : 0)
                    .arg(s.error_id);
    return true;
  };
  if (!appendCyl(QStringLiteral("LM"), 0)) { handlePlcRuntimeError(err); return; }
  for (int i = 0; i < 3; ++i) if (!appendCyl(QStringLiteral("CL"), i)) { handlePlcRuntimeError(err); return; }
  for (int i = 0; i < 4; ++i) if (!appendCyl(QStringLiteral("GT2"), i)) { handlePlcRuntimeError(err); return; }
  manualMaintainWidget_->setCylinderStatesText(cylLines.join(QStringLiteral("\n")));
}

void MainWindow::onPlcTrayUpdated(const core::PlcTrayPartIdBlockV2 &tray) {
  lastTray_ = tray;
  hasLastTray_ = true;
  const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  if (productionWidget_ && !calibrationContext) {
    QVector<QString> slotIds(core::kLogicalSlotCount);
    const quint16 presentMask = hasLastStatus_ ? lastStatus_.tray_present_mask : 0xFFFFu;
    for (int i = 0; i < core::kLogicalSlotCount; ++i) {
      const bool present = ((presentMask >> i) & 0x1u) != 0;
      if (!present) continue; // 无料槽位不写入
      const QString id = tray.part_ids[i].trimmed();
      // 有料槽位必须落位：空值按 NG 显示，便于人工修正
      slotIds[i] = id.isEmpty() ? QStringLiteral("NG") : id;
    }
    productionWidget_->setScannedPartIds(slotIds);
  }
}

void MainWindow::onPlcMailboxSnapshotUpdated(const core::PlcMailboxSnapshot &snapshot) {
  if (!lastMailboxSnapshot_) lastMailboxSnapshot_ = std::make_unique<core::PlcMailboxSnapshot>();
  *lastMailboxSnapshot_ = snapshot;
  hasLastMailboxSnapshot_ = true;
  const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);

  QString slot0 = QStringLiteral("-");
  QString slot1 = QStringLiteral("-");
  QString partId0;
  QString partId1;

  if (!snapshot.items.isEmpty()) {
    slot0 = snapshot.items.at(0).slot_index >= 0 ? QString::number(snapshot.items.at(0).slot_index + 1) : QStringLiteral("-");
    partId0 = snapshot.items.at(0).part_id;
  }
  if (snapshot.items.size() > 1) {
    slot1 = snapshot.items.at(1).slot_index >= 0 ? QString::number(snapshot.items.at(1).slot_index + 1) : QStringLiteral("-");
    partId1 = snapshot.items.at(1).part_id;
  }

  if (diagnosticsWidget_) {
    diagnosticsWidget_->setMailboxPreview(mailboxPartTypeText(snapshot), slot0, slot1, partId0, partId1);
  }

  if (productionWidget_ && !calibrationContext) {
    productionWidget_->setMeasureDone(true);
  }

  if (calibrationWidget_ && calibrationContext) {
    for (const auto &item : snapshot.items) {
      if (item.slot_index == core::kCalibrationSlotIndex) {
        core::CalibrationSlotSummary s;
        s.slot_index = item.slot_index;
        s.calibration_type = QString(snapshot.part_type.toUpper());
        s.calibration_master_part_id = calibrationWidget_->masterPartIdForType(snapshot.part_type);
        s.measured_part_id = item.part_id;
        s.valid = false;
        calibrationWidget_->setSlotSummary(s);
        break;
      }
    }
  }

  if (productionWidget_ && !calibrationContext) {
    productionWidget_->appendPlcLogMessage(QStringLiteral("Mailbox 已解析：part=%1 slot0=%2 slot1=%3")
                                               .arg(mailboxPartTypeText(snapshot), slot0, slot1));
  }

  if (calibrationContext) {
    const bool singleItem = (snapshot.item_count == 1 && snapshot.items.size() == 1);
    const bool slot16Only = singleItem && snapshot.items.at(0).slot_index == core::kCalibrationSlotIndex;
    if (!slot16Only) {
      appendCalibrationLog(QStringLiteral("标定邮箱规则异常：标定流程仅允许槽位16单件数据（item_count=%1, items=%2）")
                               .arg(snapshot.item_count)
                               .arg(snapshot.items.size()));
      if (!snapshot.items.isEmpty()) {
        appendCalibrationLog(QStringLiteral("标定邮箱异常详情：slot=%1 part_id=%2")
                                 .arg(snapshot.items.first().slot_index + 1)
                                 .arg(snapshot.items.first().part_id));
      }
    }
  }
}

void MainWindow::onPlcEventsRaised(const core::PlcPollEventsV26 &events) {
  const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  if (events.scan_ready) {
    if (calibrationContext) {
      appendCalibrationLog(QStringLiteral("检测到扫码事件（标定流程不参与扫码核对）"));
    } else {
      appendProductionLog(QStringLiteral("检测到新扫码结果"));
      processAutoScanIdCheck();
    }
  }
  if (events.new_mailbox) {
    if (calibrationContext) {
      appendCalibrationLog(QStringLiteral("检测到新测量包，按标定流程处理"));
    } else {
      appendProductionLog(QStringLiteral("检测到新测量包，等待业务处理/ACK"));
    }
    processAutoMailboxFlow();
  }
}

void MainWindow::updateCalibrationAutoState(quint16 stepState) {
  if (!calibrationFlowExpected_) {
    calibrationAutoState_ = CalibrationAutoState::Idle;
    return;
  }
  CalibrationAutoState next = calibrationAutoState_;
  switch (stepState) {
  case plc_step_rules_v26::kStepCalPickFromRack:
  case plc_step_rules_v26::kStepCalLoadMeasureStation:
    next = CalibrationAutoState::WaitLoadSlot16;
    break;
  case plc_step_rules_v26::kStepCalMeasureA:
  case plc_step_rules_v26::kStepCalMeasureB:
  case plc_step_rules_v26::kStepCalMeasureLength:
    next = CalibrationAutoState::Measuring;
    break;
  case plc_step_rules_v26::kStepCalArchiveWaitDecisionA:
  case plc_step_rules_v26::kStepCalArchiveWaitDecisionB:
    next = CalibrationAutoState::WaitPcRead;
    break;
  case plc_step_rules_v26::kStepCalReturnToRack:
    next = CalibrationAutoState::Completed;
    break;
  default:
    if (!isCalibrationStepCode(stepState)) {
      next = CalibrationAutoState::Idle;
      calibrationFlowExpected_ = false;
    }
    break;
  }
  if (next == calibrationAutoState_) return;
  calibrationAutoState_ = next;
  QString text;
  switch (calibrationAutoState_) {
  case CalibrationAutoState::Idle: text = QStringLiteral("空闲"); break;
  case CalibrationAutoState::WaitLoadSlot16: text = QStringLiteral("等待槽16上料"); break;
  case CalibrationAutoState::WaitPcConfirm: text = QStringLiteral("等待PC确认"); break;
  case CalibrationAutoState::Measuring: text = QStringLiteral("标定测量中"); break;
  case CalibrationAutoState::WaitPcRead: text = QStringLiteral("等待PC读取"); break;
  case CalibrationAutoState::Completed: text = QStringLiteral("标定完成"); break;
  }
  appendCalibrationLog(QStringLiteral("标定状态机切换：%1").arg(text));
}

qint16 MainWindow::currentCategoryModeForAutoFlow() const {
  if (lastCategoryMode_ == core::plc_v26::kPartTypeA ||
      lastCategoryMode_ == core::plc_v26::kPartTypeB) {
    return lastCategoryMode_;
  }
  if (productionWidget_) {
    const quint32 arg = productionWidget_->selectedPartTypeArg();
    if (arg == core::plc_v26::kPartTypeA || arg == core::plc_v26::kPartTypeB) {
      return static_cast<qint16>(arg);
    }
  }
  return core::plc_v26::kPartTypeA;
}

QString MainWindow::idCheckStrategyText() const {
  switch (idCheckStrategy_) {
  case IdCheckStrategy::LocalMock:
    return QStringLiteral("LOCAL_MOCK");
  case IdCheckStrategy::MesStrict:
    return QStringLiteral("MES_STRICT");
  case IdCheckStrategy::Bypass:
  default:
    return QStringLiteral("BYPASS");
  }
}

QString MainWindow::resolveIdCheckMockFilePath() const {
  const QString raw = appCfg_.mes.id_check_mock_file.trimmed();
  if (raw.isEmpty()) {
    return QFileInfo(QFileInfo(iniPath_).absolutePath(),
                     QStringLiteral("mes_id_mock.json")).absoluteFilePath();
  }
  QFileInfo fi(raw);
  if (fi.isAbsolute()) return fi.absoluteFilePath();
  return QFileInfo(QFileInfo(iniPath_).absolutePath(), raw).absoluteFilePath();
}

bool MainWindow::loadMockExpectedPartIds(QVector<QString> *out, QString *err) const {
  if (!out) {
    if (err) *err = QStringLiteral("out 不能为空");
    return false;
  }
  out->fill(QString(), core::kLogicalSlotCount);
  const QString path = resolveIdCheckMockFilePath();
  QFile f(path);
  if (!f.exists()) {
    if (err) *err = QStringLiteral("LOCAL_MOCK 文件不存在：%1").arg(path);
    return false;
  }
  if (!f.open(QIODevice::ReadOnly)) {
    if (err) *err = QStringLiteral("LOCAL_MOCK 文件打开失败：%1").arg(path);
    return false;
  }
  const QByteArray bytes = f.readAll();
  f.close();

  QJsonParseError parseErr;
  const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseErr);
  if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
    if (err) *err = QStringLiteral("LOCAL_MOCK JSON解析失败：%1 (line=%2)")
                        .arg(parseErr.errorString())
                        .arg(parseErr.offset);
    return false;
  }
  const QJsonObject obj = doc.object();

  auto applyPair = [&](int slot1Based, const QString &id) {
    if (slot1Based < 1 || slot1Based > core::kLogicalSlotCount) return;
    (*out)[slot1Based - 1] = id.trimmed();
  };

  const QJsonValue slotIdsVal = obj.value(QStringLiteral("slot_ids"));
  if (slotIdsVal.isObject()) {
    const QJsonObject m = slotIdsVal.toObject();
    for (int slot = 1; slot <= core::kLogicalSlotCount; ++slot) {
      const QString key = QString::number(slot);
      if (!m.contains(key)) continue;
      applyPair(slot, m.value(key).toString());
    }
  }

  const QJsonValue slotsVal = obj.value(QStringLiteral("slots"));
  if (slotsVal.isArray()) {
    const QJsonArray arr = slotsVal.toArray();
    for (int i = 0; i < arr.size() && i < core::kLogicalSlotCount; ++i) {
      const QJsonValue v = arr.at(i);
      if (v.isString()) {
        applyPair(i + 1, v.toString());
        continue;
      }
      if (v.isObject()) {
        const QJsonObject one = v.toObject();
        const int slot = one.value(QStringLiteral("slot")).toInt(i + 1);
        const QString id = one.value(QStringLiteral("part_id")).toString(
            one.value(QStringLiteral("partId")).toString(
                one.value(QStringLiteral("id")).toString()));
        applyPair(slot, id);
      }
    }
  }

  return true;
}

bool MainWindow::evaluateIdCheckAgainstMes(QStringList *mismatchDetails,
                                           QVector<int> *mismatchSlots) const {
  if (!mismatchDetails || !mismatchSlots) return false;
  mismatchDetails->clear();
  mismatchSlots->clear();
  if (!hasLastStatus_ || !hasLastTray_) return false;
  if (mesExpectedPartIds_.size() != core::kLogicalSlotCount) return false;

  const quint16 presentMask = lastStatus_.tray_present_mask;
  for (int slot = 0; slot < core::kLogicalSlotCount; ++slot) {
    const bool present = ((presentMask >> slot) & 0x1u) != 0;
    if (!present) continue;
    const QString expected = mesExpectedPartIds_.at(slot).trimmed();
    if (expected.isEmpty()) continue;
    QString actual = lastTray_.part_ids[slot].trimmed();
    if (actual.isEmpty()) actual = QStringLiteral("NG");
    if (actual != expected) {
      mismatchSlots->push_back(slot);
      mismatchDetails->push_back(
          QStringLiteral("槽位%1：扫描ID=%2，MES期望=%3")
              .arg(slot + 1)
              .arg(actual)
              .arg(expected));
    }
  }
  return mismatchSlots->isEmpty();
}

bool MainWindow::tryAutoContinueAfterIdCheck() {
  if (!plcRuntime_) return false;
  QString err;
  if (!plcRuntime_->writeScanDone(0, &err)) {
    handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("写 scan_done=0 失败") : err);
    return false;
  }
  appendProductionLog(QStringLiteral("写 scan_done=0（ID核对通过）"));
  if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 scan_done=0（ID核对通过）"));
  plcRuntime_->pollOnce();
  return true;
}

bool MainWindow::tryAutoWritePcAck() {
  if (!plcRuntime_) return false;
  QString err;
  if (!plcRuntime_->sendPcAck(core::plc_v26::kJudgeOk, &err)) {
    handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("写 iPc_Ack 失败") : err);
    return false;
  }
  appendProductionLog(QStringLiteral("写 iPc_Ack=1"));
  return true;
}

void MainWindow::promptNgDecisionAndDispatch() {
  if (!plcRuntime_) return;
  QMessageBox box(QMessageBox::Question,
                  QStringLiteral("NG处理决策"),
                  QStringLiteral("本次判定为 NG。\n请选择后续处理方式："),
                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                  this);
  box.setDefaultButton(QMessageBox::Yes);
  if (auto *btn = box.button(QMessageBox::Yes)) btn->setText(QStringLiteral("当前件复测"));
  if (auto *btn = box.button(QMessageBox::No)) btn->setText(QStringLiteral("继续（不复测）"));
  if (auto *btn = box.button(QMessageBox::Cancel)) btn->setText(QStringLiteral("取消"));
  box.exec();
  const auto decide = box.standardButton(box.clickedButton());
  if (decide == QMessageBox::Cancel) {
    appendProductionLog(QStringLiteral("NG自动分支：用户取消决策，保持当前步骤等待人工处理"));
    return;
  }
  QVariantMap args;
  const qint16 plcMode = (hasLastStatus_
                       && lastStatus_.control_mode >= core::plc_v26::kModeManual
                       && lastStatus_.control_mode <= core::plc_v26::kModeSingleStep)
                          ? lastStatus_.control_mode
                          : static_cast<qint16>(core::plc_v26::kModeAuto);
  args.insert(QStringLiteral("plc_mode"), plcMode);
  args.insert(QStringLiteral("part_type_arg"), static_cast<int>(currentCategoryModeForAutoFlow()));
  if (decide == QMessageBox::Yes) {
    appendProductionLog(QStringLiteral("NG自动分支：用户选择当前件复测"));
    handleUiCommandRequested(QStringLiteral("START_RETEST_CURRENT"), args);
    return;
  }
  appendProductionLog(QStringLiteral("NG自动分支：用户选择继续（不复测）"));
  handleUiCommandRequested(QStringLiteral("CONTINUE_NO_RETEST"), args);
}

void MainWindow::processAutoScanIdCheck() {
  if (!plcRuntime_ || !hasLastStatus_) return;
  if (lastStatus_.scan_done == 0) return;

  if (!hasLastTray_) {
    core::PlcTrayPartIdBlockV2 tray;
    QString err;
    if (!plcRuntime_->readSecondStageTrayIds(&tray, &err)) {
      handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("自动ID核对前读取扫码ID失败") : err);
      return;
    }
  }

  if (idCheckStrategy_ == IdCheckStrategy::Bypass) {
    appendProductionLog(QStringLiteral("ID核对自动流：策略=BYPASS，自动放行"));
    tryAutoContinueAfterIdCheck();
    return;
  }

  if (idCheckStrategy_ == IdCheckStrategy::LocalMock) {
    QString err;
    QVector<QString> mockExpected;
    if (!loadMockExpectedPartIds(&mockExpected, &err)) {
      handlePlcRuntimeError(err);
      appendProductionLog(QStringLiteral("ID核对自动流：策略=LOCAL_MOCK，读取本地期望ID失败，阻塞继续"));
      return;
    }
    mesExpectedPartIds_ = mockExpected;
    appendProductionLog(QStringLiteral("ID核对自动流：策略=LOCAL_MOCK，文件=%1")
                            .arg(resolveIdCheckMockFilePath()));
  }

  bool hasExpectedIds = false;
  if (mesExpectedPartIds_.size() == core::kLogicalSlotCount) {
    for (const QString &id : mesExpectedPartIds_) {
      if (!id.trimmed().isEmpty()) {
        hasExpectedIds = true;
        break;
      }
    }
  }
  if (!hasExpectedIds) {
    if (idCheckStrategy_ == IdCheckStrategy::MesStrict) {
      appendProductionLog(QStringLiteral("ID核对自动流：策略=MES_STRICT，但未收到MES期望ID，阻塞继续"));
    } else {
      appendProductionLog(QStringLiteral("ID核对自动流：策略=LOCAL_MOCK，但文件未提供任何期望ID，阻塞继续"));
    }
    return;
  }

  QStringList mismatchDetails;
  QVector<int> mismatchSlots;
  const bool pass = evaluateIdCheckAgainstMes(&mismatchDetails, &mismatchSlots);
  if (pass) {
    appendProductionLog(QStringLiteral("ID核对自动流：MES比对通过，允许继续"));
    tryAutoContinueAfterIdCheck();
    return;
  }

  for (int slot : mismatchSlots) {
    if (productionWidget_) {
      productionWidget_->markSlotScanMismatch(slot, QStringLiteral("MES工件编号不一致"));
    }
  }
  appendProductionLog(QStringLiteral("ID核对自动流：MES比对失败，阻塞自动继续"));
  for (const QString &line : mismatchDetails) {
    appendProductionLog(QStringLiteral("  %1").arg(line));
  }
}

void MainWindow::processAutoMailboxFlow() {
  if (!plcRuntime_ || !hasLastMailboxSnapshot_ || !lastMailboxSnapshot_) return;
  if (hasLastStatus_ && calibrationFlowExpected_ && isCalibrationStepCode(lastStatus_.step_state)) {
    return;
  }

  if (!handleComputeResultRequested(lastMailboxSnapshot_->part_type)) {
    appendProductionLog(QStringLiteral("自动流：测量包计算失败，保持等待人工处理"));
    return;
  }
  if (!lastComputeHasItems_) {
    appendProductionLog(QStringLiteral("自动流：本次测量包无有效工件，跳过ACK/继续分支"));
    return;
  }

  if (!tryAutoWritePcAck()) {
    appendProductionLog(QStringLiteral("自动流：iPc_Ack写入失败，终止后续分支"));
    return;
  }

  if (lastComputeOverallOk_) {
    appendProductionLog(QStringLiteral("自动流：判定OK，自动发送继续（不复测）"));
    QVariantMap args;
    const qint16 plcMode = (hasLastStatus_
                         && lastStatus_.control_mode >= core::plc_v26::kModeManual
                         && lastStatus_.control_mode <= core::plc_v26::kModeSingleStep)
                            ? lastStatus_.control_mode
                            : static_cast<qint16>(core::plc_v26::kModeAuto);
    args.insert(QStringLiteral("plc_mode"), plcMode);
    args.insert(QStringLiteral("part_type_arg"), static_cast<int>(currentCategoryModeForAutoFlow()));
    handleUiCommandRequested(QStringLiteral("CONTINUE_NO_RETEST"), args);
    return;
  }
  appendProductionLog(QStringLiteral("自动流：判定NG，弹窗等待复测/继续决策"));
  promptNgDecisionAndDispatch();
}

void MainWindow::handleUiCommandRequested(const QString &cmd, const QVariantMap &args) {
  if (!plcRuntime_) {
    return;
  }
  const QString uiSource = args.value(QStringLiteral("ui_source")).toString().trimmed().toLower();
  const bool fromCalibrationUi = (uiSource == QStringLiteral("calibration"));
  const bool statusInCalibrationFlow = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  const bool commandInCalibrationFlow = fromCalibrationUi || statusInCalibrationFlow || cmd == QStringLiteral("START_CALIBRATION");
  auto appendCmdLog = [this, commandInCalibrationFlow](const QString &line) {
    if (commandInCalibrationFlow) appendCalibrationLog(line);
    else appendProductionLog(line);
  };
  if (cmd == QStringLiteral("COMPUTE_RESULT")) {
    QChar preferredPartType = QChar('A');
    const QString partTypeText = args.value(QStringLiteral("part_type")).toString().trimmed().toUpper();
    if (!partTypeText.isEmpty() && (partTypeText.at(0) == QChar('A') || partTypeText.at(0) == QChar('B'))) {
      preferredPartType = partTypeText.at(0);
    }
    handleComputeResultRequested(preferredPartType);
    return;
  }
  if (cmd == QStringLiteral("CONTINUE_AFTER_ID_CHECK")) {
    if (!tryAutoContinueAfterIdCheck()) return;
    return;
  }

  qint16 plcMode = static_cast<qint16>(mapArg(args, QStringLiteral("plc_mode"), static_cast<quint32>(core::plc_v26::kModeManual)));
  if (cmd == QStringLiteral("SET_MODE_MANUAL")) plcMode = core::plc_v26::kModeManual;
  if (cmd == QStringLiteral("SET_MODE_AUTO")) plcMode = core::plc_v26::kModeAuto;
  if (cmd == QStringLiteral("START_AUTO") || cmd == QStringLiteral("START_CALIBRATION")) plcMode = core::plc_v26::kModeAuto;
  qint16 categoryMode = static_cast<qint16>(mapArg(args, QStringLiteral("part_type_arg"), core::plc_v26::kPartTypeA));
  if (categoryMode == core::plc_v26::kPartTypeA || categoryMode == core::plc_v26::kPartTypeB) {
    lastCategoryMode_ = categoryMode;
  }

  QString err;
  if (!plcRuntime_->writePlcMode(plcMode, &err)) { handlePlcRuntimeError(err); return; }
  if (cmd == QStringLiteral("SET_MODE_MANUAL") || cmd == QStringLiteral("SET_MODE_AUTO")) {
    if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV26(plcMode)));
    plcRuntime_->pollOnce();
    return;
  }

  const bool needRewriteModeAndCategory =
      (cmd == QStringLiteral("INITIALIZE") ||
       cmd == QStringLiteral("START_AUTO") ||
       cmd == QStringLiteral("START_CALIBRATION"));
  if (needRewriteModeAndCategory) {
    if (!plcRuntime_->writePlcMode(plcMode, &err)) { handlePlcRuntimeError(err); return; }
    if (!plcRuntime_->setCategoryMode(categoryMode, &err)) { handlePlcRuntimeError(err); return; }
  }
  if (cmd == QStringLiteral("START_RETEST_CURRENT")
      || cmd == QStringLiteral("CONTINUE_NO_RETEST")) {
    if (!plcRuntime_->writeJudgeResult(core::plc_v26::kJudgeUnknown, &err)) {
      handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("清零 iJudge_Result 失败") : err);
      return;
    }
    appendCmdLog(QStringLiteral("已清零 iJudge_Result=0"));
  }

  quint16 cmdBits = 0;
  bool ok = false;
  if (cmd == QStringLiteral("INITIALIZE")) {
    cmdBits = core::plc_v26::kCmdInitializeBit;
    ok = plcRuntime_->sendInitialize(categoryMode, &err);
  } else if (cmd == QStringLiteral("START_AUTO")) {
    cmdBits = core::plc_v26::kCmdStartMeasureBit;
    ok = plcRuntime_->sendStartMeasure(categoryMode, &err);
  } else if (cmd == QStringLiteral("START_CALIBRATION")) {
    cmdBits = core::plc_v26::kCmdStartCalibrationBit;
    ok = plcRuntime_->sendStartCalibration(categoryMode, &err);
  } else if (cmd == QStringLiteral("STOP")) {
    cmdBits = core::plc_v26::kCmdStopBit;
    ok = plcRuntime_->sendStop(categoryMode, &err);
  } else if (cmd == QStringLiteral("RESET_ALARM") || cmd == QStringLiteral("HOME_ALL")) {
    cmdBits = core::plc_v26::kCmdResetBit;
    ok = plcRuntime_->sendReset(categoryMode, &err);
  } else if (cmd == QStringLiteral("START_RETEST_CURRENT")) {
    cmdBits = core::plc_v26::kCmdRetestCurrentBit;
    ok = plcRuntime_->sendRetestCurrent(categoryMode, &err);
  } else if (cmd == QStringLiteral("CONTINUE_NO_RETEST")) {
    cmdBits = core::plc_v26::kCmdContinueWithoutRetestBit;
    ok = plcRuntime_->sendContinueWithoutRetest(categoryMode, &err);
  } else if (cmd == QStringLiteral("ALARM_MUTE")) {
    cmdBits = core::plc_v26::kCmdAlarmMuteBit;
    ok = plcRuntime_->sendAlarmMute(categoryMode, &err);
  } else {
    handlePlcRuntimeError(QStringLiteral("暂未映射的 PLC 命令：%1").arg(cmd));
    return;
  }
  if (!ok) { handlePlcRuntimeError(err); return; }
  if (cmd == QStringLiteral("START_RETEST_CURRENT") && productionWidget_ && !commandInCalibrationFlow) {
    productionWidget_->clearActiveSlotsComputedResults();
    appendCmdLog(QStringLiteral("复测：已清理活跃槽位旧结果显示，等待新结果覆盖"));
  }
  if (cmd == QStringLiteral("START_CALIBRATION")) {
    calibrationFlowExpected_ = true;
  } else if (cmd == QStringLiteral("START_AUTO") ||
             cmd == QStringLiteral("INITIALIZE")) {
    calibrationFlowExpected_ = false;
  }
  awaitingCmdReply_ = true;
  pendingCmdBits_ = cmdBits;

  appendCmdLog(QStringLiteral("写 PLC 命令：%1 mode=%2 category=%3 code=0x%4")
                   .arg(cmd)
                   .arg(plcMode)
                   .arg(categoryMode)
                   .arg(QString::number(cmdBits, 16).toUpper()));
  if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 命令：%1 mode=%2 category=%3 code=0x%4")
                          .arg(cmd)
                          .arg(plcMode)
                          .arg(categoryMode)
                          .arg(QString::number(cmdBits, 16).toUpper()));
}

void MainWindow::handleWriteTrayPartIdRequested(int slotIndex, const QString &partId) {
  if (!plcRuntime_) {
    return;
  }
  if (slotIndex < 0 || slotIndex >= core::kLogicalSlotCount) {
    handlePlcRuntimeError(QStringLiteral("槽位号越界：%1").arg(slotIndex));
    return;
  }
  const QString id = partId.trimmed();
  if (id.isEmpty()) {
    handlePlcRuntimeError(QStringLiteral("工件ID不能为空"));
    return;
  }

  QString err;
  if (!plcRuntime_->writeTrayPartIdSlot(slotIndex, id, &err)) {
    handlePlcRuntimeError(err.isEmpty()
                              ? QStringLiteral("写入槽位 %1 工件ID失败").arg(slotIndex + 1)
                              : err);
    return;
  }

  appendProductionLog(QStringLiteral("已写入槽位%1工件ID：%2").arg(slotIndex + 1).arg(id));
  plcRuntime_->pollOnce();
}

void MainWindow::handleReadMailboxRequested(QChar preferredPartType, bool preferCalibrationContext) {
  if (!plcRuntime_) return;
  const bool calibrationContext = preferCalibrationContext
                               || isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  auto appendMailboxLog = [this, calibrationContext](const QString &line) {
    if (calibrationContext) appendCalibrationLog(line);
    else appendProductionLog(line);
  };
  core::PlcMailboxSnapshot snapshot;
  QString err;
  if (!plcRuntime_->readSecondStageMailboxSnapshot(preferredPartType, &snapshot, &err)) {
    handlePlcRuntimeError(err);
    return;
  }
  if (!lastMailboxSnapshot_) lastMailboxSnapshot_ = std::make_unique<core::PlcMailboxSnapshot>();
  *lastMailboxSnapshot_ = snapshot;
  hasLastMailboxSnapshot_ = true;

  appendMailboxLog(QStringLiteral("读取测量包成功：part=%1 item_count=%2")
                       .arg(QString(snapshot.part_type))
                       .arg(snapshot.item_count));
  if (calibrationContext && calibrationWidget_) {
    for (const auto &item : snapshot.items) {
      if (item.slot_index != core::kCalibrationSlotIndex) continue;
      core::CalibrationSlotSummary s;
      s.slot_index = item.slot_index;
      s.calibration_type = QString(snapshot.part_type.toUpper());
      s.calibration_master_part_id = calibrationWidget_->masterPartIdForType(snapshot.part_type);
      s.measured_part_id = item.part_id.trimmed().isEmpty() ? QStringLiteral("NG") : item.part_id.trimmed();
      s.valid = false;
      calibrationWidget_->setSlotSummary(s);
      break;
    }
  }

  if (!calibrationContext) {
    for (const auto &item : snapshot.items) {
      const QString slotText = item.slot_index >= 0 ? QString::number(item.slot_index + 1) : QStringLiteral("-");
      const QString idText = item.part_id.trimmed().isEmpty() ? QStringLiteral("NG") : item.part_id.trimmed();
      if (snapshot.part_type == QChar('A')) {
        appendMailboxLog(QStringLiteral("  item%1 slot=%2 id=%3 总长=%4 原始点数=%5")
                             .arg(item.item_index)
                             .arg(slotText)
                             .arg(idText)
                             .arg(item.total_len_mm, 0, 'f', 6)
                             .arg(item.raw_points_um.size()));
      } else {
        appendMailboxLog(QStringLiteral("  item%1 slot=%2 id=%3 AD=%4 BC=%5 原始点数=%6")
                             .arg(item.item_index)
                             .arg(slotText)
                             .arg(idText)
                             .arg(item.ad_len_mm, 0, 'f', 6)
                             .arg(item.bc_len_mm, 0, 'f', 6)
                             .arg(item.raw_points_um.size()));
      }
      if (!item.raw_points_um.isEmpty()) {
        QStringList vals;
        vals.reserve(item.raw_points_um.size());
        for (float v : item.raw_points_um) vals << QString::number(v, 'f', 6);
        appendMailboxLog(QStringLiteral("    raw=[%1]").arg(vals.join(QStringLiteral(","))));
      }
    }
  }
}

bool MainWindow::handleComputeResultRequested(QChar preferredPartType) {
  if (!plcRuntime_) return false;
  lastComputeHasItems_ = false;
  lastComputeOverallOk_ = false;
  lastComputePartType_ = preferredPartType.toUpper();

  core::PlcMailboxSnapshot snapshot;
  if (hasLastMailboxSnapshot_ && lastMailboxSnapshot_) {
    snapshot = *lastMailboxSnapshot_;
  } else {
    QString err;
    if (!plcRuntime_->readSecondStageMailboxSnapshot(preferredPartType, &snapshot, &err)) {
      handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("读取测量包失败，无法计算结果") : err);
      return false;
    }
    if (!lastMailboxSnapshot_) lastMailboxSnapshot_ = std::make_unique<core::PlcMailboxSnapshot>();
    *lastMailboxSnapshot_ = snapshot;
    hasLastMailboxSnapshot_ = true;
  }

  QString validErr;
  if (!snapshot.isValid(&validErr)) {
    handlePlcRuntimeError(QStringLiteral("测量包校验失败，无法计算：%1").arg(validErr));
    return false;
  }

  const int invalidLimit = qBound(0, appCfg_.algo.invalid_point_limit, 72);
  const int minValidPoints = qBound(3, 72 - invalidLimit, 72);
  const core::DiameterAlgoParams diameterParamsB =
      buildDiameterAlgoParams(appCfg_.algo, minValidPoints,
                              appCfg_.algo.a_b_k_in_mm, appCfg_.algo.a_b_k_out_mm);
  const core::DiameterAlgoParams diameterParamsC =
      buildDiameterAlgoParams(appCfg_.algo, minValidPoints,
                              appCfg_.algo.a_c_k_in_mm, appCfg_.algo.a_c_k_out_mm);
  const core::RunoutAlgoParams runoutParamsA =
      buildRunoutAlgoParams(appCfg_.algo, minValidPoints, appCfg_.algo.b_a_k_runout_mm);
  const core::RunoutAlgoParams runoutParamsD =
      buildRunoutAlgoParams(appCfg_.algo, minValidPoints, appCfg_.algo.b_d_k_runout_mm);
  const bool calibrationContext = isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  auto appendComputeLog = [this, calibrationContext](const QString &line) {
    if (calibrationContext) appendCalibrationLog(line);
    else appendProductionLog(line);
  };

  appendComputeLog(QStringLiteral("开始计算：part=%1 item_count=%2 无效点阈值=%3 最小有效点=%4")
                       .arg(QString(snapshot.part_type))
                       .arg(snapshot.item_count)
                       .arg(invalidLimit)
                       .arg(minValidPoints));
  appendComputeLog(QStringLiteral("A型输入偏置：内径=%1 mm 外径=%2 mm")
                       .arg(formatNumber(appCfg_.algo.a_inner_input_offset_mm))
                       .arg(formatNumber(appCfg_.algo.a_outer_input_offset_mm)));
  appendComputeLog(QStringLiteral("A型通道K：B端(K_in=%1,K_out=%2) C端(K_in=%3,K_out=%4)")
                       .arg(formatNumber(appCfg_.algo.a_b_k_in_mm))
                       .arg(formatNumber(appCfg_.algo.a_b_k_out_mm))
                       .arg(formatNumber(appCfg_.algo.a_c_k_in_mm))
                       .arg(formatNumber(appCfg_.algo.a_c_k_out_mm)));
  appendComputeLog(QStringLiteral("B型通道K：A点(K=%1) D点(K=%2)")
                       .arg(formatNumber(appCfg_.algo.b_a_k_runout_mm))
                       .arg(formatNumber(appCfg_.algo.b_d_k_runout_mm)));
  const QString runoutMetric = normalizedRunoutMetric(appCfg_.algo.runout_metric);
  appendComputeLog(QStringLiteral("B型跳动口径：%1").arg(runoutMetric));

  int expectedItemCount = 0;
  int judgedItemCount = 0;
  bool overallOk = true;
  for (const auto &item : snapshot.items) {
    if (item.present) ++expectedItemCount;
  }

  for (const auto &item : snapshot.items) {
    if (!item.present) continue;
    const QString slotText = slotTextByIndex(item.slot_index);
    const QString idText = item.part_id.trimmed().isEmpty() ? QStringLiteral("NG") : item.part_id.trimmed();

    core::ProductionSlotSummary slotSummary;
    slotSummary.slot_index = item.slot_index;
    slotSummary.part_id = idText;
    slotSummary.part_type = snapshot.part_type.toUpper();
    slotSummary.valid = true;
    slotSummary.compute.valid = true;

    if (snapshot.part_type.toUpper() == QChar('A')) {
      if (item.raw_points_um.size() < 72 * 4) {
        appendComputeLog(QStringLiteral("计算失败：A型 item%1 raw点数不足，期望>=288，实际=%2")
                             .arg(item.item_index)
                             .arg(item.raw_points_um.size()));
        continue;
      }

      Channel72Series bInner = buildChannel72Series(item.raw_points_um, 0, invalidLimit);
      Channel72Series bOuter = buildChannel72Series(item.raw_points_um, 72, invalidLimit);
      Channel72Series cInner = buildChannel72Series(item.raw_points_um, 144, invalidLimit);
      Channel72Series cOuter = buildChannel72Series(item.raw_points_um, 216, invalidLimit);
      applyChannelOffset(&bInner, appCfg_.algo.a_inner_input_offset_mm);
      applyChannelOffset(&cInner, appCfg_.algo.a_inner_input_offset_mm);
      applyChannelOffset(&bOuter, appCfg_.algo.a_outer_input_offset_mm);
      applyChannelOffset(&cOuter, appCfg_.algo.a_outer_input_offset_mm);

      auto runInner = [&](const Channel72Series &ch, const core::DiameterAlgoParams &params) -> core::DiameterChannelResult {
        if (ch.invalid_too_many) return core::DiameterChannelResult{};
        return core::computeInnerDiameter(ch.values_mm, ch.valid_mask, params);
      };
      auto runOuter = [&](const Channel72Series &ch, const core::DiameterAlgoParams &params) -> core::DiameterChannelResult {
        if (ch.invalid_too_many) return core::DiameterChannelResult{};
        return core::computeOuterDiameter(ch.values_mm, ch.valid_mask, params);
      };

      const core::DiameterChannelResult rBInner = runInner(bInner, diameterParamsB);
      const core::DiameterChannelResult rBOuter = runOuter(bOuter, diameterParamsB);
      const core::DiameterChannelResult rCInner = runInner(cInner, diameterParamsC);
      const core::DiameterChannelResult rCOuter = runOuter(cOuter, diameterParamsC);

      slotSummary.compute.values.total_len_mm = item.total_len_mm;
      slotSummary.compute.values.id_left_mm = rBInner.success ? static_cast<float>(rBInner.circle_fit.diameter_mm) : qQNaN();
      slotSummary.compute.values.od_left_mm = rBOuter.success ? static_cast<float>(rBOuter.circle_fit.diameter_mm) : qQNaN();
      slotSummary.compute.values.id_right_mm = rCInner.success ? static_cast<float>(rCInner.circle_fit.diameter_mm) : qQNaN();
      slotSummary.compute.values.od_right_mm = rCOuter.success ? static_cast<float>(rCOuter.circle_fit.diameter_mm) : qQNaN();
      slotSummary.compute.valid = rBInner.success && rBOuter.success && rCInner.success && rCOuter.success;

      JudgeEvalState judge;
      evaluateSpecItem(QStringLiteral("A总长"), slotSummary.compute.values.total_len_mm,
                       appCfg_.algo.spec_a_total_len, true, &judge);
      evaluateSpecItem(QStringLiteral("A左内径"), slotSummary.compute.values.id_left_mm,
                       appCfg_.algo.spec_a_id_left, false, &judge);
      evaluateSpecItem(QStringLiteral("A左外径"), slotSummary.compute.values.od_left_mm,
                       appCfg_.algo.spec_a_od_left, false, &judge);
      evaluateSpecItem(QStringLiteral("A右内径"), slotSummary.compute.values.id_right_mm,
                       appCfg_.algo.spec_a_id_right, false, &judge);
      evaluateSpecItem(QStringLiteral("A右外径"), slotSummary.compute.values.od_right_mm,
                       appCfg_.algo.spec_a_od_right, false, &judge);
      finalizeSlotJudgement(&slotSummary, &judge);
      judgedItemCount += 1;
      if (!slotSummary.judgement_ok) overallOk = false;

      appendComputeLog(QStringLiteral("A型 item%1 slot=%2 id=%3 总长=%4 ID(B)=%5 OD(B)=%6 ID(C)=%7 OD(C)=%8")
                           .arg(item.item_index)
                           .arg(slotText)
                           .arg(idText)
                           .arg(formatNumber(item.total_len_mm))
                           .arg(formatNumber(slotSummary.compute.values.id_left_mm))
                           .arg(formatNumber(slotSummary.compute.values.od_left_mm))
                           .arg(formatNumber(slotSummary.compute.values.id_right_mm))
                           .arg(formatNumber(slotSummary.compute.values.od_right_mm)));
      appendComputeLog(QStringLiteral("  A型通道有效点: B内=%1/72 B外=%2/72 C内=%3/72 C外=%4/72")
                           .arg(countValidMask(bInner.valid_mask))
                           .arg(countValidMask(bOuter.valid_mask))
                           .arg(countValidMask(cInner.valid_mask))
                           .arg(countValidMask(cOuter.valid_mask)));

      if (bInner.invalid_too_many || bOuter.invalid_too_many || cInner.invalid_too_many || cOuter.invalid_too_many) {
        appendComputeLog(QStringLiteral("  A型通道无效：无效点超过阈值(%1)").arg(invalidLimit));
      } else if (!slotSummary.compute.valid) {
        appendComputeLog(QStringLiteral("  A型拟合失败：B内[%1] B外[%2] C内[%3] C外[%4]")
                             .arg(rBInner.error, rBOuter.error, rCInner.error, rCOuter.error));
      }
      appendComputeLog(QStringLiteral("  判定=%1%2")
                           .arg(slotSummary.judgement_ok ? QStringLiteral("OK") : QStringLiteral("NG"))
                           .arg(slotSummary.judgement_ok ? QString() : QStringLiteral(" 原因=%1").arg(slotSummary.fail_reason_text)));
    } else if (snapshot.part_type.toUpper() == QChar('B')) {
      if (item.raw_points_um.size() < 72 * 2) {
        appendComputeLog(QStringLiteral("计算失败：B型 item%1 raw点数不足，期望>=144，实际=%2")
                             .arg(item.item_index)
                             .arg(item.raw_points_um.size()));
        continue;
      }

      const Channel72Series aRunout = buildChannel72Series(item.raw_points_um, 0, invalidLimit);
      const Channel72Series dRunout = buildChannel72Series(item.raw_points_um, 72, invalidLimit);

      auto runRunout = [&](const Channel72Series &ch, const core::RunoutAlgoParams &params) -> core::RunoutResult {
        if (ch.invalid_too_many) return core::RunoutResult{};
        return core::computeRunoutAnalysis(ch.values_mm, ch.valid_mask, params);
      };

      const core::RunoutResult rA = runRunout(aRunout, runoutParamsA);
      const core::RunoutResult rD = runRunout(dRunout, runoutParamsD);

      slotSummary.compute.values.ad_len_mm = item.ad_len_mm;
      slotSummary.compute.values.bc_len_mm = item.bc_len_mm;
      slotSummary.compute.values.runout_left_mm = static_cast<float>(selectedRunoutValue(rA, runoutMetric));
      slotSummary.compute.values.runout_right_mm = static_cast<float>(selectedRunoutValue(rD, runoutMetric));
      slotSummary.compute.valid = rA.success && rD.success;

      JudgeEvalState judge;
      evaluateSpecItem(QStringLiteral("B_AD长度"), slotSummary.compute.values.ad_len_mm,
                       appCfg_.algo.spec_b_ad_len, true, &judge);
      evaluateSpecItem(QStringLiteral("B_BC长度"), slotSummary.compute.values.bc_len_mm,
                       appCfg_.algo.spec_b_bc_len, true, &judge);
      evaluateSpecItem(QStringLiteral("B左跳动"), slotSummary.compute.values.runout_left_mm,
                       appCfg_.algo.spec_b_runout_left, false, &judge);
      evaluateSpecItem(QStringLiteral("B右跳动"), slotSummary.compute.values.runout_right_mm,
                       appCfg_.algo.spec_b_runout_right, false, &judge);
      finalizeSlotJudgement(&slotSummary, &judge);
      judgedItemCount += 1;
      if (!slotSummary.judgement_ok) overallOk = false;

      appendComputeLog(QStringLiteral("B型 item%1 slot=%2 id=%3 AD=%4 BC=%5 跳动A=%6 跳动D=%7")
                           .arg(item.item_index)
                           .arg(slotText)
                           .arg(idText)
                           .arg(formatNumber(item.ad_len_mm))
                           .arg(formatNumber(item.bc_len_mm))
                           .arg(formatNumber(slotSummary.compute.values.runout_left_mm))
                           .arg(formatNumber(slotSummary.compute.values.runout_right_mm)));
      appendComputeLog(QStringLiteral("  B型通道有效点: A点=%1/72 D点=%2/72")
                           .arg(countValidMask(aRunout.valid_mask))
                           .arg(countValidMask(dRunout.valid_mask)));

      if (aRunout.invalid_too_many || dRunout.invalid_too_many) {
        appendComputeLog(QStringLiteral("  B型通道无效：无效点超过阈值(%1)").arg(invalidLimit));
      } else if (!slotSummary.compute.valid) {
        appendComputeLog(QStringLiteral("  B型拟合失败：A点[%1] D点[%2]").arg(rA.error, rD.error));
      }
      appendComputeLog(QStringLiteral("  判定=%1%2")
                           .arg(slotSummary.judgement_ok ? QStringLiteral("OK") : QStringLiteral("NG"))
                           .arg(slotSummary.judgement_ok ? QString() : QStringLiteral(" 原因=%1").arg(slotSummary.fail_reason_text)));
    } else {
      appendComputeLog(QStringLiteral("计算失败：未知part_type=%1").arg(QString(snapshot.part_type)));
      continue;
    }

    if (!calibrationContext && productionWidget_ && item.slot_index >= 0 && item.slot_index < core::kLogicalSlotCount) {
      productionWidget_->setSlotSummary(item.slot_index, slotSummary);
    } else if (calibrationContext && calibrationWidget_) {
      core::CalibrationSlotSummary calSummary;
      calSummary.slot_index = item.slot_index;
      calSummary.calibration_type = QString(snapshot.part_type.toUpper());
      calSummary.calibration_master_part_id = calibrationWidget_->masterPartIdForType(snapshot.part_type);
      calSummary.measured_part_id = idText;
      calSummary.valid = slotSummary.valid;
      calSummary.judgement_known = slotSummary.judgement_known;
      calSummary.judgement_ok = slotSummary.judgement_ok;
      calSummary.fail_reason_text = slotSummary.fail_reason_text;
      calSummary.compute = slotSummary.compute;
      calibrationWidget_->setSlotSummary(calSummary);
    }
  }

  if (expectedItemCount > 0 && judgedItemCount < expectedItemCount) {
    overallOk = false;
    appendComputeLog(QStringLiteral("警告：部分工件未生成有效判定（expected=%1, judged=%2），总判定按 NG 处理")
                         .arg(expectedItemCount)
                         .arg(judgedItemCount));
  }
  lastComputeHasItems_ = (expectedItemCount > 0);
  lastComputeOverallOk_ = (expectedItemCount > 0) ? overallOk : false;
  lastComputePartType_ = snapshot.part_type.toUpper();
  if (expectedItemCount > 0) {
    const quint16 judgeResult = overallOk ? core::plc_v26::kJudgeOk : core::plc_v26::kJudgeNg;
    QString err;
    if (!plcRuntime_->writeJudgeResult(judgeResult, &err)) {
      handlePlcRuntimeError(err.isEmpty() ? QStringLiteral("写 iJudge_Result 失败") : err);
    } else {
      appendComputeLog(QStringLiteral("已写 iJudge_Result=%1 (%2)")
                           .arg(judgeResult)
                           .arg(overallOk ? QStringLiteral("OK") : QStringLiteral("NG")));
    }
  } else {
    appendComputeLog(QStringLiteral("本次测量包无有效 item，跳过写 iJudge_Result"));
  }

  appendComputeLog(QStringLiteral("计算结束"));
  return true;
}


void MainWindow::handleAckMailboxRequested(bool preferCalibrationContext) {
  if (!plcRuntime_) return;
  const bool calibrationContext = preferCalibrationContext
                               || isCalibrationContext(hasLastStatus_, calibrationFlowExpected_, hasLastStatus_ ? lastStatus_.step_state : 0);
  QString err;
  if (!plcRuntime_->sendPcAck(1, &err)) {
    handlePlcRuntimeError(err);
    return;
  }
  if (calibrationContext) appendCalibrationLog(QStringLiteral("手动写入 pc_ack=1"));
  else appendProductionLog(QStringLiteral("手动写入 pc_ack=1"));
}
