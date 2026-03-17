#pragma once

#include <QWidget>

#include "core/config.hpp"
#include "core/db.hpp"

namespace Ui {
class DataWidget;
}

class DataWidget : public QWidget {
  Q_OBJECT
public:
  explicit DataWidget(const core::AppConfig &cfg, QWidget *parent = nullptr);
  ~DataWidget() override;

signals:
  void requestOpenRaw(const QString &measurement_uuid);
  void requestQueueMesUpload(const QString &measurement_uuid);

public slots:
  void refresh();

private slots:
  void onFilterChanged();
  void onSelectionChanged();
  void onOpenRawClicked();
  void onQueueMesClicked();

private:
  void setupModel();
  void reloadFromNewMeasurementSchema();
  void showMeasurementDetail(quint64 measurementId);
  QString makeMeasurementSummary(const core::MeasurementListRowEx &row) const;

  Ui::DataWidget *ui_ = nullptr;
  core::AppConfig cfg_;

  class QLineEdit *editTaskCard_ = nullptr;
  class QStandardItemModel *model_ = nullptr;
};