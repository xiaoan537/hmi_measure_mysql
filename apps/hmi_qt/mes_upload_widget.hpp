#pragma once
#include <QWidget>
#include <QStandardItemModel>

#include "core/config.hpp"
#include "core/db.hpp"

class MesWorker;

namespace Ui { class MesUploadWidget; }

class MesUploadWidget : public QWidget
{
    Q_OBJECT
public:
    MesUploadWidget(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent = nullptr);
    ~MesUploadWidget() override;

private slots:
    void onQuery();
    void onUploadSelected();
    void onRetrySelectedFailed();
    void onOutboxChanged();

private:
    QVector<QString> selectedUuids() const;
    void fillTable(const QVector<core::MesUploadRow> &rows);

    Ui::MesUploadWidget *ui_ = nullptr;

    core::AppConfig cfg_;
    MesWorker *worker_ = nullptr;
    core::Db db_;

    class QLineEdit *edTaskCard_ = nullptr;
    QStandardItemModel *model_ = nullptr;
};
