#include "dev_tools_widget.hpp"
#include "dev_tools.hpp"

#include <QDateTime>
#include <QMessageBox>
#include <QPushButton>

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
}

DevToolsWidget::~DevToolsWidget() { delete ui_; }

void DevToolsWidget::appendLog(const QString &text) {
  ui_->textLog->appendPlainText(text);
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
