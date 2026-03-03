#include "mes_upload_widget.hpp"
#include "mes_worker.hpp"

#include <QMessageBox>
#include <QDateTime>

#include "ui_mes_upload_widget.h"

MesUploadWidget::MesUploadWidget(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent)
    : QWidget(parent), ui_(new Ui::MesUploadWidget), cfg_(cfg), worker_(worker)
{
    ui_->setupUi(this);

    // 打开数据库并确保表结构
    QString e;
    db_.open(cfg_.db, &e);
    db_.ensureSchema(&e);

    // filters init
    ui_->dtFrom->setDateTime(QDateTime::currentDateTimeUtc().addDays(-7));
    ui_->dtTo->setDateTime(QDateTime::currentDateTimeUtc());
    ui_->dtFrom->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    ui_->dtTo->setDisplayFormat("yyyy-MM-dd HH:mm:ss");

    ui_->edPartId->setPlaceholderText("part_id contains...");

    ui_->cbType->clear();
    ui_->cbType->addItems({"ALL", "A", "B"});

    ui_->cbOk->clear();
    ui_->cbOk->addItems({"ALL", "OK", "NG"});

    ui_->cbMesStatus->clear();
    ui_->cbMesStatus->addItems({"ALL", "NOT_QUEUED", "PENDING", "SENDING", "SENT", "FAILED"});

    connect(ui_->btnQuery, &QPushButton::clicked, this, &MesUploadWidget::onQuery);
    connect(ui_->btnUpload, &QPushButton::clicked, this, &MesUploadWidget::onUploadSelected);
    connect(ui_->btnRetry, &QPushButton::clicked, this, &MesUploadWidget::onRetrySelectedFailed);

    // table
    model_ = new QStandardItemModel(this);
    model_->setHorizontalHeaderLabels({"SEL", "measured_at_utc", "part_id", "type", "ok",
                                       "total_len_mm", "bc_len_mm", "mes_status",
                                       "attempts", "last_error", "uuid"});
    ui_->tableView->setModel(model_);
    ui_->tableView->setColumnHidden(10, true);
    ui_->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui_->tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    if (worker_)
        connect(worker_, &MesWorker::outboxChanged, this, &MesUploadWidget::onOutboxChanged);

    onQuery();
}

MesUploadWidget::~MesUploadWidget()
{
    delete ui_;
}

void MesUploadWidget::onQuery()
{
    core::MesUploadFilter f;
    f.from_utc = ui_->dtFrom->dateTime().toUTC();
    f.to_utc = ui_->dtTo->dateTime().toUTC();
    f.part_id_like = ui_->edPartId->text().trimmed();
    f.part_type = (ui_->cbType->currentText() == "ALL") ? "" : ui_->cbType->currentText();

    if (ui_->cbOk->currentText() == "OK")
        f.ok_filter = 1;
    else if (ui_->cbOk->currentText() == "NG")
        f.ok_filter = 0;
    else
        f.ok_filter = -1;

    f.mes_status = (ui_->cbMesStatus->currentText() == "ALL") ? "" : ui_->cbMesStatus->currentText();

    QString e;
    auto rows = db_.queryMesUploadRows(f, 500, &e);
    if (!e.isEmpty())
        QMessageBox::warning(this, "Query", e);

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
            uuids.push_back(model_->item(i, 10)->text());
    }
    return uuids;
}

void MesUploadWidget::onUploadSelected()
{
    if (!cfg_.mes.enabled || !cfg_.mes.manual_enabled || cfg_.mes.url.trimmed().isEmpty())
    {
        QMessageBox::warning(this, "MES", "MES manual upload disabled or URL empty in app.ini");
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
    if (!cfg_.mes.enabled || !cfg_.mes.manual_enabled)
    {
        QMessageBox::warning(this, "MES", "MES manual upload disabled in app.ini");
        return;
    }

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
    onQuery();
}
