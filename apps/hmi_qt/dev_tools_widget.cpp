#include "dev_tools_widget.hpp"
#include "dev_tools.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "core/db.hpp"
#include "core/measurement_ingest.hpp"
#include "ui_dev_tools_widget.h"

DevToolsWidget::DevToolsWidget(const core::AppConfig &cfg, QWidget *parent)
    : QWidget(parent), ui_(new Ui::DevToolsWidget), cfg_(cfg) {
  ui_->setupUi(this);
  connect(ui_->btnInsertA, &QPushButton::clicked, this,
          &DevToolsWidget::onInsertATest);
  connect(ui_->btnInsertB, &QPushButton::clicked, this,
          &DevToolsWidget::onInsertBTest);
  connect(ui_->btnSmoke, &QPushButton::clicked, this,
          &DevToolsWidget::onRunSmoke);
  connect(ui_->btnQueryLatest, &QPushButton::clicked, this,
          &DevToolsWidget::onQueryLatest);
  connect(ui_->btnClearLog, &QPushButton::clicked, this,
          &DevToolsWidget::onClearLog);

  auto *plcBox = new QGroupBox(QStringLiteral("PLC联调 / 自动流程"), this);
  auto *plcLay = new QVBoxLayout(plcBox);

  auto *modeLay = new QHBoxLayout();
  modeLay->addWidget(new QLabel(QStringLiteral("流程模式："), plcBox));
  plcFlowCombo_ = new QComboBox(plcBox);
  plcFlowCombo_->addItem(QStringLiteral("手动（只监听，不自动推进）"), static_cast<int>(PlcFlowModeUi::Manual));
  plcFlowCombo_->addItem(QStringLiteral("半自动（扫码后自动继续，不自动ACK）"), static_cast<int>(PlcFlowModeUi::SemiAuto));
  plcFlowCombo_->addItem(QStringLiteral("全自动（扫码后自动继续，读包后自动ACK）"), static_cast<int>(PlcFlowModeUi::FullAuto));
  modeLay->addWidget(plcFlowCombo_, 1);
  plcLay->addLayout(modeLay);

  auto *summaryLay = new QHBoxLayout();
  lbPlcConn_ = new QLabel(QStringLiteral("连接：-"), plcBox);
  lbPlcMachine_ = new QLabel(QStringLiteral("机器：-"), plcBox);
  lbPlcStep_ = new QLabel(QStringLiteral("步骤：-"), plcBox);
  lbPlcSeq_ = new QLabel(QStringLiteral("seq(scan/meas)：-/-"), plcBox);
  summaryLay->addWidget(lbPlcConn_);
  summaryLay->addWidget(lbPlcMachine_);
  summaryLay->addWidget(lbPlcStep_);
  summaryLay->addWidget(lbPlcSeq_, 1);
  plcLay->addLayout(summaryLay);

  auto *btnLay1 = new QHBoxLayout();
  btnPlcPoll_ = new QPushButton(QStringLiteral("轮询一拍"), plcBox);
  btnPlcReloadIds_ = new QPushButton(QStringLiteral("读取扫码ID"), plcBox);
  btnPlcContinue_ = new QPushButton(QStringLiteral("继续流程(ID核对通过)"), plcBox);
  btnLay1->addWidget(btnPlcPoll_);
  btnLay1->addWidget(btnPlcReloadIds_);
  btnLay1->addWidget(btnPlcContinue_);
  plcLay->addLayout(btnLay1);

  auto *btnLay2 = new QHBoxLayout();
  btnPlcRescan_ = new QPushButton(QStringLiteral("请求重扫ID"), plcBox);
  btnPlcReadMailbox_ = new QPushButton(QStringLiteral("读取测量包"), plcBox);
  btnPlcAck_ = new QPushButton(QStringLiteral("写 ACK(pc_ack)"), plcBox);
  btnLay2->addWidget(btnPlcRescan_);
  btnLay2->addWidget(btnPlcReadMailbox_);
  btnLay2->addWidget(btnPlcAck_);
  plcLay->addLayout(btnLay2);

  ui_->verticalLayout->insertWidget(0, plcBox);

  connect(plcFlowCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index){
    const int mode = plcFlowCombo_->itemData(index).toInt();
    refreshPlcActionEnableStates();
    emit plcFlowModeChanged(mode);
    appendPlcLog(QStringLiteral("PLC流程模式切换为：%1").arg(plcFlowModeText(mode)));
  });
  connect(btnPlcPoll_, &QPushButton::clicked, this, &DevToolsWidget::requestPlcPollOnce);
  connect(btnPlcReloadIds_, &QPushButton::clicked, this, &DevToolsWidget::requestPlcReloadSlotIds);
  connect(btnPlcContinue_, &QPushButton::clicked, this, &DevToolsWidget::requestPlcContinueAfterIdCheck);
  connect(btnPlcRescan_, &QPushButton::clicked, this, &DevToolsWidget::requestPlcRequestRescanIds);
  connect(btnPlcReadMailbox_, &QPushButton::clicked, this, &DevToolsWidget::requestPlcReadMailbox);
  connect(btnPlcAck_, &QPushButton::clicked, this, &DevToolsWidget::requestPlcAckMailbox);

  setPlcFlowMode(static_cast<int>(PlcFlowModeUi::Manual));
}

DevToolsWidget::~DevToolsWidget() { delete ui_; }

void DevToolsWidget::appendLog(const QString &text) {
  ui_->textLog->appendPlainText(text);
}

QString DevToolsWidget::plcFlowModeText(int mode) const {
  switch (static_cast<PlcFlowModeUi>(mode)) {
  case PlcFlowModeUi::Manual:
    return QStringLiteral("手动");
  case PlcFlowModeUi::SemiAuto:
    return QStringLiteral("半自动");
  case PlcFlowModeUi::FullAuto:
    return QStringLiteral("全自动");
  default:
    return QStringLiteral("未知");
  }
}

void DevToolsWidget::setPlcFlowMode(int mode) {
  if (!plcFlowCombo_) return;
  for (int i = 0; i < plcFlowCombo_->count(); ++i) {
    if (plcFlowCombo_->itemData(i).toInt() == mode) {
      const QSignalBlocker blocker(plcFlowCombo_);
      plcFlowCombo_->setCurrentIndex(i);
      break;
    }
  }
  refreshPlcActionEnableStates();
}

void DevToolsWidget::refreshPlcActionEnableStates() {
  const int mode = plcFlowCombo_ ? plcFlowCombo_->currentData().toInt() : static_cast<int>(PlcFlowModeUi::Manual);
  const bool isManual = (mode == static_cast<int>(PlcFlowModeUi::Manual));
  const bool isSemi = (mode == static_cast<int>(PlcFlowModeUi::SemiAuto));
  const bool isFull = (mode == static_cast<int>(PlcFlowModeUi::FullAuto));

  if (btnPlcPoll_) btnPlcPoll_->setEnabled(!isFull);
  if (btnPlcReloadIds_) btnPlcReloadIds_->setEnabled(isManual || isSemi);
  if (btnPlcContinue_) btnPlcContinue_->setEnabled(isManual || isSemi);
  if (btnPlcRescan_) btnPlcRescan_->setEnabled(isManual || isSemi);
  if (btnPlcReadMailbox_) btnPlcReadMailbox_->setEnabled(isManual || isSemi);
  if (btnPlcAck_) btnPlcAck_->setEnabled(isManual || isSemi);
}

void DevToolsWidget::setPlcRuntimeSummary(bool connected, const QString &machineText,
                                         const QString &stepText, quint32 scanSeq,
                                         quint32 measSeq) {
  if (lbPlcConn_) lbPlcConn_->setText(QStringLiteral("连接：%1").arg(connected ? QStringLiteral("已连接") : QStringLiteral("未连接")));
  if (lbPlcMachine_) lbPlcMachine_->setText(QStringLiteral("机器：%1").arg(machineText.isEmpty() ? QStringLiteral("-") : machineText));
  if (lbPlcStep_) lbPlcStep_->setText(QStringLiteral("步骤：%1").arg(stepText.isEmpty() ? QStringLiteral("-") : stepText));
  if (lbPlcSeq_) lbPlcSeq_->setText(QStringLiteral("seq(scan/meas)：%1/%2").arg(scanSeq).arg(measSeq));
}

void DevToolsWidget::appendPlcLog(const QString &text) {
  appendLog(QStringLiteral("[PLC] %1").arg(text));
}

bool DevToolsWidget::insertViaIngest(const QString &partType,
                                     const QString &partId, QString *err) {
  core::Db db;
  QString dberr;
  if (!db.open(cfg_.db, &dberr)) {
    if (err)
      *err = dberr;
    return false;
  }
  if (!db.ensureSchema(&dberr)) {
    if (err)
      *err = dberr;
    return false;
  }

  core::MeasurementIngestRequest req;
  req.cycle.meas_seq = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
  req.cycle.part_type = partType;
  req.cycle.item_count = 1;
  req.cycle.source_mode = "AUTO";
  req.cycle.measured_at_utc = QDateTime::currentDateTimeUtc();
  req.cycle.mailbox_header_json =
      QString(R"({"source":"dev_tools","part_type":"%1","item_count":1})")
          .arg(partType);
  req.cycle.mailbox_meta_json = R"({"note":"dev tools page ingest"})";

  core::IngestItemInput item;
  item.item_index = 0;
  item.slot_index = (partType == "A") ? QVariant(1) : QVariant(2);
  item.slot_id = (partType == "A") ? "DEV-SLOT-A" : "DEV-SLOT-B";
  item.part_id = partId;
  item.result_ok = 1;
  item.measure_mode = "NORMAL";
  item.measure_round = 1;
  item.result_judgement = "OK";
  item.upload_kind = "FIRST_MEASURE";
  item.operator_id = "dev_tools";
  item.review_status = "PENDING";
  item.status = "READY";
  req.items.push_back(item);

  core::IngestResultInput result;
  if (partType == "A") {
    result.total_len_mm = 123.456;
    result.id_left_mm = 16.111;
    result.id_right_mm = 15.999;
    result.od_left_mm = 20.333;
    result.od_right_mm = 19.888;
    result.extra_json = R"({"source":"dev_tools","kind":"A"})";
  } else {
    result.ad_len_mm = 88.123;
    result.bc_len_mm = 33.456;
    result.runout_left_mm = 0.012;
    result.runout_right_mm = 0.018;
    result.extra_json = R"({"source":"dev_tools","kind":"B"})";
  }
  result.tolerance_json = "{}";
  req.results.push_back(result);

  core::IngestRawInput raw;
  raw.enabled = false;
  req.raws.push_back(raw);

  core::IngestReportInput report;
  report.create_mes_report = false;
  req.reports.push_back(report);

  core::MeasurementIngestResponse resp;
  core::MeasurementIngestService svc(db);
  QString ingestErr;
  if (!svc.ingest(req, &resp, &ingestErr)) {
    if (err)
      *err = ingestErr;
    return false;
  }

  appendLog(QStringLiteral("写入成功: part=%1, cycle_id=%2, item_count=%3")
                .arg(partId)
                .arg(resp.plc_cycle_id)
                .arg(resp.items.size()));
  return true;
}

void DevToolsWidget::onInsertATest() {
  QString err;
  const QString partId =
      QString("A-DEV-%1").arg(QDateTime::currentSecsSinceEpoch());
  if (!insertViaIngest("A", partId, &err)) {
    QMessageBox::warning(this, QStringLiteral("开发调试"),
                         QStringLiteral("插入A型测试数据失败:\n%1").arg(err));
    appendLog(QStringLiteral("A型写入失败: %1").arg(err));
    return;
  }
}

void DevToolsWidget::onInsertBTest() {
  QString err;
  const QString partId =
      QString("B-DEV-%1").arg(QDateTime::currentSecsSinceEpoch());
  if (!insertViaIngest("B", partId, &err)) {
    QMessageBox::warning(this, QStringLiteral("开发调试"),
                         QStringLiteral("插入B型测试数据失败:\n%1").arg(err));
    appendLog(QStringLiteral("B型写入失败: %1").arg(err));
    return;
  }
}

void DevToolsWidget::onRunSmoke() {
  core::Db db;
  QString err;
  if (!db.open(cfg_.db, &err)) {
    QMessageBox::warning(this, QStringLiteral("开发调试"), err);
    appendLog(QStringLiteral("DB打开失败: %1").arg(err));
    return;
  }
  if (!db.ensureSchema(&err)) {
    QMessageBox::warning(this, QStringLiteral("开发调试"), err);
    appendLog(QStringLiteral("Schema失败: %1").arg(err));
    return;
  }
  QString smokeErr;
  if (!runDbSmokeTestNewSchema(db, &smokeErr)) {
    QMessageBox::warning(this, QStringLiteral("开发调试"),
                         QStringLiteral("数据库冒烟测试失败:\n%1").arg(smokeErr));
    appendLog(QStringLiteral("Smoke失败: %1").arg(smokeErr));
    return;
  }
  appendLog(QStringLiteral("数据库冒烟测试成功"));
}

void DevToolsWidget::onQueryLatest() {
  core::Db db;
  QString err;
  if (!db.open(cfg_.db, &err) || !db.ensureSchema(&err)) {
    QMessageBox::warning(this, QStringLiteral("开发调试"), err);
    appendLog(QStringLiteral("查询准备失败: %1").arg(err));
    return;
  }
  const auto rows = db.queryLatestMeasurementsEx(10, &err);
  if (!err.isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("开发调试"), err);
    appendLog(QStringLiteral("查询失败: %1").arg(err));
    return;
  }
  appendLog(QStringLiteral("最新 %1 条 measurement:").arg(rows.size()));
  for (const auto &r : rows) {
    appendLog(QStringLiteral("  #%1 %2 %3 %4 round=%5 judge=%6")
                  .arg(r.measurement_id)
                  .arg(r.part_id)
                  .arg(r.part_type)
                  .arg(r.measure_mode)
                  .arg(r.measure_round)
                  .arg(r.result_judgement));
  }
}

void DevToolsWidget::onClearLog() { ui_->textLog->clear(); }
