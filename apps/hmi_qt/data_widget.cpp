#include "data_widget.hpp"

#include <QAbstractItemView>
#include <QDateTime>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStringList>

#include "core/db.hpp"

#include "ui_data_widget.h"

DataWidget::DataWidget(const core::AppConfig &cfg, QWidget *parent)
    : QWidget(parent), ui_(new Ui::DataWidget), cfg_(cfg) {
  ui_->setupUi(this);
  setupModel();

  ui_->dateFrom->setDateTime(QDateTime::currentDateTime().addDays(-7));
  ui_->dateTo->setDateTime(QDateTime::currentDateTime());
  ui_->spinLimit->setValue(200);

  // 当前 Data 页先切到新 measurement 链路，MES 状态过滤暂未接入
  ui_->comboMes->setEnabled(false);
  ui_->comboMes->setToolTip(
      QStringLiteral("当前页面已切到新测量记录查询，MES状态过滤后续再接入"));

  connect(ui_->btnRefresh, &QPushButton::clicked, this, &DataWidget::refresh);
  connect(ui_->btnOpenRaw, &QPushButton::clicked, this,
          &DataWidget::onOpenRawClicked);
  connect(ui_->btnQueueMes, &QPushButton::clicked, this,
          &DataWidget::onQueueMesClicked);

  connect(ui_->editPartId, &QLineEdit::textChanged, this,
          &DataWidget::onFilterChanged);
  connect(ui_->comboType, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &DataWidget::onFilterChanged);
  connect(ui_->comboOk, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &DataWidget::onFilterChanged);
  connect(ui_->dateFrom, &QDateTimeEdit::dateTimeChanged, this,
          &DataWidget::onFilterChanged);
  connect(ui_->dateTo, &QDateTimeEdit::dateTimeChanged, this,
          &DataWidget::onFilterChanged);

  connect(ui_->tableView->selectionModel(),
          &QItemSelectionModel::currentRowChanged, this,
          [this](const QModelIndex &cur, const QModelIndex &) {
            if (!cur.isValid()) {
              ui_->textDetail->clear();
              return;
            }
            const auto *idItem = model_->item(cur.row(), 0);
            if (!idItem) {
              ui_->textDetail->clear();
              return;
            }
            const quint64 measurementId = idItem->text().toULongLong();
            showMeasurementDetail(measurementId);
          });

  refresh();
}

DataWidget::~DataWidget() { delete ui_; }

void DataWidget::setupModel() {
  model_ = new QStandardItemModel(this);
  model_->setHorizontalHeaderLabels(
      {QStringLiteral("测量ID"), QStringLiteral("测量时间"),
       QStringLiteral("工件号"), QStringLiteral("型号"), QStringLiteral("模式"),
       QStringLiteral("轮次"), QStringLiteral("判定"),
       QStringLiteral("结果摘要"), QStringLiteral("UUID")});

  ui_->tableView->setModel(model_);
  ui_->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  ui_->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
  ui_->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui_->tableView->horizontalHeader()->setStretchLastSection(true);

  // 隐藏内部列：measurement_id / measurement_uuid
  ui_->tableView->setColumnHidden(0, true);
  ui_->tableView->setColumnHidden(8, true);
}

void DataWidget::onFilterChanged() {
  // 当前先保持“显式点击刷新”，后续如有需要再做 debounce 自动刷新
}

QString DataWidget::makeMeasurementSummary(
    const core::MeasurementListRowEx &row) const {
  if (row.part_type == "A") {
    if (row.has_total_len) {
      return QString("L=%1").arg(row.total_len_mm, 0, 'f', 3);
    }
    return QStringLiteral("--");
  }

  if (row.part_type == "B") {
    QStringList parts;
    if (row.has_ad_len) {
      parts << QString("AD=%1").arg(row.ad_len_mm, 0, 'f', 3);
    }
    if (row.has_bc_len) {
      parts << QString("BC=%1").arg(row.bc_len_mm, 0, 'f', 3);
    }
    return parts.isEmpty() ? QStringLiteral("--") : parts.join("  ");
  }

  return QStringLiteral("--");
}

void DataWidget::refresh() { reloadFromNewMeasurementSchema(); }

void DataWidget::reloadFromNewMeasurementSchema() {
  core::Db db;
  QString err;
  if (!db.open(cfg_.db, &err)) {
    QMessageBox::warning(this, QStringLiteral("数据库"), err);
    return;
  }
  if (!db.ensureSchema(&err)) {
    QMessageBox::warning(this, QStringLiteral("数据库"), err);
    return;
  }

  const QString partIdLike = ui_->editPartId->text().trimmed();
  // const QString typeFilter = ui_->comboType->currentData().toString();
  const int typeFilterIndex = ui_->comboType->currentIndex();
  // const int okFilter = ui_->comboOk->currentData().toInt();
  const int okFilterIndex = ui_->comboOk->currentIndex();
  const QDateTime fromUtc = ui_->dateFrom->dateTime().toUTC();
  const QDateTime toUtc = ui_->dateTo->dateTime().toUTC();
  const int limit = ui_->spinLimit->value();

  // 新接口当前只支持按 limit 查询，先取稍大一些再本地筛选
  QString qerr;
  const int fetchLimit = qMax(limit, 500);
  const auto rows = db.queryLatestMeasurementsEx(fetchLimit, &qerr);
  if (!qerr.isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("查询"), qerr);
    return;
  }

  model_->removeRows(0, model_->rowCount());

  int outRow = 0;
  for (const auto &r : rows) {
    if (!partIdLike.isEmpty() &&
        !r.part_id.contains(partIdLike, Qt::CaseInsensitive)) {
      continue;
    }

    // if (!typeFilter.isEmpty() && r.part_type != typeFilter) {
    //   continue;
    // }

    // 假设 comboType 顺序是：全部 / A型 / B型
    if (typeFilterIndex == 1 && r.part_type != "A") {
      continue;
    }
    if (typeFilterIndex == 2 && r.part_type != "B") {
      continue;
    }

    if (r.measured_at_utc.isValid()) {
      if (r.measured_at_utc < fromUtc || r.measured_at_utc > toUtc) {
        continue;
      }
    }

    // if (okFilter == 1 && r.result_judgement != "OK") {
    //   continue;
    // }
    // if (okFilter == 0 && r.result_judgement == "OK") {
    //   continue;
    // }

    // 假设 comboOk 三个选项顺序是：全部 / OK / 非OK
    if (okFilterIndex == 1 && r.result_judgement != "OK") {
      continue;
    }
    if (okFilterIndex == 2 && r.result_judgement == "OK") {
      continue;
    }

    if (outRow >= limit) {
      break;
    }

    model_->setItem(outRow, 0,
                    new QStandardItem(QString::number(r.measurement_id)));
    model_->setItem(outRow, 1,
                    new QStandardItem(r.measured_at_utc.toLocalTime().toString(
                        "yyyy-MM-dd HH:mm:ss")));
    model_->setItem(outRow, 2, new QStandardItem(r.part_id));
    model_->setItem(outRow, 3, new QStandardItem(r.part_type));
    model_->setItem(outRow, 4, new QStandardItem(r.measure_mode));
    model_->setItem(outRow, 5,
                    new QStandardItem(QString::number(r.measure_round)));
    model_->setItem(outRow, 6, new QStandardItem(r.result_judgement));
    model_->setItem(outRow, 7, new QStandardItem(makeMeasurementSummary(r)));
    model_->setItem(outRow, 8, new QStandardItem(r.measurement_uuid));

    ++outRow;
  }

  ui_->lblCount->setText(QStringLiteral("%1 条").arg(outRow));

  if (outRow > 0) {
    ui_->tableView->selectRow(0);
    const quint64 firstId = model_->item(0, 0)->text().toULongLong();
    showMeasurementDetail(firstId);
  } else {
    ui_->textDetail->clear();
  }
}

void DataWidget::showMeasurementDetail(quint64 measurementId) {
  core::Db db;
  QString err;
  if (!db.open(cfg_.db, &err)) {
    QMessageBox::warning(this, QStringLiteral("数据库"), err);
    return;
  }
  if (!db.ensureSchema(&err)) {
    QMessageBox::warning(this, QStringLiteral("数据库"), err);
    return;
  }

  core::MeasurementDetailEx d;
  if (!db.getMeasurementDetailExById(measurementId, &d, &err)) {
    QMessageBox::warning(this, QStringLiteral("详情查询"), err);
    return;
  }
  if (!d.found) {
    ui_->textDetail->setPlainText(QStringLiteral("未找到记录"));
    return;
  }

  QString text;
  text += QStringLiteral("测量ID: %1\n").arg(d.measurement_id);
  text += QStringLiteral("UUID: %1\n").arg(d.measurement_uuid);
  text += QStringLiteral("工件号: %1\n").arg(d.part_id);
  text += QStringLiteral("型号: %1\n").arg(d.part_type);
  text += QStringLiteral("槽位ID: %1\n").arg(d.slot_id);
  text += QStringLiteral("槽位索引: %1\n")
              .arg(d.slot_index.isNull() ? QStringLiteral("-")
                                         : d.slot_index.toString());
  text += QStringLiteral("Item索引: %1\n")
              .arg(d.item_index.isNull() ? QStringLiteral("-")
                                         : d.item_index.toString());
  text += QStringLiteral("模式: %1\n").arg(d.measure_mode);
  text += QStringLiteral("轮次: %1\n").arg(d.measure_round);
  text += QStringLiteral("判定: %1\n").arg(d.result_judgement);
  text += QStringLiteral("复核状态: %1\n").arg(d.review_status);
  text += QStringLiteral("失败码: %1\n").arg(d.fail_reason_code);
  text += QStringLiteral("失败原因: %1\n").arg(d.fail_reason_text);
  text += QStringLiteral("测量时间(UTC): %1\n")
              .arg(d.measured_at_utc.toString("yyyy-MM-dd HH:mm:ss"));

  if (d.part_type == "A") {
    text += QStringLiteral("\n[A型结果]\n");
    text += QStringLiteral("总长: %1\n")
                .arg(d.total_len_mm.isNull() ? QStringLiteral("-")
                                             : d.total_len_mm.toString());
    text += QStringLiteral("左端内径: %1\n")
                .arg(d.id_left_mm.isNull() ? QStringLiteral("-")
                                           : d.id_left_mm.toString());
    text += QStringLiteral("右端内径: %1\n")
                .arg(d.id_right_mm.isNull() ? QStringLiteral("-")
                                            : d.id_right_mm.toString());
    text += QStringLiteral("左端外径: %1\n")
                .arg(d.od_left_mm.isNull() ? QStringLiteral("-")
                                           : d.od_left_mm.toString());
    text += QStringLiteral("右端外径: %1\n")
                .arg(d.od_right_mm.isNull() ? QStringLiteral("-")
                                            : d.od_right_mm.toString());
  } else if (d.part_type == "B") {
    text += QStringLiteral("\n[B型结果]\n");
    text += QStringLiteral("AD长度: %1\n")
                .arg(d.ad_len_mm.isNull() ? QStringLiteral("-")
                                          : d.ad_len_mm.toString());
    text += QStringLiteral("BC长度: %1\n")
                .arg(d.bc_len_mm.isNull() ? QStringLiteral("-")
                                          : d.bc_len_mm.toString());
    text += QStringLiteral("左端跳动: %1\n")
                .arg(d.runout_left_mm.isNull() ? QStringLiteral("-")
                                               : d.runout_left_mm.toString());
    text += QStringLiteral("右端跳动: %1\n")
                .arg(d.runout_right_mm.isNull() ? QStringLiteral("-")
                                                : d.runout_right_mm.toString());
  }

  ui_->textDetail->setPlainText(text);
}

void DataWidget::onSelectionChanged() {}

void DataWidget::onOpenRawClicked() {
  const QModelIndex cur = ui_->tableView->currentIndex();
  if (!cur.isValid()) {
    return;
  }
  const QString uuid = model_->item(cur.row(), 8)->text();
  emit requestOpenRaw(uuid);
}

void DataWidget::onQueueMesClicked() {
  const QModelIndex cur = ui_->tableView->currentIndex();
  if (!cur.isValid()) {
    return;
  }
  const QString uuid = model_->item(cur.row(), 8)->text();
  emit requestQueueMesUpload(uuid);
}