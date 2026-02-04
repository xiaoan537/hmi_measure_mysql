#include "mes_upload_widget.hpp"
#include "mes_worker.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>

// 构造函数，初始化上传小部件，设置布局、数据库连接、查询按钮连接槽函数。
MesUploadWidget::MesUploadWidget(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent)
    : QWidget(parent), cfg_(cfg), worker_(worker)
{
    // 打开数据库并确保表结构
    QString e;
    db_.open(cfg_.db, &e);
    db_.ensureSchema(&e);

    // filters
    dtFrom_ = new QDateTimeEdit(QDateTime::currentDateTimeUtc().addDays(-7), this); // 用于编辑日期和时间的控件，通常用于用户输入日期和时间。
    dtTo_ = new QDateTimeEdit(QDateTime::currentDateTimeUtc(), this);
    dtFrom_->setDisplayFormat("yyyy-MM-dd HH:mm:ss"); // 用于设置 QDateTimeEdit 控件显示日期时间的格式
    dtTo_->setDisplayFormat("yyyy-MM-dd HH:mm:ss");

    edPartId_ = new QLineEdit(this);                      // Qt 框架中的一个控件，用于在用户界面中接收和显示单行文本输入。
    edPartId_->setPlaceholderText("part_id contains..."); // 用于为QLineEdit控件设置当控件为空时显示的提示文本

    cbType_ = new QComboBox(this);        // 用于创建下拉列表，允许用户从多个选项中选择一个。
    cbType_->addItems({"ALL", "A", "B"}); // addItems用于向QComboBox或QListWidget等控件中添加多个选项

    cbOk_ = new QComboBox(this);
    cbOk_->addItems({"ALL", "OK", "NG"});

    cbMesStatus_ = new QComboBox(this);
    cbMesStatus_->addItems({"ALL", "NOT_QUEUED", "PENDING", "SENDING", "SENT", "FAILED"});

    btnQuery_ = new QPushButton("Query", this); // QPushButton 是 Qt 框架中的一个类，用于创建按钮控件
    btnUpload_ = new QPushButton("Upload Selected", this);
    btnRetry_ = new QPushButton("Retry FAILED (Selected)", this);

    connect(btnQuery_, &QPushButton::clicked, this, &MesUploadWidget::onQuery);
    connect(btnUpload_, &QPushButton::clicked, this, &MesUploadWidget::onUploadSelected);
    connect(btnRetry_, &QPushButton::clicked, this, &MesUploadWidget::onRetrySelectedFailed);

    // table
    table_ = new QTableView(this);                                                                                                                                       // 用于在图形用户界面中显示和编辑表格数据。它通常与 QStandardItemModel 一起使用，以提供灵活的数据展示和编辑功能。
    model_ = new QStandardItemModel(this);                                                                                                                               // 在 Qt 框架中使用的标准项模型类，用于管理表格视图中的数据。
    model_->setHorizontalHeaderLabels({"SEL", "measured_at_utc", "part_id", "type", "ok", "total_len_mm", "bc_len_mm", "mes_status", "attempts", "last_error", "uuid"}); // 此函数用于为QTableView设置水平方向的表头标签。
    table_->setModel(model_);                                                                                                                                            // 用于将数据模型与视图组件关联起来，以便在用户界面中展示数据。
    table_->setColumnHidden(10, true);                                                                                                                                   // uuid hidden,用于设置表格视图中的某一列是否对用户可见，隐藏界面表格中第11列的数据
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);                                                                                                         // 用于定义用户选择项目时的行为逻辑。在提供的代码上下文中，它被设置为SelectRows，意味着用户点击时会选中整行数据，而不是单个单元格。
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);                                                                                                      // 此方法用于定义用户如何与视图交互以选择项，例如单选、多选或扩展选择。

    auto *top = new QHBoxLayout(); // QHBoxLayout 是用于将子部件（widgets）沿水平方向从左到右排列的布局类。在当前代码上下文中，它被用于组织查询过滤器（如日期选择器、输入框和按钮）以及底部的操作按钮，使它们在界面上呈横向排列。
    top->addWidget(dtFrom_);
    top->addWidget(dtTo_);
    top->addWidget(edPartId_);
    top->addWidget(cbType_);
    top->addWidget(cbOk_);
    top->addWidget(cbMesStatus_);
    top->addWidget(btnQuery_);

    auto *bottom = new QHBoxLayout(); // QHBoxLayout 是用于将子部件（widgets）沿水平方向从左到右排列的布局类。在当前代码上下文中，它被用于组织上传按钮、重试按钮以及一个拉伸项（addStretch(1)），用于在界面上创建一个水平方向的间距，使按钮和拉伸项之间有一定的间距。
    bottom->addWidget(btnUpload_);
    bottom->addWidget(btnRetry_);
    bottom->addStretch(1); // 用于在 QHBoxLayout 中添加一个可伸缩的空间项，用于在界面上创建一个水平方向的间距，使按钮和拉伸项之间有一定的间距。

    auto *root = new QVBoxLayout(this); // QVBoxLayout 是用于将子部件（widgets）沿垂直方向从上到下排列的布局类。在当前代码上下文中，它被用于组织整个窗口的布局，包括顶部的查询过滤器、中间的表格视图和底部的操作按钮。
    root->addLayout(top);               // 将包含查询控件（日期选择器、输入框、下拉框和查询按钮）的水平布局放在界面的顶部。
    root->addWidget(table_, 1);         // 将表格视图（table_）添加到垂直布局（root）中，设置其占用的垂直空间比例为1。这意味着表格视图会占用剩余的垂直空间，而其他部件（如查询过滤器和操作按钮）会根据需要自动调整大小。
    root->addLayout(bottom);            // 将包含操作按钮（上传和重试按钮）的水平布局放在界面的底部。

    if (worker_)
    {
        connect(worker_, &MesWorker::outboxChanged, this, &MesUploadWidget::onOutboxChanged);
    }

    onQuery();
}

// 查询按钮点击时调用，根据过滤条件查询测量结果
void MesUploadWidget::onQuery()
{
    // 从界面控件获取当前的过滤条件，构建一个MesUploadFilter对象。
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

    // 使用过滤条件从数据库查询最多500条记录。
    QString e;
    auto rows = db_.queryMesUploadRows(f, 500, &e);
    if (!e.isEmpty())
    {
        QMessageBox::warning(this, "Query", e);
    }
    fillTable(rows); // 填充表格，根据查询结果更新界面，只要调用该函数，表格就会根据查询结果进行刷新。
}

// 填充表格，根据查询结果更新界面
void MesUploadWidget::fillTable(const QVector<core::MesUploadRow> &rows)
{
    model_->setRowCount(0); // 用于动态调整表格控件中显示的行数。在当前代码上下文中，它被用于在执行新查询前清空旧的数据行。
    for (const auto &r : rows)
    {
        QList<QStandardItem *> items; // 用于存储当前行的所有单元格数据项（QStandardItem）。QStandardItem 用于构建表格模型（QStandardItemModel）的每一行数据。它承载了复选框状态、文本内容（如时间戳、零件ID、状态等）以及隐藏的 UUID 数据，是 Qt 模型/视图架构中存储单元格数据的基本容器。

        auto *sel = new QStandardItem();   // 创建一个新的 QStandardItem 实例，用于表示表格中的复选框单元格。
        sel->setCheckable(true);           // 此方法用于定义一个控件（如QStandardItem或QPushButton）是否可以被用户勾选或切换状态。在提供的代码中，它用于初始化表格的第一列，使其显示为一个复选框。
        sel->setCheckState(Qt::Unchecked); // 用于编程方式控制表格或列表项中复选框的勾选状态（如选中、未选中或部分选中）。

        items << sel; // 将复选框项添加到 items 列表中，作为第一列的数据。
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

        model_->appendRow(items); // appendRow用于在表格或列表模型的最后一行之后插入新数据。在当前代码上下文中，它用于将从数据库查询到的测量数据逐条填充到UI界面的表格视图中。
    }
}

// 获取表格中选中行的 UUID 列表
QVector<QString> MesUploadWidget::selectedUuids() const
{
    QVector<QString> uuids;
    const int n = model_->rowCount(); // 获取表格模型中当前的行数，即查询结果的记录数。
    for (int i = 0; i < n; i++)       // 遍历表格模型的每一行，检查复选框是否被选中。
    {
        auto *sel = model_->item(i, 0);              // 获取表格模型中第i行第0列的单元格数据项（QStandardItem）。
        if (sel && sel->checkState() == Qt::Checked) // 检查复选框项是否存在（非空），并且其勾选状态是否为选中（Qt::Checked）。
        {
            uuids.push_back(model_->item(i, 10)->text()); // 如果复选框被选中，将第i行第10列（UUID 列）的文本内容添加到 uuids 向量中。
        }
    }
    return uuids;
}

// 上传按钮点击时调用，将选中的测量结果加入上传队列，触发 worker_->kick() 开始上传，刷新查询结果。
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

// 重试按钮点击时调用，将选中的失败测量结果重新加入上传队列，触发 worker_->kick() 开始上传，刷新查询结果。
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

// 上传队列状态变化时调用，自动刷新查询结果。
void MesUploadWidget::onOutboxChanged()
{
    // outbox 状态变化时自动刷新
    onQuery();
}
