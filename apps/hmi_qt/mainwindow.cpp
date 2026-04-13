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


constexpr quint32 kAxisCtrlRegsPerAxisV25 = 104;
constexpr quint32 kAxisStaRegsPerAxisV25 = 12;
constexpr quint32 kAxisCtrlBytesPerAxisV25 = kAxisCtrlRegsPerAxisV25 * 2;
constexpr quint32 kAxisStaBytesPerAxisV25 = kAxisStaRegsPerAxisV25 * 2;
constexpr quint32 kAxisCtrlOffsetEnable = 162;
constexpr quint32 kAxisCtrlOffsetReset = 163;
constexpr quint32 kAxisCtrlOffsetHome = 164;
constexpr quint32 kAxisCtrlOffsetEStop = 165;
constexpr quint32 kAxisCtrlOffsetStop = 166;
constexpr quint32 kAxisCtrlOffsetMoveAbs = 167;
constexpr quint32 kAxisCtrlOffsetMoveRel = 168;
constexpr quint32 kAxisCtrlOffsetJogForward = 169;
constexpr quint32 kAxisCtrlOffsetJogBackward = 170;
constexpr quint32 kAxisCtrlOffsetSetAcc = 176;
constexpr quint32 kAxisCtrlOffsetSetDec = 184;
constexpr quint32 kAxisCtrlOffsetSetPosition = 192;
constexpr quint32 kAxisCtrlOffsetSetVelocity = 200;
constexpr quint32 kAxisStaOffsetEnabled = 0;
constexpr quint32 kAxisStaOffsetHomed = 1;
constexpr quint32 kAxisStaOffsetError = 2;
constexpr quint32 kAxisStaOffsetBusy = 3;
constexpr quint32 kAxisStaOffsetDone = 4;
constexpr quint32 kAxisStaOffsetErrorId = 6;
constexpr quint32 kAxisStaOffsetActPositionRegs = 4;
constexpr quint32 kAxisStaOffsetActVelocityRegs = 8;
constexpr quint32 kCylinderCtrlBytes = 3;
constexpr quint32 kCylinderStaBytes = 6;

QString plcModeTextV25(int mode) {
  switch (mode) {
  case 1: return QStringLiteral("手动");
  case 2: return QStringLiteral("自动");
  case 3: return QStringLiteral("单步");
  default: return QStringLiteral("模式(%1)").arg(mode);
  }
}

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

QString axisNameByIndex(int axisIndex) {
  static const QStringList names = {
      QStringLiteral("龙门X轴"), QStringLiteral("龙门Y轴"), QStringLiteral("龙门Z轴"),
      QStringLiteral("测量X1轴"), QStringLiteral("测量X2轴"), QStringLiteral("测量X3轴"),
      QStringLiteral("内外径R1轴"), QStringLiteral("内外径R2轴"),
      QStringLiteral("跳动R3轴"), QStringLiteral("跳动R4轴")};
  return (axisIndex >= 0 && axisIndex < names.size()) ? names.at(axisIndex) : QStringLiteral("Axis%1").arg(axisIndex + 1);
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

  if (devToolsWidget_) {
    connect(devToolsWidget_, &DevToolsWidget::requestPlcPollOnce,
            plcRuntime_.get(), &core::PlcRuntimeServiceV2::pollOnce);
    connect(devToolsWidget_, &DevToolsWidget::requestPlcReloadSlotIds,
            this, [this] { core::PlcTrayPartIdBlockV2 tray; QString err; if (!plcRuntime_->readSecondStageTrayIds(&tray, &err)) handlePlcRuntimeError(err); });
    connect(devToolsWidget_, &DevToolsWidget::requestPlcReadMailbox,
            this, [this] { handleReadMailboxRequested(productionWidget_ ? productionWidget_->selectedPartTypeText().at(0) : QChar('A')); });
    connect(devToolsWidget_, &DevToolsWidget::requestPlcAckMailbox,
            this, &MainWindow::handleAckMailboxRequested);
    connect(devToolsWidget_, &DevToolsWidget::requestPlcContinueAfterIdCheck,
            this, [this] { handleUiCommandRequested(QStringLiteral("CONTINUE_AFTER_ID_CHECK"), QVariantMap{}); });
    connect(devToolsWidget_, &DevToolsWidget::requestPlcRequestRescanIds,
            this, [this] { handleUiCommandRequested(QStringLiteral("REQUEST_RESCAN_IDS"), QVariantMap{}); });
    connect(devToolsWidget_, &DevToolsWidget::plcFlowModeChanged,
            this, &MainWindow::onPlcFlowModeChanged);
    connect(devToolsWidget_, &DevToolsWidget::requestSetPlcMode, this, [this](int mode) {
      QString err;
      if (!plcRuntime_->writePlcMode(static_cast<qint16>(mode), &err)) { handlePlcRuntimeError(err); return; }
      if (devToolsWidget_) devToolsWidget_->appendPlcLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV25(mode)));
      plcRuntime_->pollOnce();
    });
    connect(devToolsWidget_, &DevToolsWidget::requestPlcNamedCommand, this, &MainWindow::handleUiCommandRequested);
    connect(devToolsWidget_, &DevToolsWidget::requestAxisCommand, this, [this](int axisIndex, const QString &action) {
      if (!plcRuntime_) return;
      const quint32 baseMb = plcRuntime_->config().plc.axis_ctrl_start_address * 2u + static_cast<quint32>(axisIndex) * kAxisCtrlBytesPerAxisV25;
      quint32 offset = kAxisCtrlOffsetEnable;
      if (action == QStringLiteral("RESET")) offset = kAxisCtrlOffsetReset;
      else if (action == QStringLiteral("HOME")) offset = kAxisCtrlOffsetHome;
      else if (action == QStringLiteral("STOP")) offset = kAxisCtrlOffsetStop;
      else if (action == QStringLiteral("JOG_FWD")) offset = kAxisCtrlOffsetJogForward;
      else if (action == QStringLiteral("JOG_BWD")) offset = kAxisCtrlOffsetJogBackward;
      QString err;
      if (!plcRuntime_->writeMbBytesRaw(baseMb + offset, QByteArray(1, '\x01'), &err)) { handlePlcRuntimeError(err); return; }
      if (action != QStringLiteral("ENABLE")) { QThread::msleep(50); plcRuntime_->writeMbBytesRaw(baseMb + offset, QByteArray(1, '\x00'), nullptr); }
      if (devToolsWidget_) devToolsWidget_->appendPlcLog(QStringLiteral("写轴控制：axis=%1 action=%2").arg(axisIndex + 1).arg(action));
    });
    connect(devToolsWidget_, &DevToolsWidget::requestReadAxisStatus, this, [this](int axisIndex) {
      if (!plcRuntime_) return;
      QByteArray bytes; QString err;
      const quint32 baseMb = plcRuntime_->config().plc.axis_sta_start_address * 2u + static_cast<quint32>(axisIndex) * kAxisStaBytesPerAxisV25;
      if (!plcRuntime_->readMbBytesRaw(baseMb, static_cast<quint16>(kAxisStaBytesPerAxisV25), &bytes, &err)) { handlePlcRuntimeError(err); return; }
      auto u8 = [&](int i){ return (i < bytes.size()) ? static_cast<unsigned char>(bytes.at(i)) : 0; };
      auto u16le = [&](int i){ return static_cast<int>(u8(i) | (u8(i+1) << 8)); };
      QVector<quint16> regs;
      if (!plcRuntime_->readHoldingRegistersRaw(plcRuntime_->config().plc.axis_sta_start_address + axisIndex * kAxisStaRegsPerAxisV25, static_cast<quint16>(kAxisStaRegsPerAxisV25), &regs, &err)) { handlePlcRuntimeError(err); return; }
      double pos=0.0, vel=0.0;
      core::plcReadFloat64WordSwappedAt(regs, 4, &pos, nullptr);
      core::plcReadFloat64WordSwappedAt(regs, 8, &vel, nullptr);
      if (devToolsWidget_) devToolsWidget_->appendPlcLog(QStringLiteral("Axis%1 状态: enabled=%2 homed=%3 error=%4 busy=%5 done=%6 errId=%7 pos=%8 vel=%9")
        .arg(axisIndex + 1).arg(u8(0)).arg(u8(1)).arg(u8(2)).arg(u8(3)).arg(u8(4)).arg(u16le(6)).arg(pos,0,'f',3).arg(vel,0,'f',3));
    });
    connect(devToolsWidget_, &DevToolsWidget::requestCylinderCommand, this, [this](const QString &group, int index, const QString &action) {
      if (!plcRuntime_) return;
      quint32 startMb = 0;
      if (group == QStringLiteral("LM")) { startMb = plcRuntime_->config().plc.lm_ctrl_start_address * 2u; }
      else if (group == QStringLiteral("CL")) { startMb = plcRuntime_->config().plc.cl_ctrl_start_address * 2u + static_cast<quint32>(index) * kCylinderCtrlBytes; }
      else { startMb = plcRuntime_->config().plc.gt2_ctrl_start_address * 2u + static_cast<quint32>(index) * kCylinderCtrlBytes; }
      QByteArray bytes(kCylinderCtrlBytes, '\0');
      if (action == QStringLiteral("P")) bytes[0] = 1; else if (action == QStringLiteral("N")) bytes[1] = 1; else bytes[2] = 1;
      QString err;
      if (!plcRuntime_->writeMbBytesRaw(startMb, bytes, &err)) { handlePlcRuntimeError(err); return; }
      QThread::msleep(50); plcRuntime_->writeMbBytesRaw(startMb, QByteArray(kCylinderCtrlBytes, '\0'), nullptr);
      if (devToolsWidget_) devToolsWidget_->appendPlcLog(QStringLiteral("写气缸控制：%1[%2] action=%3").arg(group).arg(index + 1).arg(action));
    });
    connect(devToolsWidget_, &DevToolsWidget::requestReadCylinderStatus, this, [this]() {
      if (!plcRuntime_) return; QString err; QByteArray bytes;
      auto readOne=[&](quint32 startMb, const QString &name){ QByteArray b; if (!plcRuntime_->readMbBytesRaw(startMb, kCylinderStaBytes, &b, &err)) return false; auto u8=[&](int i){ return (i < b.size()) ? static_cast<unsigned char>(b.at(i)) : 0; }; int errId = (u8(4) | (u8(5)<<8)); if (devToolsWidget_) devToolsWidget_->appendPlcLog(QStringLiteral("%1 状态: P=%2 N=%3 Error=%4 ErrId=%5").arg(name).arg(u8(0)).arg(u8(1)).arg(u8(2)).arg(errId)); return true; };
      if (!readOne(plcRuntime_->config().plc.lm_sta_start_address * 2u, QStringLiteral("抓料气缸"))) { handlePlcRuntimeError(err); return; }
      for (int i=0;i<3;++i){ if (!readOne(plcRuntime_->config().plc.cl_sta_start_address * 2u + static_cast<quint32>(i)*kCylinderStaBytes, QStringLiteral("CL%1").arg(i+1))) { handlePlcRuntimeError(err); return; }}
      for (int i=0;i<4;++i){ if (!readOne(plcRuntime_->config().plc.gt2_sta_start_address * 2u + static_cast<quint32>(i)*kCylinderStaBytes, QStringLiteral("GT2_%1").arg(i+1))) { handlePlcRuntimeError(err); return; }}
    });
    devToolsWidget_->setPlcFlowMode(plcFlowMode_);
  }

  if (manualMaintainWidget_) {
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestSetPlcMode, this, [this](int mode) {
      QString err;
      if (!plcRuntime_->writePlcMode(static_cast<qint16>(mode), &err)) { handlePlcRuntimeError(err); return; }
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV25(mode)));
      plcRuntime_->pollOnce();
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestPlcNamedCommand, this, &MainWindow::handleUiCommandRequested);
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestAxisCommand, this, [this](int axisIndex, const QString &action) {
      if (!plcRuntime_) return;
      const quint32 baseMb = plcRuntime_->config().plc.axis_ctrl_start_address * 2u + static_cast<quint32>(axisIndex) * kAxisCtrlBytesPerAxisV25;
      quint32 offset = kAxisCtrlOffsetEnable;
      QByteArray payload(1, '\x01');
      bool pulse = false;
      if (action == QStringLiteral("ENABLE_ON")) { offset = kAxisCtrlOffsetEnable; payload[0] = '\x01'; }
      else if (action == QStringLiteral("ENABLE_OFF")) { offset = kAxisCtrlOffsetEnable; payload[0] = '\0'; }
      else if (action == QStringLiteral("RESET")) { offset = kAxisCtrlOffsetReset; pulse = true; }
      else if (action == QStringLiteral("HOME")) { offset = kAxisCtrlOffsetHome; pulse = true; }
      else if (action == QStringLiteral("ESTOP")) { offset = kAxisCtrlOffsetEStop; pulse = true; }
      else if (action == QStringLiteral("STOP")) { offset = kAxisCtrlOffsetStop; pulse = true; }
      QString err;
      if (!plcRuntime_->writeMbBytesRaw(baseMb + offset, payload, &err)) { handlePlcRuntimeError(err); return; }
      if (pulse) { QThread::msleep(50); plcRuntime_->writeMbBytesRaw(baseMb + offset, QByteArray(1, '\0'), nullptr); }
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写轴控制：%1 action=%2 val=%3").arg(axisNameByIndex(axisIndex)).arg(action).arg(static_cast<int>(static_cast<unsigned char>(payload.at(0)))));
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestAxisJog, this, [this](int axisIndex, const QString &direction, bool active) {
      if (!plcRuntime_) return;
      const quint32 baseMb = plcRuntime_->config().plc.axis_ctrl_start_address * 2u + static_cast<quint32>(axisIndex) * kAxisCtrlBytesPerAxisV25;
      const quint32 offset = (direction == QStringLiteral("JOG_BWD")) ? kAxisCtrlOffsetJogBackward : kAxisCtrlOffsetJogForward;
      QString err;
      if (!plcRuntime_->writeMbBytesRaw(baseMb + offset, QByteArray(1, active ? '\x01' : '\0'), &err)) { handlePlcRuntimeError(err); return; }
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestAxisMove, this, [this](int axisIndex, const QString &action, double acc, double dec, double pos, double vel) {
      if (!plcRuntime_) return;
      const quint32 baseMb = plcRuntime_->config().plc.axis_ctrl_start_address * 2u + static_cast<quint32>(axisIndex) * kAxisCtrlBytesPerAxisV25;
      QString err;
      QByteArray paramBytes;
      paramBytes += float64ToMbBytes(acc);
      paramBytes += float64ToMbBytes(dec);
      paramBytes += float64ToMbBytes(pos);
      paramBytes += float64ToMbBytes(vel);
      if (!plcRuntime_->writeMbBytesRaw(baseMb + kAxisCtrlOffsetSetAcc, paramBytes, &err)) { handlePlcRuntimeError(err); return; }
      const quint32 triggerOffset = (action == QStringLiteral("MOVE_REL")) ? kAxisCtrlOffsetMoveRel : kAxisCtrlOffsetMoveAbs;
      if (!plcRuntime_->writeMbBytesRaw(baseMb + triggerOffset, QByteArray(1, '\x01'), &err)) { handlePlcRuntimeError(err); return; }
      QThread::msleep(50);
      plcRuntime_->writeMbBytesRaw(baseMb + triggerOffset, QByteArray(1, '\0'), nullptr);
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写轴运动：%1 action=%2 acc=%3 dec=%4 pos=%5 vel=%6").arg(axisNameByIndex(axisIndex)).arg(action).arg(acc,0,'f',3).arg(dec,0,'f',3).arg(pos,0,'f',3).arg(vel,0,'f',3));
    });
    connect(manualMaintainWidget_, &ManualMaintainWidget::requestCylinderCommand, this, [this](const QString &group, int index, const QString &action) {
      if (!plcRuntime_) return;
      quint32 startMb = 0;
      if (group == QStringLiteral("LM")) { startMb = plcRuntime_->config().plc.lm_ctrl_start_address * 2u; }
      else if (group == QStringLiteral("CL")) { startMb = plcRuntime_->config().plc.cl_ctrl_start_address * 2u + static_cast<quint32>(index) * kCylinderCtrlBytes; }
      else { startMb = plcRuntime_->config().plc.gt2_ctrl_start_address * 2u + static_cast<quint32>(index) * kCylinderCtrlBytes; }
      QByteArray bytes(kCylinderCtrlBytes, '\0');
      if (action == QStringLiteral("P")) bytes[0] = 1; else if (action == QStringLiteral("N")) bytes[1] = 1; else bytes[2] = 1;
      QString err;
      if (!plcRuntime_->writeMbBytesRaw(startMb, bytes, &err)) { handlePlcRuntimeError(err); return; }
      QThread::msleep(50); plcRuntime_->writeMbBytesRaw(startMb, QByteArray(kCylinderCtrlBytes, '\0'), nullptr);
      if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写气缸控制：%1[%2] action=%3").arg(group).arg(index + 1).arg(action));
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
            this, [this] { core::PlcTrayPartIdBlockV2 tray; QString err; if (!plcRuntime_->readSecondStageTrayIds(&tray, &err)) handlePlcRuntimeError(err); });
    connect(productionWidget_, &ProductionWidget::requestAckMailbox,
            this, &MainWindow::handleAckMailboxRequested);
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
  }
  if (calibrationWidget_) {
    calibrationWidget_->setPlcConnected(stats.connected);
  }
  if (diagnosticsWidget_) {
    const int pollHz = (stats.poll_interval_ms > 0) ? (1000 / stats.poll_interval_ms) : 0;
    diagnosticsWidget_->setCommStats(pollHz, stats.last_poll_ms,
                                     stats.poll_ok_count, stats.poll_error_count);
  }
  if (devToolsWidget_ && hasLastStatus_) {
    devToolsWidget_->setPlcRuntimeSummary(stats.connected,
                                          machineStateText(lastStatus_.machine_state),
                                          stepStateText(lastStatus_.step_state),
                                          0,
                                          0);
  }
  if (!stats.connected && plcRuntime_ && plcRuntime_->config().plc.auto_reconnect && !reconnectAttemptLogged_) {
    appendProductionLog(QStringLiteral("尝试重新连接 PLC..."));
    reconnectAttemptLogged_ = true;
  }
  if (manualMaintainWidget_ && hasLastStatus_) {
    manualMaintainWidget_->setRuntimeSummary(stats.connected,
                                             machineStateText(lastStatus_.machine_state),
                                             stepStateText(lastStatus_.step_state));
    manualMaintainWidget_->setCurrentPlcMode(lastStatus_.control_mode);
  }
}

void MainWindow::onPlcStatusUpdated(const core::PlcStatusBlockV2 &status) {
  lastStatus_ = status;
  hasLastStatus_ = true;
  if (devToolsWidget_) {
    devToolsWidget_->setPlcRuntimeSummary(plcRuntime_ ? plcRuntime_->isConnected() : false,
                                          machineStateText(status.machine_state),
                                          stepStateText(status.step_state),
                                          0,
                                          0);
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
  QVector<quint16> axisRegs;
  if (plcRuntime_->readHoldingRegistersRaw(plcRuntime_->config().plc.axis_sta_start_address,
                                           static_cast<quint16>(kAxisStaRegsPerAxisV25 * 10),
                                           &axisRegs, &err)) {
    const QByteArray axisBytes = mbBytesFromRegsUi(axisRegs);
    QStringList lines;
    for (int axisIndex = 0; axisIndex < 10; ++axisIndex) {
      const int byteBase = axisIndex * static_cast<int>(kAxisStaBytesPerAxisV25);
      const auto u8 = [&](int off) -> int { return (byteBase + off < axisBytes.size()) ? static_cast<unsigned char>(axisBytes.at(byteBase + off)) : 0; };
      const auto u16le = [&](int off) -> int { return u8(off) | (u8(off + 1) << 8); };
      double pos = 0.0, vel = 0.0;
      core::plcReadFloat64WordSwappedAt(axisRegs, axisIndex * static_cast<int>(kAxisStaRegsPerAxisV25) + static_cast<int>(kAxisStaOffsetActPositionRegs), &pos, nullptr);
      core::plcReadFloat64WordSwappedAt(axisRegs, axisIndex * static_cast<int>(kAxisStaRegsPerAxisV25) + static_cast<int>(kAxisStaOffsetActVelocityRegs), &vel, nullptr);
      lines << QStringLiteral("%1 | En=%2 Homed=%3 Err=%4 Busy=%5 Done=%6 ErrId=%7 Pos=%8 Vel=%9")
                   .arg(axisNameByIndex(axisIndex))
                   .arg(u8(static_cast<int>(kAxisStaOffsetEnabled)))
                   .arg(u8(static_cast<int>(kAxisStaOffsetHomed)))
                   .arg(u8(static_cast<int>(kAxisStaOffsetError)))
                   .arg(u8(static_cast<int>(kAxisStaOffsetBusy)))
                   .arg(u8(static_cast<int>(kAxisStaOffsetDone)))
                   .arg(u16le(static_cast<int>(kAxisStaOffsetErrorId)))
                   .arg(pos, 0, 'f', 3)
                   .arg(vel, 0, 'f', 3);
    }
    manualMaintainWidget_->setAxisStatesText(lines.join('\n'));
  }

  QVector<quint16> cylRegs;
  if (plcRuntime_->readHoldingRegistersRaw(plcRuntime_->config().plc.lm_sta_start_address,
                                           static_cast<quint16>(12 + 3 + 4 + 5),
                                           &cylRegs, &err)) {
    const QByteArray cylBytes = mbBytesFromRegsUi(cylRegs);
    QStringList lines;
    auto addCyl = [&](int byteBase, const QString &name) {
      const auto u8 = [&](int off) -> int { return (byteBase + off < cylBytes.size()) ? static_cast<unsigned char>(cylBytes.at(byteBase + off)) : 0; };
      const int errId = u8(4) | (u8(5) << 8);
      lines << QStringLiteral("%1 | P=%2 N=%3 Err=%4 ErrId=%5").arg(name).arg(u8(0)).arg(u8(1)).arg(u8(2)).arg(errId);
    };
    addCyl(0, QStringLiteral("抓料气缸"));
    addCyl(6, QStringLiteral("内外径夹持"));
    addCyl(12, QStringLiteral("跳动夹持"));
    addCyl(18, QStringLiteral("长度夹持"));
    addCyl(24, QStringLiteral("GT2_1"));
    addCyl(30, QStringLiteral("GT2_2"));
    addCyl(36, QStringLiteral("GT2_3"));
    addCyl(42, QStringLiteral("GT2_4"));
    manualMaintainWidget_->setCylinderStatesText(lines.join('\n'));
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

  if (devToolsWidget_) {
    devToolsWidget_->appendPlcLog(QStringLiteral("Mailbox 已解析：part=%1 slot0=%2 slot1=%3")
                                      .arg(mailboxPartTypeText(snapshot), slot0, slot1));
  }

  if (plcFlowMode_ >= static_cast<int>(PlcFlowModeUi::FullAuto) &&
      hasLastStatus_ &&
      lastStatus_.step_state == static_cast<quint16>(core::PlcStepStateV2::WaitPcRead) &&
      snapshot.meas_seq != 0 && snapshot.meas_seq != lastAutoAckMeasSeq_) {
    handleAckMailboxRequested();
    lastAutoAckMeasSeq_ = snapshot.meas_seq;
    if (devToolsWidget_) {
      devToolsWidget_->appendPlcLog(QStringLiteral("自动写入 pc_ack：meas_seq=%1").arg(snapshot.meas_seq));
    }
  }
}

void MainWindow::onPlcEventsRaised(const core::PlcPollEventsV2 &events) {
  if (devToolsWidget_) {
    if (events.scan_ready) {
      devToolsWidget_->appendPlcLog(QStringLiteral("检测到新扫码结果：scan_seq=%1")
                                        .arg(0));
    }
    if (events.new_mailbox) {
      devToolsWidget_->appendPlcLog(QStringLiteral("检测到新测量包：meas_seq=%1，等待业务处理/ACK")
                                        .arg(0));
    }
  }

  if (!hasLastStatus_ || !plcRuntime_) {
    return;
  }

  if (plcFlowMode_ >= static_cast<int>(PlcFlowModeUi::SemiAuto) &&
      events.scan_ready &&
      lastStatus_.step_state == static_cast<quint16>(core::PlcStepStateV2::WaitPcIdCheck) &&
      lastStatus_.scan_done != 0 && lastStatus_.scan_done != lastAutoContinueScanSeq_) {
    handleUiCommandRequested(QStringLiteral("CONTINUE_AFTER_ID_CHECK"), QVariantMap{});
    lastAutoContinueScanSeq_ = lastStatus_.scan_done;
    if (devToolsWidget_) {
      devToolsWidget_->appendPlcLog(QStringLiteral("自动继续流程：scan_seq=%1")
                                        .arg(lastStatus_.scan_done));
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
  bool ok = false;
  const auto code = uiCommandToCode(cmd, &ok);
  if (!ok) {
    handlePlcRuntimeError(QStringLiteral("暂未映射的 PLC 命令：%1").arg(cmd));
    return;
  }

  qint16 plcMode = static_cast<qint16>(mapArg(args, QStringLiteral("plc_mode"), static_cast<quint32>(core::PlcControlModeV25::Manual)));
  if (cmd == QStringLiteral("SET_MODE_MANUAL")) plcMode = static_cast<qint16>(core::PlcControlModeV25::Manual);
  if (cmd == QStringLiteral("SET_MODE_AUTO")) plcMode = static_cast<qint16>(core::PlcControlModeV25::Auto);
  if (cmd == QStringLiteral("START_AUTO") || cmd == QStringLiteral("START_CALIBRATION")) plcMode = static_cast<qint16>(core::PlcControlModeV25::Auto);

  QString err;
  if (!plcRuntime_->writePlcMode(plcMode, &err)) {
    handlePlcRuntimeError(err);
    return;
  }

  if (cmd == QStringLiteral("SET_MODE_MANUAL") || cmd == QStringLiteral("SET_MODE_AUTO")) {
    if (devToolsWidget_) devToolsWidget_->appendPlcLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV25(plcMode)));
    if (manualMaintainWidget_) manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 模式：%1").arg(plcModeTextV25(plcMode)));
    plcRuntime_->pollOnce();
    return;
  }

  core::PlcCommandBlockV2 command;
  command.cmd_code = static_cast<quint16>(code);
  command.category_mode = static_cast<qint16>(mapArg(args, QStringLiteral("part_type_arg"), 0));
  command.cmd_arg0 = static_cast<quint32>(qMax(0, static_cast<int>(command.category_mode)));

  if (!plcRuntime_->sendCommand(command, &err)) {
    handlePlcRuntimeError(err);
    return;
  }
  if (devToolsWidget_) {
    devToolsWidget_->appendPlcLog(QStringLiteral("写 PLC 命令：%1 mode=%2 category=%3").arg(cmd).arg(plcMode).arg(command.category_mode));
  }
  if (manualMaintainWidget_) {
    manualMaintainWidget_->appendLog(QStringLiteral("写 PLC 命令：%1 mode=%2 category=%3").arg(cmd).arg(plcMode).arg(command.category_mode));
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

void MainWindow::handleReadMailboxRequested(QChar preferredPartType) {
  if (!plcRuntime_) return;
  core::PlcMailboxSnapshot snapshot;
  QString err;
  if (plcRuntime_->config().plc.first_stage_enabled) {
    if (!plcRuntime_->readFirstStageMailboxSnapshot(preferredPartType, &snapshot, &err)) {
      handlePlcRuntimeError(err);
      return;
    }
    if (devToolsWidget_) {
      devToolsWidget_->appendPlcLog(QStringLiteral("按第一阶段联调方式读取测量包：part=%1 item_count=%2")
                                        .arg(QString(snapshot.part_type), QString::number(snapshot.item_count)));
    }
    return;
  }
  if (!plcRuntime_->readSecondStageMailboxSnapshot(preferredPartType, &snapshot, &err)) {
    handlePlcRuntimeError(err);
    return;
  }
  if (devToolsWidget_) {
    devToolsWidget_->appendPlcLog(QStringLiteral("按第二阶段地址读取测量包：part=%1 item_count=%2")
                                      .arg(QString(snapshot.part_type), QString::number(snapshot.item_count)));
  }
}

void MainWindow::handleAckMailboxRequested() {
  if (!plcRuntime_) return;
  QString err;
  if (!plcRuntime_->sendPcAck(1, &err)) {
    handlePlcRuntimeError(err);
    return;
  }
  if (devToolsWidget_) {
    devToolsWidget_->appendPlcLog(QStringLiteral("手动写入 pc_ack=1"));
  }
}
