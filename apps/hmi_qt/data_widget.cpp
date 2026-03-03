#include "data_widget.hpp"

#include <QStandardItemModel>
#include <QMessageBox>
#include <QDateTime>

#include "core/db.hpp"

#include "ui_data_widget.h"

DataWidget::DataWidget(const core::AppConfig& cfg, QWidget* parent)
  : QWidget(parent), ui_(new Ui::DataWidget), cfg_(cfg)
{
  ui_->setupUi(this);
  setupModel();

  // defaults
  ui_->dateFrom->setDateTime(QDateTime::currentDateTime().addDays(-7));
  ui_->dateTo->setDateTime(QDateTime::currentDateTime());
  ui_->spinLimit->setValue(200);

  connect(ui_->btnRefresh, &QPushButton::clicked, this, &DataWidget::refresh);
  connect(ui_->btnOpenRaw, &QPushButton::clicked, this, &DataWidget::onOpenRawClicked);
  connect(ui_->btnQueueMes, &QPushButton::clicked, this, &DataWidget::onQueueMesClicked);

  // filter changes
  connect(ui_->editPartId, &QLineEdit::textChanged, this, &DataWidget::onFilterChanged);
  connect(ui_->comboType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DataWidget::onFilterChanged);
  connect(ui_->comboOk, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DataWidget::onFilterChanged);
  connect(ui_->comboMes, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DataWidget::onFilterChanged);
  connect(ui_->dateFrom, &QDateTimeEdit::dateTimeChanged, this, &DataWidget::onFilterChanged);
  connect(ui_->dateTo, &QDateTimeEdit::dateTimeChanged, this, &DataWidget::onFilterChanged);

  connect(ui_->tableView->selectionModel(), &QItemSelectionModel::currentRowChanged,
          this, [this](const QModelIndex& cur, const QModelIndex&) {
            applyRowToDetails(cur.row());
          });

  refresh();
}

DataWidget::~DataWidget()
{
  delete ui_;
}

void DataWidget::setupModel()
{
  model_ = new QStandardItemModel(this);
  model_->setHorizontalHeaderLabels({
    "Measured(UTC)", "PartID", "Type", "OK", "TotalLen", "BCLen", "MES", "Attempts", "UUID"
  });
  ui_->tableView->setModel(model_);
  ui_->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  ui_->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
  ui_->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui_->tableView->horizontalHeader()->setStretchLastSection(true);
  ui_->tableView->setColumnHidden(8, true); // UUID hidden but available
}

void DataWidget::onFilterChanged()
{
  // Debounce not necessary for now; keep it explicit via refresh button.
}

void DataWidget::refresh()
{
  core::Db db;
  QString err;
  if (!db.open(cfg_.db, &err))
  {
    QMessageBox::warning(this, "DB", err);
    return;
  }
  if (!db.ensureSchema(&err))
  {
    QMessageBox::warning(this, "DB", err);
    return;
  }

  core::MesUploadFilter f;
  f.from_utc = ui_->dateFrom->dateTime().toUTC();
  f.to_utc = ui_->dateTo->dateTime().toUTC();
  f.part_id_like = ui_->editPartId->text().trimmed();

  // type
  const QString t = ui_->comboType->currentData().toString();
  f.part_type = t;

  // ok
  f.ok_filter = ui_->comboOk->currentData().toInt();

  // mes status
  f.mes_status = ui_->comboMes->currentData().toString();

  const int limit = ui_->spinLimit->value();
  const auto rows = db.queryMesUploadRows(f, limit, &err);
  if (!err.isEmpty())
  {
    QMessageBox::warning(this, "Query", err);
    return;
  }

  model_->removeRows(0, model_->rowCount());
  model_->setRowCount((int)rows.size());
  for (int i = 0; i < rows.size(); ++i)
  {
    const auto& r = rows[i];
    model_->setItem(i, 0, new QStandardItem(r.measured_at_utc.toString("yyyy-MM-dd HH:mm:ss")));
    model_->setItem(i, 1, new QStandardItem(r.part_id));
    model_->setItem(i, 2, new QStandardItem(r.part_type));
    model_->setItem(i, 3, new QStandardItem(r.ok ? "OK" : "NG"));
    model_->setItem(i, 4, new QStandardItem(QString::number(r.total_len_mm, 'f', 3)));
    model_->setItem(i, 5, new QStandardItem(QString::number(r.bc_len_mm, 'f', 3)));
    model_->setItem(i, 6, new QStandardItem(r.mes_status));
    model_->setItem(i, 7, new QStandardItem(QString::number(r.attempt_count)));
    model_->setItem(i, 8, new QStandardItem(r.measurement_uuid));
  }

  ui_->lblCount->setText(QString("%1 rows").arg(rows.size()));
  if (rows.size() > 0)
    ui_->tableView->selectRow(0);
  else
    ui_->textDetail->clear();
}

void DataWidget::applyRowToDetails(int row)
{
  if (row < 0 || row >= model_->rowCount())
  {
    ui_->textDetail->clear();
    return;
  }
  const QString uuid = model_->item(row, 8)->text();
  QString detail;
  detail += QString("UUID: %1\n").arg(uuid);
  detail += QString("Measured(UTC): %1\n").arg(model_->item(row, 0)->text());
  detail += QString("PartID: %1\n").arg(model_->item(row, 1)->text());
  detail += QString("Type: %1\n").arg(model_->item(row, 2)->text());
  detail += QString("OK: %1\n").arg(model_->item(row, 3)->text());
  detail += QString("TotalLen: %1\n").arg(model_->item(row, 4)->text());
  detail += QString("BCLen: %1\n").arg(model_->item(row, 5)->text());
  detail += QString("MES: %1 (attempts %2)\n").arg(model_->item(row, 6)->text()).arg(model_->item(row, 7)->text());
  ui_->textDetail->setPlainText(detail);
}

void DataWidget::onSelectionChanged() {}

void DataWidget::onOpenRawClicked()
{
  const QModelIndex cur = ui_->tableView->currentIndex();
  if (!cur.isValid())
    return;
  const QString uuid = model_->item(cur.row(), 8)->text();
  emit requestOpenRaw(uuid);
}

void DataWidget::onQueueMesClicked()
{
  const QModelIndex cur = ui_->tableView->currentIndex();
  if (!cur.isValid())
    return;
  const QString uuid = model_->item(cur.row(), 8)->text();
  emit requestQueueMesUpload(uuid);
}
