#include "alarm_widget.hpp"
#include "calibration_widget.hpp"
#include "data_widget.hpp"
#include "dev_tools_widget.hpp"
#include "diagnostics_widget.hpp"
#include "mainwindow.hpp"
#include "mes_upload_widget.hpp"
#include "mes_worker.hpp"
#include "production_widget.hpp"
#include "raw_viewer_widget.hpp"
#include "settings_widget.hpp"
#include "todo_widget.hpp"

#include <QAction>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVariantMap>

#include <QSizePolicy>

#include "ui_mainwindow.h"

#include <cmath>
#include <memory>

#include "core/measurement_pipeline.hpp"
#include "core/plc_contract_v2.hpp"
#include "core/plc_fake_client_v2.hpp"
#include "core/plc_runtime_v2.hpp"

namespace {
QString mailboxPartTypeText(const core::PlcMailboxSnapshot &snapshot) {
  const QChar pt = snapshot.part_type.toUpper();
  if (pt == QChar('A') || pt == QChar('B')) {
    return QString(pt);
  }
  return QStringLiteral("-");
}

QString machineStateText(quint16 machineState) {
  switch (static_cast<core::PlcMachineState>(machineState)) {
  case core::PlcMachineState::Idle: return QStringLiteral("IDLE");
  case core::PlcMachineState::Auto: return QStringLiteral("AUTO");
  case core::PlcMachineState::Manual: return QStringLiteral("MANUAL");
  case core::PlcMachineState::Paused: return QStringLiteral("PAUSED");
  case core::PlcMachineState::Fault: return QStringLiteral("FAULT");
  case core::PlcMachineState::EStop: return QStringLiteral("ESTOP");
  default: return QStringLiteral("STATE(%1)").arg(machineState);
  }
}

bool isCalibrationStep(quint16 stepState) {
  return stepState >= 200 && stepState < 300;
}

bool isActiveProductionStep(quint16 stepState) {
  switch (static_cast<core::PlcStepStateV2>(stepState)) {
  case core::PlcStepStateV2::PickFromTray:
  case core::PlcStepStateV2::MoveToStations:
  case core::PlcStepStateV2::PlaceToStations:
  case core::PlcStepStateV2::MeasureActive:
  case core::PlcStepStateV2::GenerateMailbox:
  case core::PlcStepStateV2::WaitPcRead:
  case core::PlcStepStateV2::ReturnToTray:
    return true;
  default:
    return false;
  }
}

QVector<QString> trayToVector(const core::PlcTrayPartIdBlockV2 &tray) {
  QVector<QString> ids(core::kLogicalSlotCount);
  for (int i = 0; i < core::kLogicalSlotCount; ++i) {
    ids[i] = tray.part_ids[i];
  }
  return ids;
}

core::PlcCommandCodeV2 uiCommandToCode(const QString &cmd, bool *ok) {
  if (ok) *ok = true;
  if (cmd == QStringLiteral("SET_MODE_AUTO")) return core::PlcCommandCodeV2::SetModeAuto;
  if (cmd == QStringLiteral("SET_MODE_MANUAL")) return core::PlcCommandCodeV2::SetModeManual;
  if (cmd == QStringLiteral("START_AUTO")) return core::PlcCommandCodeV2::StartAuto;
  if (cmd == QStringLiteral("START_CALIBRATION")) return core::PlcCommandCodeV2::StartCalibration;
  if (cmd == QStringLiteral("PAUSE")) return core::PlcCommandCodeV2::Pause;
  if (cmd == QStringLiteral("RESUME")) return core::PlcCommandCodeV2::Resume;
  if (cmd == QStringLiteral("STOP")) return core::PlcCommandCodeV2::Stop;
  if (cmd == QStringLiteral("RESET_ALARM")) return core::PlcCommandCodeV2::ResetAlarm;
  if (cmd == QStringLiteral("HOME_ALL")) return core::PlcCommandCodeV2::HomeAll;
  if (cmd == QStringLiteral("CONTINUE_AFTER_ID_CHECK")) return core::PlcCommandCodeV2::ContinueAfterIdCheck;
  if (cmd == QStringLiteral("REQUEST_RESCAN_IDS")) return core::PlcCommandCodeV2::RequestRescanIds;
  if (cmd == QStringLiteral("CONTINUE_AFTER_NG_CONFIRM")) return core::PlcCommandCodeV2::ContinueAfterNgConfirm;
  if (cmd == QStringLiteral("START_RETEST_CURRENT")) return core::PlcCommandCodeV2::StartRetestCurrent;
  if (ok) *ok = false;
  return core::PlcCommandCodeV2::Stop;
}

quint32 mapArg(const QVariantMap &args, const QString &key, quint32 def = 0) {
  const QVariant v = args.value(key);
  if (!v.isValid()) return def;
  bool ok = false;
  const quint32 out = v.toUInt(&ok);
  return ok ? out : def;
}

QVector<float> makeDemoMailboxArray(const core::PlcMailboxHeaderV2 &header) {
  const int total = static_cast<int>(header.item_count) * static_cast<int>(header.ring_count) *
                    static_cast<int>(header.point_count) * static_cast<int>(header.channel_count);
  QVector<float> values;
  values.reserve(total);
  for (int item = 0; item < static_cast<int>(header.item_count); ++item) {
    for (int ring = 0; ring < static_cast<int>(header.ring_count); ++ring) {
      Q_UNUSED(ring);
      for (int ch = 0; ch < static_cast<int>(header.channel_count); ++ch) {
        for (int pt = 0; pt < static_cast<int>(header.point_count); ++pt) {
          const double angle = (static_cast<double>(pt) / qMax(1, static_cast<int>(header.point_count))) * 6.28318530718;
          const double base = (item == 0 ? 1000.0 : 1200.0) + ch * 35.0;
          values.push_back(static_cast<float>(base + 25.0 * std::sin(angle) + 5.0 * std::cos(angle * 2.0)));
        }
      }
    }
  }
  return values;
}
} // namespace

MainWindow::MainWindow(const core::AppConfig &cfg, const QString &iniPath,
                       MesWorker *worker, QWidget *parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow), iniPath_(iniPath) {
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
       QStringLiteral("手动(待做)"), QStringLiteral("报表(待做)"),
       QStringLiteral("用户权限(待做)")});

  productionWidget_ = new ProductionWidget(cfg, ui_->stackedWidget);
  calibrationWidget_ = new CalibrationWidget(cfg, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(productionWidget_);
  ui_->stackedWidget->addWidget(calibrationWidget_);
  ui_->stackedWidget->addWidget(new DataWidget(cfg, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(
      new MesUploadWidget(cfg, worker, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(
      new SettingsWidget(cfg, iniPath_, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(new AlarmWidget(cfg, ui_->stackedWidget));
  diagnosticsWidget_ = new DiagnosticsWidget(cfg, ui_->stackedWidget);
  ui_->stackedWidget->addWidget(diagnosticsWidget_);
  ui_->stackedWidget->addWidget(new RawViewerWidget(cfg, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(new DevToolsWidget(cfg, ui_->stackedWidget));

  ui_->stackedWidget->addWidget(new TodoWidget(
      QStringLiteral("手动/维护（TODO）"),
      QStringLiteral("后续将增加手动读写槽位、单步调试、回零等维护功能。"),
      ui_->stackedWidget));
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
          });
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::errorOccurred,
          this, &MainWindow::handlePlcRuntimeError);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::statsUpdated,
          this, &MainWindow::onPlcStatsUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::statusUpdated,
          this, &MainWindow::onPlcStatusUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::trayUpdated,
          this, &MainWindow::onPlcTrayUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::mailboxSnapshotUpdated,
          this, &MainWindow::onPlcMailboxSnapshotUpdated);

  if (!cfg.plc.enabled) {
    return;
  }

  if (cfg.plc.use_fake_client) {
    fakePlcClient_ = new core::FakePlcRegisterClientV2(this);
    plcRuntime_->setRegisterClient(fakePlcClient_, false);
    seedFakePlcDemoData();
  }

  QString err;
  if (!plcRuntime_->start(&err)) {
    handlePlcRuntimeError(err);
    updatePlcStatusLabel();
    return;
  }

  if (!cfg.plc.use_fake_client) {
    if (!plcRuntime_->connectNow(&err)) {
      handlePlcRuntimeError(err);
    }
  }
  updatePlcStatusLabel();
}

void MainWindow::setupDiagnosticsBindings() {
  if (!diagnosticsWidget_ || !plcRuntime_) {
    return;
  }

  connect(diagnosticsWidget_, &DiagnosticsWidget::requestRefresh,
          plcRuntime_.get(), &core::PlcRuntimeServiceV2::pollOnce);
  connect(diagnosticsWidget_, &DiagnosticsWidget::requestReadMailbox,
          plcRuntime_.get(), &core::PlcRuntimeServiceV2::pollOnce);
  connect(diagnosticsWidget_, &DiagnosticsWidget::requestAckMailbox,
          this, &MainWindow::handleAckMailboxRequested);
}

void MainWindow::setupBusinessPageBindings() {
  if (!plcRuntime_) {
    return;
  }

  if (productionWidget_) {
    connect(productionWidget_, &ProductionWidget::requestReadMailbox,
            plcRuntime_.get(), &core::PlcRuntimeServiceV2::pollOnce);
    connect(productionWidget_, &ProductionWidget::requestReloadSlotIds,
            plcRuntime_.get(), &core::PlcRuntimeServiceV2::pollOnce);
    connect(productionWidget_, &ProductionWidget::requestAckMailbox,
            this, &MainWindow::handleAckMailboxRequested);
    connect(productionWidget_, &ProductionWidget::requestWriteSlotIds,
            this, &MainWindow::handleWriteTrayPartIdsRequested);
    connect(productionWidget_, &ProductionWidget::uiCommandRequested,
            this, &MainWindow::handleUiCommandRequested);
  }

  if (calibrationWidget_) {
    connect(calibrationWidget_, &CalibrationWidget::requestReadMailbox,
            plcRuntime_.get(), &core::PlcRuntimeServiceV2::pollOnce);
    connect(calibrationWidget_, &CalibrationWidget::requestAckMailbox,
            this, &MainWindow::handleAckMailboxRequested);
    connect(calibrationWidget_, &CalibrationWidget::uiCommandRequested,
            this, &MainWindow::handleUiCommandRequested);
  }
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
  const QString mode = cfg.use_fake_client ? QStringLiteral("Fake") : QStringLiteral("Real");
  const QString conn = plcRuntime_->isConnected() ? QStringLiteral("已连接") : QStringLiteral("未连接");
  lbPlc_->setText(QStringLiteral("PLC: %1 / %2").arg(mode, conn));
}

void MainWindow::seedFakePlcDemoData() {
  if (!plcRuntime_ || !fakePlcClient_) {
    return;
  }

  const auto &layout = plcRuntime_->addressLayout();
  QString err;

  core::PlcStatusBlockV2 status;
  status.machine_state = static_cast<quint16>(core::PlcMachineState::Auto);
  status.step_state = static_cast<quint16>(core::PlcStepStateV2::WaitPcRead);
  status.state_seq = 1;
  status.tray_present_mask = 0x0006; // slot1 + slot2
  status.scan_done = 1;
  status.scan_seq = 1;
  status.active_item_count = 2;
  status.active_slot_index[0] = 1;
  status.active_slot_index[1] = 2;
  status.mailbox_ready = 1;
  status.meas_seq = 1;
  fakePlcClient_->loadStatusBlock(layout, status, &err);

  core::PlcTrayPartIdBlockV2 tray;
  tray.part_ids[1] = QStringLiteral("DEMO-A-0001");
  tray.part_ids[2] = QStringLiteral("DEMO-A-0002");
  fakePlcClient_->loadTrayPartIdBlock(layout, tray, &err);

  core::PlcCommandBlockV2 command;
  fakePlcClient_->loadCommandBlock(layout, command, &err);

  core::PlcMailboxRawFrame frame;
  frame.header.meas_seq = 1;
  frame.header.part_type = 1; // A
  frame.header.item_count = 2;
  frame.header.slot_index[0] = 1;
  frame.header.slot_index[1] = 2;
  frame.header.part_id_ascii[0] = tray.part_ids[1];
  frame.header.part_id_ascii[1] = tray.part_ids[2];
  frame.header.total_len_mm[0] = 12.34f;
  frame.header.total_len_mm[1] = 12.29f;
  frame.header.ad_len_mm[0] = 0.0f;
  frame.header.ad_len_mm[1] = 0.0f;
  frame.header.bc_len_mm[0] = 0.0f;
  frame.header.bc_len_mm[1] = 0.0f;
  frame.header.raw_layout_ver = 1;
  frame.header.ring_count = static_cast<quint16>(qMax(1, plcRuntime_->config().scan_a.rings));
  frame.header.point_count = static_cast<quint16>(qMax(1, plcRuntime_->config().scan_a.points_per_ring));
  frame.header.channel_count = 4;
  frame.arrays_um = makeDemoMailboxArray(frame.header);
  fakePlcClient_->loadMailboxRawFrame(layout, frame, &err);
}

void MainWindow::handlePlcRuntimeError(const QString &message) {
  if (!message.trimmed().isEmpty()) {
    statusBar()->showMessage(QStringLiteral("PLC: %1").arg(message), 5000);
  }
}

void MainWindow::onPlcStatsUpdated(const core::PlcRuntimeStatsV2 &stats) {
  updatePlcStatusLabel();
  if (productionWidget_) {
    productionWidget_->setPlcConnected(stats.connected);
  }
  if (calibrationWidget_) {
    calibrationWidget_->setPlcConnected(stats.connected);
  }
  if (diagnosticsWidget_) {
    const int pollHz = (stats.poll_interval_ms > 0) ? (1000 / stats.poll_interval_ms) : 0;
    diagnosticsWidget_->setCommStats(pollHz, stats.last_poll_ms,
                                     stats.poll_ok_count, stats.poll_error_count);
  }
}

void MainWindow::onPlcStatusUpdated(const core::PlcStatusBlockV2 &status) {
  if (diagnosticsWidget_) {
    diagnosticsWidget_->setStatusFields(static_cast<int>(status.step_state),
                                        static_cast<int>(status.machine_state),
                                        static_cast<int>(status.alarm_code),
                                        static_cast<int>(status.alarm_level),
                                        static_cast<quint32>(status.interlock_mask),
                                        static_cast<int>(status.meas_seq));
  }

  if (productionWidget_) {
    productionWidget_->setMachineState(status.machine_state, machineStateText(status.machine_state));
    productionWidget_->setStepState(status.step_state);
    productionWidget_->setStateSeq(status.state_seq);
    productionWidget_->setAlarm(status.alarm_code, status.alarm_level);
    productionWidget_->setInterlockMask(status.interlock_mask);
    productionWidget_->setTrayPresentMask(status.tray_present_mask);
    productionWidget_->setCalibrationMode(isCalibrationStep(status.step_state));

    for (int slot = 0; slot < core::kLogicalSlotCount; ++slot) {
      const bool present = ((status.tray_present_mask >> slot) & 0x1u) != 0;
      if (!present) {
        productionWidget_->setSlotRuntimeState(slot, SlotRuntimeState::Empty, QString());
        continue;
      }

      SlotRuntimeState state = SlotRuntimeState::Loaded;
      QString note;

      if (isCalibrationStep(status.step_state) && slot == core::kCalibrationSlotIndex) {
        state = SlotRuntimeState::Calibration;
      } else if (status.step_state == static_cast<quint16>(core::PlcStepStateV2::ScanTrayIds)) {
        state = SlotRuntimeState::Loaded;
        note = QStringLiteral("PLC 扫码中");
      } else if (status.step_state == static_cast<quint16>(core::PlcStepStateV2::WaitPcIdCheck)) {
        state = SlotRuntimeState::WaitingIdCheck;
        note = QStringLiteral("等待 PC 核对 ID");
      }

      const bool isActive = (status.active_item_count >= 1 && status.active_slot_index[0] == static_cast<quint16>(slot)) ||
                            (status.active_item_count >= 2 && status.active_slot_index[1] == static_cast<quint16>(slot));
      if (isActive && isActiveProductionStep(status.step_state)) {
        state = SlotRuntimeState::Measuring;
        note = QStringLiteral("当前活跃槽位");
      }

      productionWidget_->setSlotRuntimeState(slot, state, note);
    }
  }

  if (calibrationWidget_) {
    calibrationWidget_->setStepState(status.step_state);
    calibrationWidget_->setTrayPresentMask(status.tray_present_mask);
    if (status.mailbox_ready != lastMailboxReady_ || status.meas_seq != lastMailboxSeq_) {
      calibrationWidget_->setMailboxReady(status.mailbox_ready != 0);
      lastMailboxReady_ = status.mailbox_ready;
      lastMailboxSeq_ = status.meas_seq;
    }
  }
}

void MainWindow::onPlcTrayUpdated(const core::PlcTrayPartIdBlockV2 &tray) {
  if (productionWidget_) {
    productionWidget_->setScannedPartIds(trayToVector(tray));
  }
}

void MainWindow::onPlcMailboxSnapshotUpdated(const core::PlcMailboxSnapshot &snapshot) {
  QString slot0 = QStringLiteral("-");
  QString slot1 = QStringLiteral("-");
  QString partId0;
  QString partId1;
  quint16 slot0Value = core::kInvalidSlotIndex;
  quint16 slot1Value = core::kInvalidSlotIndex;
  float total0 = 0.0f;
  float total1 = 0.0f;

  if (!snapshot.items.isEmpty()) {
    slot0Value = snapshot.items.at(0).slot_index;
    slot0 = QString::number(snapshot.items.at(0).slot_index);
    partId0 = snapshot.items.at(0).part_id;
    total0 = snapshot.items.at(0).total_len_mm;
  }
  if (snapshot.items.size() > 1) {
    slot1Value = snapshot.items.at(1).slot_index;
    slot1 = QString::number(snapshot.items.at(1).slot_index);
    partId1 = snapshot.items.at(1).part_id;
    total1 = snapshot.items.at(1).total_len_mm;
  }

  if (diagnosticsWidget_) {
    diagnosticsWidget_->setMailboxPreview(mailboxPartTypeText(snapshot), slot0, slot1, partId0, partId1);
  }

  if (productionWidget_) {
    productionWidget_->setMeasureDone(true);
    productionWidget_->setMailboxPreview(snapshot.meas_seq,
                                         snapshot.part_type,
                                         slot0Value,
                                         slot1Value,
                                         partId0,
                                         partId1,
                                         false,
                                         false,
                                         0,
                                         0,
                                         total0,
                                         total1);
  }

  if (calibrationWidget_) {
    for (const auto &item : snapshot.items) {
      if (item.slot_index == core::kCalibrationSlotIndex) {
        core::CalibrationSlotSummary s;
        s.slot_index = item.slot_index;
        s.calibration_type = QString(snapshot.part_type.toUpper());
        s.calibration_master_part_id = (snapshot.part_type.toUpper() == QChar('B'))
                                           ? QStringLiteral("CAL-B-001")
                                           : QStringLiteral("CAL-A-001");
        s.measured_part_id = item.part_id;
        s.valid = false;
        calibrationWidget_->setSlotSummary(s);
        break;
      }
    }
  }
}

void MainWindow::handleUiCommandRequested(const QString &cmd, const QVariantMap &args) {
  if (!plcRuntime_) {
    return;
  }
  bool ok = false;
  const auto code = uiCommandToCode(cmd, &ok);
  if (!ok) {
    handlePlcRuntimeError(QStringLiteral("暂未映射的 PLC 命令：%1").arg(cmd));
    return;
  }

  core::PlcCommandBlockV2 command;
  command.cmd_code = static_cast<quint16>(code);
  command.cmd_seq = plcCommandSeq_++;
  command.cmd_arg0 = mapArg(args, QStringLiteral("mode_arg"), 0);
  command.cmd_arg1 = mapArg(args, QStringLiteral("arg1"), 0);

  QString err;
  if (!plcRuntime_->sendCommand(command, &err)) {
    handlePlcRuntimeError(err);
  }
}

void MainWindow::handleWriteTrayPartIdsRequested(const QVector<QString> &slotIds) {
  if (!plcRuntime_) {
    return;
  }
  QString err;
  for (int i = 0; i < slotIds.size() && i < core::kLogicalSlotCount; ++i) {
    if (!plcRuntime_->writeTrayPartIdSlot(i, slotIds.at(i), &err)) {
      handlePlcRuntimeError(err.isEmpty()
                                ? QStringLiteral("写入槽位 %1 工件 ID 失败").arg(i)
                                : err);
      return;
    }
  }
  plcRuntime_->pollOnce();
}

void MainWindow::handleAckMailboxRequested() {
  if (!plcRuntime_) return;
  QString err;
  if (!plcRuntime_->sendPcAck(1, &err)) {
    handlePlcRuntimeError(err);
  }
}
