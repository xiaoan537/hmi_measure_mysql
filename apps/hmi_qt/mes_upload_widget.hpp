#pragma once
#include <QWidget>
#include <QStandardItemModel>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableView>

#include "core/config.hpp"
#include "core/db.hpp" //头文件中：包含了 Qt 界面相关的类：QWidget、QStandardItemModel、各种输入控件和表格视图

class MesWorker;

class MesUploadWidget : public QWidget
{
    // 使用Q_OBJECT宏，支持Qt的信号槽机制，使得该类能够定义信号和槽函数，进行对象间的通信和事件处理。
    Q_OBJECT
public:
    MesUploadWidget(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent = nullptr);

private slots:
    void onQuery();               // 查询按钮点击时调用，根据过滤条件查询测量结果
    void onUploadSelected();      // 上传按钮点击时调用，将选中的测量结果加入上传队列
    void onRetrySelectedFailed(); // 重试按钮点击时调用，重试选中的失败任务
    void onOutboxChanged();       // 当 MES 工作线程发出状态变化信号时调用，刷新界面

private:
    QVector<QString> selectedUuids() const;                  // 获取表格中选中行的 UUID 列表
    void fillTable(const QVector<core::MesUploadRow> &rows); // 填充表格，根据查询结果更新界面

    core::AppConfig cfg_;
    MesWorker *worker_ = nullptr;

    core::Db db_;

    // UI控件包括：时间范围选择器、零件ID输入框、类型选择框、OK/NG选择框、测量状态选择框、查询按钮、上传按钮、重试按钮、表格视图
    QDateTimeEdit *dtFrom_ = nullptr;
    QDateTimeEdit *dtTo_ = nullptr;
    QLineEdit *edPartId_ = nullptr;
    QComboBox *cbType_ = nullptr;
    QComboBox *cbOk_ = nullptr;
    QComboBox *cbMesStatus_ = nullptr;

    QPushButton *btnQuery_ = nullptr;
    QPushButton *btnUpload_ = nullptr;
    QPushButton *btnRetry_ = nullptr;

    QTableView *table_ = nullptr;
    QStandardItemModel *model_ = nullptr; // 表格数据模型，用于存储查询结果
};
