#pragma once
#include <QWidget>
#include <QStandardItemModel>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableView>

#include "core/config.hpp"
#include "core/db.hpp"

class MesWorker;

class MesUploadWidget : public QWidget
{
    Q_OBJECT
public:
    MesUploadWidget(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent = nullptr);

private slots:
    void onQuery();
    void onUploadSelected();
    void onRetrySelectedFailed();
    void onOutboxChanged();

private:
    QVector<QString> selectedUuids() const;
    void fillTable(const QVector<core::MesUploadRow> &rows);

    core::AppConfig cfg_;
    MesWorker *worker_ = nullptr;

    core::Db db_;

    // UI
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
    QStandardItemModel *model_ = nullptr;
};
