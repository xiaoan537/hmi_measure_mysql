#include "mes_upload_widget.hpp"
#include "mes_worker.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>

MesUploadWidget::MesUploadWidget(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent)
    : QWidget(parent), cfg_(cfg), worker_(worker)
{
    QString e;
    db_.open(cfg_.db, &e);
    db_.ensureSchema(&e);

    // filters
    dtFrom_ = new QDateTimeEdit(QDateTime::currentDateTimeUtc().addDays(-7), this);
    dtTo_ = new QDateTimeEdit(QDateTime::currentDateTimeUtc(), this);
    dtFrom_->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    dtTo_->setDisplayFormat("yyyy-MM-dd HH:mm:ss");

    edPartId_ = new QLineEdit(this);
    edPartId_->setPlaceholderText("part_id contains...");

    cbType_ = new QComboBox(this);
    cbType_->addItems({"ALL", "A", "B"});

    cbOk_ = new QComboBox(this);
    cbOk_->addItems({"ALL", "OK", "NG"});

    cbMesStatus_ = new QComboBox(this);
    cbMesStatus_->addItems({"ALL", "NOT_QUEUED", "PENDING", "SENDING", "SENT", "FAILED"});

    btnQuery_ = new QPushButton("Query", this);
    btnUpload_ = new QPushButton("Upload Selected", this);
    btnRetry_ = new QPushButton("Retry FAILED (Selected)", this);

    connect(btnQuery_, &QPushButton::clicked, this, &MesUploadWidget::onQuery);
    connect(btnUpload_, &QPushButton::clicked, this, &MesUploadWidget::onUploadSelected);
    connect(btnRetry_, &QPushButton::clicked, this, &MesUploadWidget::onRetrySelectedFailed);

    // table
    table_ = new QTableView(this);
    model_ = new QStandardItemModel(this);
    model_->setHorizontalHeaderLabels({"SEL", "measured_at_utc", "part_id", "type", "ok", "total_len_mm", "bc_len_mm", "mes_status", "attempts", "last_error", "uuid"});
    table_->setModel(model_);
    table_->setColumnHidden(10, true); // uuid hidden
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto *top = new QHBoxLayout();
    top->addWidget(dtFrom_);
    top->addWidget(dtTo_);
    top->addWidget(edPartId_);
    top->addWidget(cbType_);
    top->addWidget(cbOk_);
    top->addWidget(cbMesStatus_);
    top->addWidget(btnQuery_);

    auto *bottom = new QHBoxLayout();
    bottom->addWidget(btnUpload_);
    bottom->addWidget(btnRetry_);
    bottom->addStretch(1);

    auto *root = new QVBoxLayout(this);
    root->addLayout(top);
    root->addWidget(table_, 1);
    root->addLayout(bottom);

    if (worker_)
    {
        connect(worker_, &MesWorker::outboxChanged, this, &MesUploadWidget::onOutboxChanged);
    }

    onQuery();
}

void MesUploadWidget::onQuery()
{
    core::MesUploadFilter f;
    f.from_utc = dtFrom_->dateTime().toUTC();
    f.to_utc = dtTo_->dateTime().toUTC();
    f.part_id_like = edPartId_->text().trimmed();
    f.part_type = (cbType_->currentText() == "ALL") ? "" : cbType_->currentText();
    if (cbOk_->currentText() == "OK")
        f.ok_filter = 1;
    else if (cbOk_->currentText() == "NG")
        f.ok_filter = 0;
    else
        f.ok_filter = -1;
    f.mes_status = (cbMesStatus_->currentText() == "ALL") ? "" : cbMesStatus_->currentText();

    QString e;
    auto rows = db_.queryMesUploadRows(f, 500, &e);
    if (!e.isEmpty())
    {
        QMessageBox::warning(this, "Query", e);
    }
    fillTable(rows);
}

void MesUploadWidget::fillTable(const QVector<core::MesUploadRow> &rows)
{
    model_->setRowCount(0);
    for (const auto &r : rows)
    {
        QList<QStandardItem *> items;

        auto *sel = new QStandardItem();
        sel->setCheckable(true);
        sel->setCheckState(Qt::Unchecked);

        items << sel;
        items << new QStandardItem(r.measured_at_utc.toUTC().toString(Qt::ISODateWithMs));
        items << new QStandardItem(r.part_id);
        items << new QStandardItem(r.part_type);
        items << new QStandardItem(r.ok ? "1" : "0");
        items << new QStandardItem(QString::number(r.total_len_mm, 'f', 3));
        items << new QStandardItem(QString::number(r.bc_len_mm, 'f', 3));
        items << new QStandardItem(r.mes_status);
        items << new QStandardItem(QString::number(r.attempt_count));
        items << new QStandardItem(r.last_error.left(120));
        items << new QStandardItem(r.measurement_uuid);

        model_->appendRow(items);
    }
}

QVector<QString> MesUploadWidget::selectedUuids() const
{
    QVector<QString> uuids;
    const int n = model_->rowCount();
    for (int i = 0; i < n; i++)
    {
        auto *sel = model_->item(i, 0);
        if (sel && sel->checkState() == Qt::Checked)
        {
            uuids.push_back(model_->item(i, 10)->text());
        }
    }
    return uuids;
}

void MesUploadWidget::onUploadSelected()
{
    if (!cfg_.mes.enabled || cfg_.mes.url.trimmed().isEmpty())
    {
        QMessageBox::warning(this, "MES", "MES disabled or URL empty in app.ini");
        return;
    }

    const auto uuids = selectedUuids();
    if (uuids.isEmpty())
    {
        QMessageBox::information(this, "MES", "No rows selected.");
        return;
    }

    int queued = 0, skipped = 0;
    QString lastErr;

    for (const auto &u : uuids)
    {
        QString e;
        const bool ok = db_.queueMesUploadByUuid(u, &e);
        if (ok)
            queued++;
        else
        {
            skipped++;
            lastErr = e;
        }
    }

    if (worker_)
        worker_->kick();
    onQuery();

    QMessageBox::information(
        this, "MES",
        QString("Queued: %1, Skipped: %2\nLast note: %3").arg(queued).arg(skipped).arg(lastErr));
}

void MesUploadWidget::onRetrySelectedFailed()
{
    const auto uuids = selectedUuids();
    if (uuids.isEmpty())
    {
        QMessageBox::information(this, "MES", "No rows selected.");
        return;
    }
    QString e;
    const int n = db_.retryFailed(uuids, &e);
    if (!e.isEmpty())
        QMessageBox::warning(this, "Retry", e);

    if (worker_)
        worker_->kick();
    onQuery();

    QMessageBox::information(this, "Retry", QString("Re-queued FAILED rows: %1").arg(n));
}

void MesUploadWidget::onOutboxChanged()
{
    // outbox 状态变化时自动刷新
    onQuery();
}
