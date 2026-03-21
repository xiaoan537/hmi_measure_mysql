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

#include <QSizePolicy>

#include "ui_mainwindow.h"

#include <cmath>
#include <memory>

#include "core/measurement_pipeline.hpp"
#include "core/plc_contract_v2.hpp"
#include "core/plc_fake_client_v2.hpp"
#include "core/plc_runtime_v2.hpp"

MainWindow::MainWindow(const core::AppConfig &cfg, const QString &iniPath,
                       MesWorker *worker, QWidget *parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow), iniPath_(iniPath) {
  ui_->setupUi(this);

  // MainWindow ctor, setupUi 后
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

  ui_->stackedWidget->addWidget(new ProductionWidget(cfg, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(new CalibrationWidget(cfg, ui_->stackedWidget));
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
}

MainWindow::~MainWindow() {
  if (plcRuntime_) {
    plcRuntime_->stop();
  }
  delete ui_;
}


namespace {
QString mailboxPartTypeText(const core::PlcMailboxSnapshot &snapshot) {
  const QChar pt = snapshot.part_type.toUpper();
  if (pt == QChar('A') || pt == QChar('B')) {
    return QString(pt);
  }
  return QStringLiteral("-");
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

void MainWindow::setupPlcRuntime(const core::AppConfig &cfg) {
  plcRuntime_ = std::make_unique<core::PlcRuntimeServiceV2>(cfg, this);
  updatePlcStatusLabel();

  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::connectionChanged,
          this, [this](bool) { updatePlcStatusLabel(); });
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::errorOccurred,
          this, &MainWindow::handlePlcRuntimeError);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::statsUpdated,
          this, &MainWindow::onPlcStatsUpdated);
  connect(plcRuntime_.get(), &core::PlcRuntimeServiceV2::statusUpdated,
          this, &MainWindow::onPlcStatusUpdated);
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
          this, [this]() {
            if (!plcRuntime_) return;
            QString err;
            if (!plcRuntime_->sendPcAck(1, &err)) {
              handlePlcRuntimeError(err);
            }
          });
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
}

void MainWindow::onPlcMailboxSnapshotUpdated(const core::PlcMailboxSnapshot &snapshot) {
  if (!diagnosticsWidget_) {
    return;
  }
  QString slot0 = QStringLiteral("-");
  QString slot1 = QStringLiteral("-");
  QString partId0;
  QString partId1;

  if (!snapshot.items.isEmpty()) {
    slot0 = QString::number(snapshot.items.at(0).slot_index);
    partId0 = snapshot.items.at(0).part_id;
  }
  if (snapshot.items.size() > 1) {
    slot1 = QString::number(snapshot.items.at(1).slot_index);
    partId1 = snapshot.items.at(1).part_id;
  }

  diagnosticsWidget_->setMailboxPreview(mailboxPartTypeText(snapshot), slot0, slot1, partId0, partId1);
}
