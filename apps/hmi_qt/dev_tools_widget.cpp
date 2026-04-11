#include "dev_tools_widget.hpp"
#include "dev_tools.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>
#include <QtMath>

#include "core/db.hpp"
#include "core/measurement_geometry_algorithms.hpp"
#include "core/measurement_ingest.hpp"
#include "ui_dev_tools_widget.h"

namespace {

QString joinBoolMask(const QVector<bool> &mask) {
  QStringList parts;
  parts.reserve(mask.size());
  for (bool v : mask) {
    parts.push_back(v ? QStringLiteral("1") : QStringLiteral("0"));
  }
  return parts.join(',');
}

QString joinDoubleList(const QVector<double> &values) {
  QStringList parts;
  parts.reserve(values.size());
  for (double v : values) {
    parts.push_back(QString::number(v, 'f', 6));
  }
  return parts.join(',');
}

} // namespace

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

  // 手动/维护内容已迁移到“手动/维护”页面，开发调试页仅保留联调与算法调试内容。

  // ui_->verticalLayout->insertWidget(1, algoBox);

  auto *runoutBox = new QGroupBox(QStringLiteral("跳动算法调试 / 回放"), this);
  auto *runoutLay = new QVBoxLayout(runoutBox);

  auto *runoutBtnLay = new QHBoxLayout();
  btnRunoutFillExample_ = new QPushButton(QStringLiteral("填充跳动示例"), runoutBox);
  btnRunoutRun_ = new QPushButton(QStringLiteral("运行跳动算法"), runoutBox);
  runoutBtnLay->addWidget(btnRunoutFillExample_);
  runoutBtnLay->addWidget(btnRunoutRun_);
  runoutBtnLay->addStretch(1);
  runoutLay->addLayout(runoutBtnLay);

  auto *runoutParamForm = new QFormLayout();
  spRunoutK_ = new QDoubleSpinBox(runoutBox);
  spRunoutK_->setDecimals(6); spRunoutK_->setRange(-1000000.0, 1000000.0); spRunoutK_->setValue(20.0);
  spRunoutAngleOffset_ = new QDoubleSpinBox(runoutBox);
  spRunoutAngleOffset_->setDecimals(3); spRunoutAngleOffset_->setRange(-360.0, 360.0);
  spRunoutResidual_ = new QDoubleSpinBox(runoutBox);
  spRunoutResidual_->setDecimals(6); spRunoutResidual_->setRange(0.0, 1000.0); spRunoutResidual_->setValue(0.03);
  spRunoutVAngle_ = new QDoubleSpinBox(runoutBox);
  spRunoutVAngle_->setDecimals(3); spRunoutVAngle_->setRange(1.0, 179.0); spRunoutVAngle_->setValue(90.0);
  spRunoutInterp_ = new QSpinBox(runoutBox);
  spRunoutInterp_->setRange(1, 50); spRunoutInterp_->setValue(5);
  cbRunoutPrimary_ = new QComboBox(runoutBox);
  cbRunoutPrimary_->addItem(QStringLiteral("同时显示（推荐）"), 0);
  cbRunoutPrimary_->addItem(QStringLiteral("V型块等效跳动优先"), 1);
  cbRunoutPrimary_->addItem(QStringLiteral("拟合圆残差峰峰值优先"), 2);
  runoutParamForm->addRow(QStringLiteral("K_runout(mm)"), spRunoutK_);
  runoutParamForm->addRow(QStringLiteral("角度偏移(°)"), spRunoutAngleOffset_);
  runoutParamForm->addRow(QStringLiteral("拟合残差阈值(mm)"), spRunoutResidual_);
  runoutParamForm->addRow(QStringLiteral("V型块夹角(°)"), spRunoutVAngle_);
  runoutParamForm->addRow(QStringLiteral("轮廓插值倍率"), spRunoutInterp_);
  runoutParamForm->addRow(QStringLiteral("主显示算法"), cbRunoutPrimary_);
  runoutLay->addLayout(runoutParamForm);

  auto *runoutSeriesLay = new QHBoxLayout();
  auto *runoutSeriesBox = new QGroupBox(QStringLiteral("跳动输入 m_runout"), runoutBox);
  auto *runoutSeriesBoxLay = new QVBoxLayout(runoutSeriesBox);
  runoutSeriesBoxLay->addWidget(new QLabel(QStringLiteral("原始值（逗号/空格/换行分隔）"), runoutSeriesBox));
  teRunoutRaw_ = new QPlainTextEdit(runoutSeriesBox);
  teRunoutRaw_->setPlaceholderText(QStringLiteral("例如：3.201, 3.198, 3.205, ... 共72点"));
  runoutSeriesBoxLay->addWidget(teRunoutRaw_, 1);
  runoutSeriesBoxLay->addWidget(new QLabel(QStringLiteral("有效mask（可空；1/0 或 true/false）"), runoutSeriesBox));
  teRunoutValid_ = new QPlainTextEdit(runoutSeriesBox);
  teRunoutValid_->setPlaceholderText(QStringLiteral("可留空，默认全1；例如：1,1,1,0,1,..."));
  teRunoutValid_->setMaximumHeight(70);
  runoutSeriesBoxLay->addWidget(teRunoutValid_);
  runoutSeriesLay->addWidget(runoutSeriesBox, 1);
  runoutLay->addLayout(runoutSeriesLay);

  ui_->verticalLayout->insertWidget(2, runoutBox);

  connect(btnRunoutFillExample_, &QPushButton::clicked, this, &DevToolsWidget::onFillRunoutExample);
  connect(btnRunoutRun_, &QPushButton::clicked, this, &DevToolsWidget::onRunRunoutFromInput);

  connect(cbAlgoUseExplicitKOut_, &QCheckBox::toggled, spAlgoKOut_, &QWidget::setEnabled);
  connect(btnAlgoLoadJson_, &QPushButton::clicked, this, &DevToolsWidget::onLoadAlgorithmJson);
  connect(btnAlgoRun_, &QPushButton::clicked, this, &DevToolsWidget::onRunAlgorithmFromInput);
  connect(btnAlgoFillExample_, &QPushButton::clicked, this, &DevToolsWidget::onFillAlgorithmExample);

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

bool DevToolsWidget::parseDoubleSeriesText(const QString &text,
                                           QVector<double> *values,
                                           QString *err) const {
  values->clear();
  QString normalized = text;
  normalized.replace('\n', ',');
  normalized.replace('\r', ',');
  normalized.replace(';', ',');
  normalized.replace('\t', ',');
  normalized.replace(' ', ',');
  const QStringList parts = normalized.split(',', Qt::SkipEmptyParts);
  for (int i = 0; i < parts.size(); ++i) {
    bool ok = false;
    const double v = parts.at(i).trimmed().toDouble(&ok);
    if (!ok) {
      if (err) *err = QStringLiteral("第 %1 个数值无法解析：%2").arg(i + 1).arg(parts.at(i));
      values->clear();
      return false;
    }
    values->push_back(v);
  }
  return true;
}

bool DevToolsWidget::parseBoolSeriesText(const QString &text, int expectedSize,
                                         QVector<bool> *values, QString *err) const {
  values->clear();
  if (text.trimmed().isEmpty()) {
    *values = defaultValidMask(expectedSize);
    return true;
  }
  QString normalized = text;
  normalized.replace('\n', ',');
  normalized.replace('\r', ',');
  normalized.replace(';', ',');
  normalized.replace('\t', ',');
  normalized.replace(' ', ',');
  const QStringList parts = normalized.split(',', Qt::SkipEmptyParts);
  if (parts.size() != expectedSize) {
    if (err) *err = QStringLiteral("有效mask数量(%1)与数据点数量(%2)不一致").arg(parts.size()).arg(expectedSize);
    return false;
  }
  for (int i = 0; i < parts.size(); ++i) {
    const QString token = parts.at(i).trimmed().toLower();
    const bool isTrue = (token == QStringLiteral("1") || token == QStringLiteral("true") || token == QStringLiteral("t") || token == QStringLiteral("y") || token == QStringLiteral("yes"));
    const bool isFalse = (token == QStringLiteral("0") || token == QStringLiteral("false") || token == QStringLiteral("f") || token == QStringLiteral("n") || token == QStringLiteral("no"));
    if (!isTrue && !isFalse) {
      if (err) *err = QStringLiteral("第 %1 个有效mask无法解析：%2").arg(i + 1).arg(parts.at(i));
      values->clear();
      return false;
    }
    values->push_back(isTrue);
  }
  return true;
}

QVector<bool> DevToolsWidget::defaultValidMask(int count) const {
  QVector<bool> mask;
  mask.resize(count);
  for (int i = 0; i < count; ++i) {
    mask[i] = true;
  }
  return mask;
}

QString DevToolsWidget::summarizeCircleFit(const QString &title,
                                           const core::DiameterChannelResult &r) const {
  QStringList lines;
  lines << QStringLiteral("[%1]").arg(title);
  if (!r.success || !r.circle_fit.success) {
    lines << QStringLiteral("  失败：%1").arg(r.error.isEmpty() ? r.circle_fit.error : r.error);
    return lines.join('\n');
  }
  lines << QStringLiteral("  直径 = %1 mm").arg(r.circle_fit.diameter_mm, 0, 'f', 6);
  lines << QStringLiteral("  半径 = %1 mm").arg(r.circle_fit.radius_mm, 0, 'f', 6);
  lines << QStringLiteral("  圆心 = (%1, %2) mm")
              .arg(r.circle_fit.center_x_mm, 0, 'f', 6)
              .arg(r.circle_fit.center_y_mm, 0, 'f', 6);
  lines << QStringLiteral("  残差RMS = %1 mm, 最大绝对残差 = %2 mm")
              .arg(r.circle_fit.residual_rms_mm, 0, 'f', 6)
              .arg(r.circle_fit.residual_max_abs_mm, 0, 'f', 6);
  lines << QStringLiteral("  点数(raw/final/reject) = %1 / %2 / %3")
              .arg(r.circle_fit.valid_count_raw)
              .arg(r.circle_fit.valid_count_final)
              .arg(r.circle_fit.rejected_count);
  return lines.join('\n');
}

QString DevToolsWidget::summarizeThickness(const core::ThicknessResult &r) const {
  QStringList lines;
  lines << QStringLiteral("[壁厚]");
  if (!r.success) {
    lines << QStringLiteral("  失败：%1").arg(r.error);
    return lines.join('\n');
  }
  lines << QStringLiteral("  平均 = %1 mm").arg(r.mean_mm, 0, 'f', 6);
  lines << QStringLiteral("  最小/最大 = %1 / %2 mm")
              .arg(r.min_mm, 0, 'f', 6)
              .arg(r.max_mm, 0, 'f', 6);
  lines << QStringLiteral("  标准差 = %1 mm, 有效点数 = %2")
              .arg(r.stddev_mm, 0, 'f', 6)
              .arg(r.valid_count);
  return lines.join('\n');
}

QString DevToolsWidget::summarizeHarmonics(const QString &title,
                                           const core::HarmonicAnalysisResult &r) const {
  QStringList lines;
  lines << QStringLiteral("[%1 谐波]").arg(title);
  if (!r.success) {
    lines << QStringLiteral("  失败：%1").arg(r.error);
    return lines.join('\n');
  }
  lines << QStringLiteral("  DC均值 = %1 mm").arg(r.dc_mean_mm, 0, 'f', 6);
  for (const auto &c : r.components) {
    if (c.order > 4) break;
    lines << QStringLiteral("  %1X: 幅值=%2 mm, 相位=%3 rad")
                .arg(c.order)
                .arg(c.amplitude_mm, 0, 'f', 6)
                .arg(c.phase_rad, 0, 'f', 6);
  }
  return lines.join('\n');
}

QString DevToolsWidget::summarizeRunout(const core::RunoutResult &r, int primaryMode) const {
  QStringList lines;
  lines << QStringLiteral("[跳动]");
  if (!r.success) {
    lines << QStringLiteral("  失败：%1").arg(r.error);
    return lines.join('\n');
  }
  const QString primaryText = (primaryMode == 1)
      ? QStringLiteral("V型块等效跳动")
      : (primaryMode == 2 ? QStringLiteral("拟合圆残差峰峰值")
                          : QStringLiteral("同时显示"));
  lines << QStringLiteral("  主显示算法 = %1").arg(primaryText);
  lines << QStringLiteral("  设备轴参考TIR = %1 mm (max=%2 @ %3°, min=%4 @ %5°)")
              .arg(r.tir_axis_mm, 0, 'f', 6)
              .arg(r.max_radius_mm, 0, 'f', 6)
              .arg(r.max_angle_deg, 0, 'f', 3)
              .arg(r.min_radius_mm, 0, 'f', 6)
              .arg(r.min_angle_deg, 0, 'f', 3);
  lines << QStringLiteral("  V型块等效跳动 = %1 mm").arg(r.runout_vblock_mm, 0, 'f', 6);
  lines << QStringLiteral("  拟合圆残差峰峰值 = %1 mm").arg(r.fit_residual_peak_to_peak_mm, 0, 'f', 6);
  lines << QStringLiteral("  拟合残差RMS = %1 mm").arg(r.fit_residual_rms_mm, 0, 'f', 6);
  if (r.circle_fit.success) {
    lines << QStringLiteral("  拟合圆心 = (%1, %2) mm, 半径 = %3 mm")
                .arg(r.circle_fit.center_x_mm, 0, 'f', 6)
                .arg(r.circle_fit.center_y_mm, 0, 'f', 6)
                .arg(r.circle_fit.radius_mm, 0, 'f', 6);
  }
  return lines.join('\n');
}

void DevToolsWidget::loadAlgorithmJsonObject(const QJsonObject &obj) {
  auto readNumberArray = [](const QJsonObject &o, const QString &key) {
    QVector<double> out;
    const auto arr = o.value(key).toArray();
    out.reserve(arr.size());
    for (const auto &v : arr) out.push_back(v.toDouble());
    return out;
  };
  auto readBoolArray = [](const QJsonObject &o, const QString &key) {
    QVector<bool> out;
    const auto arr = o.value(key).toArray();
    out.reserve(arr.size());
    for (const auto &v : arr) out.push_back(v.toBool());
    return out;
  };

  const QVector<double> mIn = readNumberArray(obj, QStringLiteral("m_in"));
  const QVector<double> mOut = readNumberArray(obj, QStringLiteral("m_out"));
  const QVector<bool> validIn = readBoolArray(obj, QStringLiteral("valid_in"));
  const QVector<bool> validOut = readBoolArray(obj, QStringLiteral("valid_out"));
  const QVector<double> mRunout = readNumberArray(obj, QStringLiteral("m_runout"));
  const QVector<bool> validRunout = readBoolArray(obj, QStringLiteral("valid_runout"));

  if (!mIn.isEmpty() && teAlgoInnerRaw_) teAlgoInnerRaw_->setPlainText(joinDoubleList(mIn));
  if (!mOut.isEmpty() && teAlgoOuterRaw_) teAlgoOuterRaw_->setPlainText(joinDoubleList(mOut));
  if (!validIn.isEmpty() && teAlgoInnerValid_) teAlgoInnerValid_->setPlainText(joinBoolMask(validIn));
  if (!validOut.isEmpty() && teAlgoOuterValid_) teAlgoOuterValid_->setPlainText(joinBoolMask(validOut));
  if (!mRunout.isEmpty() && teRunoutRaw_) teRunoutRaw_->setPlainText(joinDoubleList(mRunout));
  if (!validRunout.isEmpty() && teRunoutValid_) teRunoutValid_->setPlainText(joinBoolMask(validRunout));

  const auto params = obj.value(QStringLiteral("params")).toObject();
  if (!params.isEmpty()) {
    if (spAlgoKIn_) spAlgoKIn_->setValue(params.value(QStringLiteral("k_in_mm")).toDouble(spAlgoKIn_->value()));
    if (spAlgoKOut_) spAlgoKOut_->setValue(params.value(QStringLiteral("k_out_mm")).toDouble(spAlgoKOut_->value()));
    if (spAlgoProbeBase_) spAlgoProbeBase_->setValue(params.value(QStringLiteral("probe_base_mm")).toDouble(spAlgoProbeBase_->value()));
    if (spAlgoAngleOffset_) spAlgoAngleOffset_->setValue(params.value(QStringLiteral("angle_offset_deg")).toDouble(spAlgoAngleOffset_->value()));
    if (spAlgoResidualIn_) spAlgoResidualIn_->setValue(params.value(QStringLiteral("residual_threshold_in_mm")).toDouble(spAlgoResidualIn_->value()));
    if (spAlgoResidualOut_) spAlgoResidualOut_->setValue(params.value(QStringLiteral("residual_threshold_out_mm")).toDouble(spAlgoResidualOut_->value()));
    if (cbAlgoUseExplicitKOut_) cbAlgoUseExplicitKOut_->setChecked(params.value(QStringLiteral("use_explicit_k_out")).toBool(cbAlgoUseExplicitKOut_->isChecked()));
    if (spRunoutK_) spRunoutK_->setValue(params.value(QStringLiteral("k_runout_mm")).toDouble(spRunoutK_->value()));
    if (spRunoutAngleOffset_) spRunoutAngleOffset_->setValue(params.value(QStringLiteral("runout_angle_offset_deg")).toDouble(spRunoutAngleOffset_->value()));
    if (spRunoutResidual_) spRunoutResidual_->setValue(params.value(QStringLiteral("runout_residual_threshold_mm")).toDouble(spRunoutResidual_->value()));
    if (spRunoutVAngle_) spRunoutVAngle_->setValue(params.value(QStringLiteral("v_block_angle_deg")).toDouble(spRunoutVAngle_->value()));
    if (spRunoutInterp_) spRunoutInterp_->setValue(params.value(QStringLiteral("runout_interpolation_factor")).toInt(spRunoutInterp_->value()));
  }
}

void DevToolsWidget::onLoadAlgorithmJson() {
  const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("加载算法调试JSON"), QString(), QStringLiteral("JSON Files (*.json);;All Files (*)"));
  if (path.isEmpty()) {
    return;
  }
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, QStringLiteral("算法调试"), QStringLiteral("无法打开文件：%1").arg(path));
    return;
  }
  QJsonParseError parseErr;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseErr);
  if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
    QMessageBox::warning(this, QStringLiteral("算法调试"), QStringLiteral("JSON解析失败：%1").arg(parseErr.errorString()));
    return;
  }
  loadAlgorithmJsonObject(doc.object());
  appendLog(QStringLiteral("[ALG] 已加载JSON：%1").arg(path));
}

void DevToolsWidget::onFillAlgorithmExample() {
  QVector<double> mIn;
  QVector<double> mOut;
  mIn.reserve(72);
  mOut.reserve(72);
  for (int i = 0; i < 72; ++i) {
    const double theta = qDegreesToRadians(static_cast<double>(i) * 5.0);
    const double inVal = 1.200 + 0.060 * qCos(theta - 0.25) + 0.008 * qCos(2.0 * theta + 0.6);
    const double outVal = 2.950 - 0.080 * qCos(theta - 0.25) + 0.006 * qCos(3.0 * theta - 0.3);
    mIn.push_back(inVal);
    mOut.push_back(outVal);
  }
  if (mIn.size() > 17) mIn[17] += 0.080;
  if (mOut.size() > 43) mOut[43] -= 0.100;

  if (teAlgoInnerRaw_) teAlgoInnerRaw_->setPlainText(joinDoubleList(mIn));
  if (teAlgoOuterRaw_) teAlgoOuterRaw_->setPlainText(joinDoubleList(mOut));
  if (teAlgoInnerValid_) teAlgoInnerValid_->clear();
  if (teAlgoOuterValid_) teAlgoOuterValid_->clear();
  if (spAlgoKIn_) spAlgoKIn_->setValue(8.000000);
  if (cbAlgoUseExplicitKOut_) cbAlgoUseExplicitKOut_->setChecked(true);
  if (spAlgoProbeBase_) spAlgoProbeBase_->setValue(15.000000);
  if (spAlgoResidualIn_) spAlgoResidualIn_->setValue(0.03);
  if (spAlgoResidualOut_) spAlgoResidualOut_->setValue(0.03);
  appendLog(QStringLiteral("[ALG] 已填充示例数据（72点，含少量谐波与异常点，默认显式K_out）"));
}

void DevToolsWidget::onFillRunoutExample() {
  QVector<double> m;
  m.reserve(72);
  for (int i = 0; i < 72; ++i) {
    const double theta = qDegreesToRadians(static_cast<double>(i) * 5.0);
    const double v = 2.800
                   - 0.120 * qCos(theta - 0.40)   // 1X 偏心主导
                   + 0.015 * qCos(2.0 * theta + 0.20)
                   + 0.008 * qCos(3.0 * theta - 0.35);
    m.push_back(v);
  }
  if (m.size() > 21) m[21] += 0.060;
  if (m.size() > 47) m[47] -= 0.050;

  if (teRunoutRaw_) teRunoutRaw_->setPlainText(joinDoubleList(m));
  if (teRunoutValid_) teRunoutValid_->clear();
  if (spRunoutK_) spRunoutK_->setValue(20.000000);
  if (spRunoutAngleOffset_) spRunoutAngleOffset_->setValue(0.0);
  if (spRunoutResidual_) spRunoutResidual_->setValue(0.03);
  if (spRunoutVAngle_) spRunoutVAngle_->setValue(90.0);
  if (spRunoutInterp_) spRunoutInterp_->setValue(5);
  if (cbRunoutPrimary_) cbRunoutPrimary_->setCurrentIndex(0);
  appendLog(QStringLiteral("[RUNOUT] 已填充跳动示例数据（72点，1X主导并含少量异常点）"));
}

void DevToolsWidget::onRunRunoutFromInput() {
  QVector<double> m;
  QString err;
  if (!parseDoubleSeriesText(teRunoutRaw_ ? teRunoutRaw_->toPlainText() : QString(), &m, &err)) {
    QMessageBox::warning(this, QStringLiteral("跳动算法调试"), QStringLiteral("跳动原始值解析失败：\n%1").arg(err));
    return;
  }
  if (m.isEmpty()) {
    QMessageBox::information(this, QStringLiteral("跳动算法调试"), QStringLiteral("请先提供一圈跳动原始值。"));
    return;
  }
  QVector<bool> valid;
  if (!parseBoolSeriesText(teRunoutValid_ ? teRunoutValid_->toPlainText() : QString(), m.size(), &valid, &err)) {
    QMessageBox::warning(this, QStringLiteral("跳动算法调试"), QStringLiteral("跳动有效mask解析失败：\n%1").arg(err));
    return;
  }

  core::RunoutAlgoParams params;
  params.k_runout_mm = spRunoutK_ ? spRunoutK_->value() : 0.0;
  params.angle_offset_deg = spRunoutAngleOffset_ ? spRunoutAngleOffset_->value() : 0.0;
  params.v_block_angle_deg = spRunoutVAngle_ ? spRunoutVAngle_->value() : 90.0;
  params.interpolation_factor = spRunoutInterp_ ? spRunoutInterp_->value() : 5;
  params.fit_options.residual_threshold_mm = spRunoutResidual_ ? spRunoutResidual_->value() : 0.03;
  params.harmonic.max_order = 8;
  params.harmonic.remove_mean = false;

  const auto r = core::computeRunoutAnalysis(m, valid, params);
  QStringList lines;
  lines << QStringLiteral("[RUNOUT] 运行跳动算法调试");
  lines << QStringLiteral("  K_runout=%1, angle_offset=%2, V角=%3, 插值倍率=%4")
               .arg(params.k_runout_mm, 0, 'f', 6)
               .arg(params.angle_offset_deg, 0, 'f', 6)
               .arg(params.v_block_angle_deg, 0, 'f', 3)
               .arg(params.interpolation_factor);
  lines << summarizeRunout(r, cbRunoutPrimary_ ? cbRunoutPrimary_->currentData().toInt() : 0);
  lines << summarizeHarmonics(QStringLiteral("跳动"), r.harmonics);
  appendLog(lines.join('\n'));
}

void DevToolsWidget::onRunAlgorithmFromInput() {
  QVector<double> mIn;
  QVector<double> mOut;
  QString err;
  if (!parseDoubleSeriesText(teAlgoInnerRaw_ ? teAlgoInnerRaw_->toPlainText() : QString(), &mIn, &err)) {
    QMessageBox::warning(this, QStringLiteral("算法调试"), QStringLiteral("内径原始值解析失败：\n%1").arg(err));
    return;
  }
  if (!parseDoubleSeriesText(teAlgoOuterRaw_ ? teAlgoOuterRaw_->toPlainText() : QString(), &mOut, &err)) {
    QMessageBox::warning(this, QStringLiteral("算法调试"), QStringLiteral("外径原始值解析失败：\n%1").arg(err));
    return;
  }
  if (mIn.isEmpty() && mOut.isEmpty()) {
    QMessageBox::information(this, QStringLiteral("算法调试"), QStringLiteral("请至少提供一组内径或外径原始值。"));
    return;
  }

  QVector<bool> validIn;
  QVector<bool> validOut;
  if (!mIn.isEmpty() && !parseBoolSeriesText(teAlgoInnerValid_ ? teAlgoInnerValid_->toPlainText() : QString(), mIn.size(), &validIn, &err)) {
    QMessageBox::warning(this, QStringLiteral("算法调试"), QStringLiteral("内径有效mask解析失败：\n%1").arg(err));
    return;
  }
  if (!mOut.isEmpty() && !parseBoolSeriesText(teAlgoOuterValid_ ? teAlgoOuterValid_->toPlainText() : QString(), mOut.size(), &validOut, &err)) {
    QMessageBox::warning(this, QStringLiteral("算法调试"), QStringLiteral("外径有效mask解析失败：\n%1").arg(err));
    return;
  }

  core::DiameterAlgoParams params;
  params.k_in_mm = spAlgoKIn_ ? spAlgoKIn_->value() : 0.0;
  params.k_out_mm = spAlgoKOut_ ? spAlgoKOut_->value() : 0.0;
  params.use_explicit_k_out = cbAlgoUseExplicitKOut_ && cbAlgoUseExplicitKOut_->isChecked();
  params.probe_base_mm = spAlgoProbeBase_ ? spAlgoProbeBase_->value() : 15.0;
  params.angle_offset_deg = spAlgoAngleOffset_ ? spAlgoAngleOffset_->value() : 0.0;
  params.inner_fit.residual_threshold_mm = spAlgoResidualIn_ ? spAlgoResidualIn_->value() : 0.03;
  params.outer_fit.residual_threshold_mm = spAlgoResidualOut_ ? spAlgoResidualOut_->value() : 0.03;
  params.harmonic.max_order = 8;
  params.harmonic.remove_mean = false;

  QStringList lines;
  lines << QStringLiteral("[ALG] 运行算法调试");
  lines << QStringLiteral("  K_in=%1, K_out=%2, use_explicit_k_out=%3, L_aux=%4, L_eff(Kout-Kin)=%5, angle_offset=%6")
               .arg(params.k_in_mm, 0, 'f', 6)
               .arg(params.k_out_mm, 0, 'f', 6)
               .arg(params.use_explicit_k_out ? QStringLiteral("true") : QStringLiteral("false"))
               .arg(params.probe_base_mm, 0, 'f', 6)
               .arg(params.k_out_mm - params.k_in_mm, 0, 'f', 6)
               .arg(params.angle_offset_deg, 0, 'f', 6);

  if (!mIn.isEmpty()) {
    const auto inner = core::computeInnerDiameter(mIn, validIn, params);
    lines << summarizeCircleFit(QStringLiteral("内径"), inner);
    lines << summarizeHarmonics(QStringLiteral("内径"), inner.harmonics);
  }

  if (!mOut.isEmpty()) {
    const auto outer = core::computeOuterDiameter(mOut, validOut, params);
    lines << summarizeCircleFit(QStringLiteral("外径"), outer);
    lines << summarizeHarmonics(QStringLiteral("外径"), outer.harmonics);
  }

  if (!mIn.isEmpty() && !mOut.isEmpty()) {
    if (mIn.size() != mOut.size()) {
      lines << QStringLiteral("[壁厚]\n  跳过：内外数组长度不一致（%1 vs %2）")
                   .arg(mIn.size())
                   .arg(mOut.size());
    } else {
      const auto t = core::computeThickness(mIn, validIn, mOut, validOut, params);
      lines << summarizeThickness(t);
    }
  }

  appendLog(lines.join('\n'));
}
