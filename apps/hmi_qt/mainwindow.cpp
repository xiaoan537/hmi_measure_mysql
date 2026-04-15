#include "alarm_widget.hpp"
#include "calibration_widget.hpp"
#include "data_widget.hpp"
#include "dev_tools_widget.hpp"
#include "manual_maintain_widget.hpp"
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
#include <QByteArray>
#include <QThread>
#include <QStringList>

#include <cstring>

#include <QSizePolicy>

#include "ui_mainwindow.h"

#include <cmath>
#include <memory>
#include <array>

#include "core/measurement_pipeline.hpp"
#include "core/plc_contract_v2.hpp"
#include "core/plc_fake_client_v2.hpp"
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

QString stepStateText(quint16 stepState) {
  switch (static_cast<core::PlcStepStateV2>(stepState)) {
  case core::PlcStepStateV2::WaitStart: return QStringLiteral("等待启动");
  case core::PlcStepStateV2::WaitTrayReady: return QStringLiteral("等待上料就绪");
  case core::PlcStepStateV2::ScanTrayIds: return QStringLiteral("扫码中");
  case core::PlcStepStateV2::WaitPcIdCheck: return QStringLiteral("等待PC核对ID");
  case core::PlcStepStateV2::PickFromTray: return QStringLiteral("取料中");
  case core::PlcStepStateV2::MoveToStations: return QStringLiteral("移动到工位");
  case core::PlcStepStateV2::PlaceToStations: return QStringLiteral("放置到工位");
  case core::PlcStepStateV2::MeasureActive: return QStringLiteral("测量中");
  case core::PlcStepStateV2::GenerateMailbox: return QStringLiteral("生成测量包");
  case core::PlcStepStateV2::WaitPcRead: return QStringLiteral("等待PC读取");
  case core::PlcStepStateV2::ReturnToTray: return QStringLiteral("回盘中");
  case core::PlcStepStateV2::CycleComplete: return QStringLiteral("循环完成");
  case core::PlcStepStateV2::CalWaitLoadSlot16: return QStringLiteral("标定等待16号槽上料");
  case core::PlcStepStateV2::CalWaitPcConfirm: return QStringLiteral("标定等待PC确认");
  case core::PlcStepStateV2::CalMeasure: return QStringLiteral("标定测量中");
  case core::PlcStepStateV2::CalWaitPcRead: return QStringLiteral("标定等待PC读取");
  case core::PlcStepStateV2::CalComplete: return QStringLiteral("标定完成");
  case core::PlcStepStateV2::Fault: return QStringLiteral("故障");
  case core::PlcStepStateV2::EStop: return QStringLiteral("急停");
  default: return QStringLiteral("STEP(%1)").arg(stepState);
  }
}


QString plcModeTextV25(int mode) { return core::plc_codec_v26::plcModeText(static_cast<qint16>(mode)); }


QByteArray mbBytesFromRegsUi(const QVector<quint16> &regs) {
  QByteArray bytes;
  bytes.reserve(regs.size() * 2);
  for (quint16 reg : regs) {
    bytes.append(static_cast<char>(reg & 0x00FFu));
    bytes.append(static_cast<char>((reg >> 8) & 0x00FFu));
  }
  return bytes;
}

QByteArray float64ToMbBytes(double value) {
  QByteArray bytes(8, '\0');
  quint64 bits = 0;
  static_assert(sizeof(double) == sizeof(quint64), "double size");
  std::memcpy(&bits, &value, sizeof(bits));
  for (int i = 0; i < 8; ++i) {
    bytes[i] = static_cast<char>((bits >> (8 * i)) & 0xFFu);
  }
  return bytes;
}

QString axisNameByIndex(int axisIndex) { return core::plc_v26::axisName(axisIndex); }

QString machineStateText(quint16 machineState) { return core::plc_codec_v26::decodeMachineState(machineState).text; }

bool isCalibrationStep(quint16 stepState) {
  return stepState >= 200 && stepState < 300;
}

bool isProcessingProductionStep(quint16 stepState) {
  switch (static_cast<core::PlcStepStateV2>(stepState)) {
  case core::PlcStepStateV2::PickFromTray:
  case core::PlcStepStateV2::MoveToStations:
  case core::PlcStepStateV2::PlaceToStations:
  case core::PlcStepStateV2::MeasureActive:
  case core::PlcStepStateV2::GenerateMailbox:
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
  if (cmd == QStringLiteral("INITIALIZE")) return core::PlcCommandCodeV2::Initialize;
  if (cmd == QStringLiteral("START_AUTO")) return core::PlcCommandCodeV2::StartAuto;
  if (cmd == QStringLiteral("START_CALIBRATION")) return core::PlcCommandCodeV2::StartCalibration;
  if (cmd == QStringLiteral("STOP")) return core::PlcCommandCodeV2::Stop;
  if (cmd == QStringLiteral("RESET_ALARM")) return core::PlcCommandCodeV2::ResetAlarm;
  if (cmd == QStringLiteral("HOME_ALL")) return core::PlcCommandCodeV2::HomeAll;
  if (cmd == QStringLiteral("PAUSE")) return core::PlcCommandCodeV2::Pause;
  if (cmd == QStringLiteral("RESUME")) return core::PlcCommandCodeV2::Resume;
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
  ui_->stackedWidget->addWidget(
      new SettingsWidget(cfg, iniPath_, ui_->stackedWidget));
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
            if (manualMaintainWidget_) {
              manualMaintainWidget_->setRuntimeSummary(connected,
                                                       hasLastStatus_ ? machineStateText(lastStatus_.machine_state) : QStringLiteral("-"),
                                                       hasLastStatus_ ? stepStateText(lastStatus_.step_state) : QStringLiteral("-"));
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
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::trayUpdated,
          this, &MainWindow::onPlcTrayUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::mailboxSnapshotUpdated,
          this, &MainWindow::onPlcMailboxSnapshotUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::plcEventsRaised,
          this, &MainWindow::onPlcEventsRaised);

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
    appendProductionLog(QStringLiteral("正在连接 PLC..."));
    if (!plcRuntime_->connectNow(&err)) {
      handlePlcRuntimeError(err);
    }
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
            this, &MainWindow::handleAckMailboxRequested);
  }

  if (manualMaintainWidget_) {
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestSetPlcMode, this, [this](int mode) {
      QString err;
      if (!plcRuntime_->writePlcMode(static_cast<qint16>(mode), &err)) { handlePlcRuntimeError(err); return; }
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV25(mode)));
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
            this, &MainWindow::handleAckMailboxRequested);
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
            this, [this] { handleReadMailboxRequested(productionWidget_ ? productionWidget_->selectedPartTypeText().at(0) : QChar('A')); });
    connect(productionWidget_, &ProductionWidget::requestReloadSlotIds,
            this, [this] {
              core::PlcTrayPartIdBlockV2 tray; QString err;
              if (!plcRuntime_->readSecondStageTrayIds(&tray, &err)) { handlePlcRuntimeError(err); return; }
              onPlcTrayUpdated(tray);
            });
    connect(productionWidget_, &ProductionWidget::requestAckMailbox,
            this, &MainWindow::handleAckMailboxRequested);
    connect(productionWidget_, &ProductionWidget::requestContinueAfterIdCheck,
            this, [this]{ handleUiCommandRequested(QStringLiteral("CONTINUE_AFTER_ID_CHECK"), {}); });
    connect(productionWidget_, &ProductionWidget::requestSetPlcMode,
            this, [this](int mode){ if (!plcRuntime_) return; QString err; if (!plcRuntime_->writePlcMode(static_cast<qint16>(mode), &err)) { handlePlcRuntimeError(err); return; } appendProductionLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV25(mode))); plcRuntime_->pollOnce(); });
    connect(productionWidget_, &ProductionWidget::requestWriteCategoryMode,
            this, [this](int partTypeArg){ if (!plcRuntime_) return; QString err; if (!plcRuntime_->setCategoryMode(static_cast<qint16>(partTypeArg), &err)) { handlePlcRuntimeError(err); return; } appendProductionLog(QStringLiteral("写工件类型到 PLC：%1").arg(partTypeArg == 1 ? QStringLiteral("B型") : QStringLiteral("A型"))); });
    connect(productionWidget_, &ProductionWidget::requestWriteSlotIds,
            this, &MainWindow::handleWriteTrayPartIdsRequested);
    connect(productionWidget_, &ProductionWidget::uiCommandRequested,
            this, &MainWindow::handleUiCommandRequested);
    connect(productionWidget_, &ProductionWidget::requestReconnectPlc,
            this, [this]{ attemptReconnectPlc(true); });
  }

  if (calibrationWidget_) {
    connect(calibrationWidget_, &CalibrationWidget::requestReadMailbox,
            this, [this] { handleReadMailboxRequested(calibrationWidget_ ? calibrationWidget_->selectedPartTypeText().at(0) : QChar('A')); });
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

void MainWindow::appendProductionLog(const QString &text) {
  if (productionWidget_) productionWidget_->appendPlcLogMessage(text);
}

void MainWindow::attemptReconnectPlc(bool manual) {
  if (!plcRuntime_) return;
  QString err;
  if (manual) appendProductionLog(QStringLiteral("尝试重新连接 PLC..."));
  plcRuntime_->disconnectNow();
  if (!plcRuntime_->connectNow(&err)) {
    handlePlcRuntimeError(err);
    return;
  }
  plcRuntime_->pollOnce();
}

void MainWindow::handlePlcRuntimeError(const QString &message) {
  if (!message.trimmed().isEmpty()) {
    statusBar()->showMessage(QStringLiteral("PLC: %1").arg(message), 5000);
    appendProductionLog(message);
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
  }
  if (diagnosticsWidget_) {
    const int pollHz = (stats.poll_interval_ms > 0) ? (1000 / stats.poll_interval_ms) : 0;
    diagnosticsWidget_->setCommStats(pollHz, stats.last_poll_ms,
                                     stats.poll_ok_count, stats.poll_error_count);
  }
  if (!stats.connected && plcRuntime_ && plcRuntime_->config().plc.auto_reconnect && !reconnectAttemptLogged_) {
    appendProductionLog(QStringLiteral("尝试重新连接 PLC..."));
    reconnectAttemptLogged_ = true;
  }
  if (manualMaintainWidget_) {
    manualMaintainWidget_->setRuntimeSummary(stats.connected,
                                             hasLastStatus_ ? machineStateText(lastStatus_.machine_state) : QStringLiteral("-"),
                                             hasLastStatus_ ? stepStateText(lastStatus_.step_state) : QStringLiteral("-"));
    if (hasLastStatus_) manualMaintainWidget_->setCurrentPlcMode(lastStatus_.control_mode);
  }
}

void MainWindow::onPlcStatusUpdated(const core::PlcStatusBlockV2 &status) {
  lastStatus_ = status;
  hasLastStatus_ = true;
  if (status.scan_done == 0) {
    // scan_done 已被 PC 清零，允许下一轮扫码完成后再次触发自动继续。
    lastAutoContinueScanSeq_ = 0;
  }
  if (manualMaintainWidget_) {
    manualMaintainWidget_->setRuntimeSummary(plcRuntime_ ? plcRuntime_->isConnected() : false,
                                             machineStateText(status.machine_state),
                                             stepStateText(status.step_state));
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

  if (productionWidget_) {
    productionWidget_->setMachineState(status.machine_state, machineStateText(status.machine_state));
    productionWidget_->setStepState(status.step_state);
    productionWidget_->setStateSeq(0);
    productionWidget_->setAlarm(status.alarm_code, 0);
    productionWidget_->setInterlockMask(status.interlock_mask);
    productionWidget_->setTrayPresentMask(status.tray_present_mask);
    productionWidget_->setCalibrationMode(isCalibrationStep(status.step_state));
    productionWidget_->setCurrentPlcMode(status.control_mode);

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
      if (isActive) {
        if (status.step_state == static_cast<quint16>(core::PlcStepStateV2::WaitPcRead)) {
          state = SlotRuntimeState::WaitingPcRead;
          note = QStringLiteral("已测完成，等待 PC 读取并 ACK");
        } else if (isProcessingProductionStep(status.step_state)) {
          state = SlotRuntimeState::Measuring;
          note = QStringLiteral("当前活跃槽位");
        }
      }

      productionWidget_->setSlotRuntimeState(slot, state, note);
    }
  }

  if (calibrationWidget_) {
    calibrationWidget_->setStepState(status.step_state);
    calibrationWidget_->setTrayPresentMask(status.tray_present_mask);
    if (status.mailbox_ready != lastMailboxReady_) {
      calibrationWidget_->setMailboxReady(status.mailbox_ready != 0);
      lastMailboxReady_ = status.mailbox_ready;
    }
  }
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
  if (productionWidget_) {
    productionWidget_->setScannedPartIds(trayToVector(tray));
    productionWidget_->appendPlcLogMessage(QStringLiteral("读取扫码ID成功："));
    for (int i = 0; i < core::kLogicalSlotCount; ++i) {
      const QString id = tray.part_ids[i].trimmed().isEmpty() ? QStringLiteral("NG") : tray.part_ids[i].trimmed();
      productionWidget_->appendPlcLogMessage(QStringLiteral("  槽位%1：%2").arg(i + 1).arg(id));
    }
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
    slot0 = snapshot.items.at(0).slot_index >= 0 ? QString::number(snapshot.items.at(0).slot_index + 1) : QStringLiteral("-");
    partId0 = snapshot.items.at(0).part_id;
    total0 = snapshot.items.at(0).total_len_mm;
  }
  if (snapshot.items.size() > 1) {
    slot1Value = snapshot.items.at(1).slot_index;
    slot1 = snapshot.items.at(1).slot_index >= 0 ? QString::number(snapshot.items.at(1).slot_index + 1) : QStringLiteral("-");
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

  if (productionWidget_) {
    productionWidget_->appendPlcLogMessage(QStringLiteral("Mailbox 已解析：part=%1 slot0=%2 slot1=%3")
                                               .arg(mailboxPartTypeText(snapshot), slot0, slot1));
  }

}

void MainWindow::onPlcEventsRaised(const core::PlcPollEventsV2 &events) {
  if (productionWidget_) {
    if (events.scan_ready) {
      productionWidget_->appendPlcLogMessage(QStringLiteral("检测到新扫码结果"));
    }
    if (events.new_mailbox) {
      productionWidget_->appendPlcLogMessage(QStringLiteral("检测到新测量包，等待业务处理/ACK"));
    }
  }
}

void MainWindow::onPlcFlowModeChanged(int mode) {
  plcFlowMode_ = mode;
}

void MainWindow::handleUiCommandRequested(const QString &cmd, const QVariantMap &args) {
  if (!plcRuntime_) {
    return;
  }
  if (cmd == QStringLiteral("CONTINUE_AFTER_ID_CHECK")) {
    QString err;
    if (!plcRuntime_->writeScanDone(0, &err)) {
      handlePlcRuntimeError(err);
      return;
    }
    if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 scan_done=0（ID核对通过）"));
    plcRuntime_->pollOnce();
    return;
  }

  quint16 cmdBits = 0;
  if (cmd == QStringLiteral("INITIALIZE")) cmdBits = core::plc_v26::kCmdInitializeBit;
  else if (cmd == QStringLiteral("START_AUTO")) cmdBits = core::plc_v26::kCmdStartMeasureBit;
  else if (cmd == QStringLiteral("START_CALIBRATION")) cmdBits = core::plc_v26::kCmdStartCalibrationBit;
  else if (cmd == QStringLiteral("STOP")) cmdBits = core::plc_v26::kCmdStopBit;
  else if (cmd == QStringLiteral("RESET_ALARM") || cmd == QStringLiteral("HOME_ALL")) cmdBits = core::plc_v26::kCmdResetBit;
  else {
    handlePlcRuntimeError(QStringLiteral("暂未映射的 PLC 命令：%1").arg(cmd));
    return;
  }

  qint16 plcMode = static_cast<qint16>(mapArg(args, QStringLiteral("plc_mode"), static_cast<quint32>(core::plc_v26::kModeManual)));
  if (cmd == QStringLiteral("SET_MODE_MANUAL")) plcMode = core::plc_v26::kModeManual;
  if (cmd == QStringLiteral("SET_MODE_AUTO")) plcMode = core::plc_v26::kModeAuto;
  if (cmd == QStringLiteral("START_AUTO") || cmd == QStringLiteral("START_CALIBRATION")) plcMode = core::plc_v26::kModeAuto;
  qint16 categoryMode = static_cast<qint16>(mapArg(args, QStringLiteral("part_type_arg"), core::plc_v26::kPartTypeA));

  QString err;
  if (!plcRuntime_->writePlcMode(plcMode, &err)) { handlePlcRuntimeError(err); return; }
  if (cmd == QStringLiteral("SET_MODE_MANUAL") || cmd == QStringLiteral("SET_MODE_AUTO")) {
    if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV25(plcMode)));
    plcRuntime_->pollOnce();
    return;
  }

  core::PlcCommandBlockV2 command;
  command.cmd_code = cmdBits;
  command.category_mode = categoryMode;
  if (!plcRuntime_->sendCommand(command, &err)) { handlePlcRuntimeError(err); return; }
  appendProductionLog(QStringLiteral("写 PLC 命令：%1 mode=%2 category=%3 code=0x%4")
                          .arg(cmd)
                          .arg(plcMode)
                          .arg(command.category_mode)
                          .arg(QString::number(command.cmd_code, 16).toUpper()));
  if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 命令：%1 mode=%2 category=%3 code=0x%4")
                          .arg(cmd)
                          .arg(plcMode)
                          .arg(command.category_mode)
                          .arg(QString::number(command.cmd_code, 16).toUpper()));
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

void MainWindow::handleReadMailboxRequested(QChar preferredPartType) {
  if (!plcRuntime_) return;
  core::PlcMailboxSnapshot snapshot;
  QString err;
  if (!plcRuntime_->readSecondStageMailboxSnapshot(preferredPartType, &snapshot, &err)) {
    handlePlcRuntimeError(err);
    return;
  }

  if (productionWidget_) {
    productionWidget_->appendPlcLogMessage(QStringLiteral("读取测量包成功：part=%1 item_count=%2")
                                               .arg(QString(snapshot.part_type))
                                               .arg(snapshot.item_count));
    for (const auto &item : snapshot.items) {
      const QString slotText = item.slot_index >= 0 ? QString::number(item.slot_index + 1) : QStringLiteral("-");
      const QString idText = item.part_id.trimmed().isEmpty() ? QStringLiteral("NG") : item.part_id.trimmed();
      if (snapshot.part_type == QChar('A')) {
        productionWidget_->appendPlcLogMessage(QStringLiteral("  item%1 slot=%2 id=%3 总长=%4 原始点数=%5")
                                                   .arg(item.item_index)
                                                   .arg(slotText)
                                                   .arg(idText)
                                                   .arg(item.total_len_mm, 0, 'f', 6)
                                                   .arg(item.raw_points_um.size()));
      } else {
        productionWidget_->appendPlcLogMessage(QStringLiteral("  item%1 slot=%2 id=%3 AD=%4 BC=%5 原始点数=%6")
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
        productionWidget_->appendPlcLogMessage(QStringLiteral("    raw=[%1]").arg(vals.join(QStringLiteral(","))));
      }
    }
  }
}


void MainWindow::handleAckMailboxRequested() {
  if (!plcRuntime_) return;
  QString err;
  if (!plcRuntime_->sendPcAck(1, &err)) {
    handlePlcRuntimeError(err);
    return;
  }
  if (productionWidget_) {
    productionWidget_->appendPlcLogMessage(QStringLiteral("手动写入 pc_ack=1"));
  }
}
