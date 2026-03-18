#include "mes_upload_widget.hpp"
#include "mes_worker.hpp"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSet>

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

    edTaskCard_ = new QLineEdit(this);
    edTaskCard_->setPlaceholderText(QStringLiteral("卡号 contains..."));
    if (auto *hl = qobject_cast<QHBoxLayout *>(ui_->horizontalLayoutFilters)) {
        hl->insertWidget(3, edTaskCard_);
    }

    ui_->cbType->clear();
    ui_->cbType->addItems({"ALL", "A", "B"});

    ui_->cbOk->clear();
    ui_->cbOk->addItems({"ALL", "成功", "NG"});

    ui_->cbMesStatus->clear();
    ui_->cbMesStatus->addItems({"ALL", "NOT_QUEUED", "PENDING", "SENDING", "SENT", "FAILED"});

    connect(ui_->btnQuery, &QPushButton::clicked, this, &MesUploadWidget::onQuery);
    connect(ui_->btnUpload, &QPushButton::clicked, this, &MesUploadWidget::onUploadSelected);
    connect(ui_->btnRetry, &QPushButton::clicked, this, &MesUploadWidget::onRetrySelectedFailed);

    // table
    model_ = new QStandardItemModel(this);
    model_->setHorizontalHeaderLabels({"SEL", "measured_at_utc", "part_id", "task_card_no", "type", "mode", "attempt", "interface_code", "ok",
                                       "total_len_mm", "bc_len_mm", "mes_status",
                                       "attempts", "last_error", "uuid"});
    ui_->tableView->setModel(model_);
    ui_->tableView->setColumnHidden(14, true);
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
    f.task_card_no_like = edTaskCard_ ? edTaskCard_->text().trimmed() : QString();
    f.part_type = (ui_->cbType->currentText() == "ALL") ? "" : ui_->cbType->currentText();

    if (ui_->cbOk->currentText() == "成功")
        f.ok_filter = 1;
    else if (ui_->cbOk->currentText() == "NG")
        f.ok_filter = 0;
    else
        f.ok_filter = -1;

    f.mes_status = (ui_->cbMesStatus->currentText() == "ALL") ? "" : ui_->cbMesStatus->currentText();

    QString e;
    auto rows = db_.queryMesUploadRows(f, 500, &e);
    if (!e.isEmpty())
        QMessageBox::warning(this, "查询", e);

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
        items << new QStandardItem(r.task_card_no);
        items << new QStandardItem(r.part_type);
        items << new QStandardItem(r.measure_mode);
        items << new QStandardItem(r.attempt_kind);
        items << new QStandardItem(r.interface_code);
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
            uuids.push_back(model_->item(i, 14)->text());
    }
    return uuids;
}

void MesUploadWidget::onUploadSelected()
{
    if (!cfg_.mes.enabled || !cfg_.mes.manual_enabled)
    {
        QMessageBox::warning(this, "MES", "MES 手动上传已被禁用");
        return;
    }

    const auto uuids = selectedUuids();
    if (uuids.isEmpty())
    {
        QMessageBox::information(this, "MES", "未选择任何行。");
        return;
    }

    int queued = 0, skipped = 0;
    QString lastErr;
    QSet<QString> missingInterfaces;

    const int n = model_->rowCount();
    for (int i = 0; i < n; i++)
    {
        auto *sel = model_->item(i, 0);
        if (!(sel && sel->checkState() == Qt::Checked))
            continue;

        const QString interfaceCode = model_->item(i, 7) ? model_->item(i, 7)->text().trimmed() : QString();
        const QString uuid = model_->item(i, 14) ? model_->item(i, 14)->text().trimmed() : QString();
        if (uuid.isEmpty())
        {
            skipped++;
            lastErr = QStringLiteral("存在缺少 measurement_uuid 的行，已跳过");
            continue;
        }
        if (!core::hasMesInterfaceUrl(cfg_.mes, interfaceCode))
        {
            skipped++;
            missingInterfaces.insert(interfaceCode.isEmpty() ? QStringLiteral("<EMPTY_INTERFACE_CODE>") : interfaceCode);
            lastErr = QStringLiteral("存在未配置接口地址的记录，已跳过");
            continue;
        }

        QString e;
        const bool ok = db_.queueMesUploadByUuid(uuid, &e);
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

    QString summary = QString("Queued: %1, Skipped: %2\nLast note: %3").arg(queued).arg(skipped).arg(lastErr);
    if (!missingInterfaces.isEmpty())
        summary += QString("\n未配置地址的接口: %1").arg(QStringList(missingInterfaces.values()).join(", "));

    QMessageBox::information(this, "MES", summary);
}

void MesUploadWidget::onRetrySelectedFailed()
{
    if (!cfg_.mes.enabled || !cfg_.mes.manual_enabled)
    {
        QMessageBox::warning(this, "MES", "app.ini 中已禁用 MES 手动上传");
        return;
    }

    const auto uuids = selectedUuids();
    if (uuids.isEmpty())
    {
        QMessageBox::information(this, "MES", "未选择任何行。");
        return;
    }

    QString e;
    const int n = db_.retryFailed(uuids, &e);
    if (!e.isEmpty())
        QMessageBox::warning(this, "重试", e);

    if (worker_)
        worker_->kick();
    onQuery();

    QMessageBox::information(this, "重试", QString("已重新入队失败记录：%1").arg(n));
}

void MesUploadWidget::onOutboxChanged()
{
    onQuery();
}
