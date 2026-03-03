#pragma once

#include <QWidget>

#include "core/config.hpp"

namespace Ui { class DataWidget; }

class DataWidget : public QWidget
{
  Q_OBJECT
public:
  explicit DataWidget(const core::AppConfig& cfg, QWidget* parent=nullptr);
  ~DataWidget() override;

signals:
  // Later: open raw file / queue mes upload
  void requestOpenRaw(const QString& measurement_uuid);
  void requestQueueMesUpload(const QString& measurement_uuid);

public slots:
  void refresh();

private slots:
  void onFilterChanged();
  void onSelectionChanged();
  void onOpenRawClicked();
  void onQueueMesClicked();

private:
  void setupModel();
  void applyRowToDetails(int row);

  Ui::DataWidget* ui_ = nullptr;
  core::AppConfig cfg_;

  class QStandardItemModel* model_ = nullptr;
};
