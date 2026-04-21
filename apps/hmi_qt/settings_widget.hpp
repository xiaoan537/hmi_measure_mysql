#pragma once

#include <QWidget>

#include "core/config.hpp"

class QDoubleSpinBox;
class QGridLayout;

namespace Ui { class SettingsWidget; }

class SettingsWidget : public QWidget
{
  Q_OBJECT
public:
  explicit SettingsWidget(const core::AppConfig& cfg, const QString& iniPath, QWidget* parent=nullptr);
  ~SettingsWidget() override;

signals:
  void configApplied(const core::AppConfig& cfg);
  void configSaved(const QString& iniPath);

private slots:
  void onReloadClicked();
  void onApplyClicked();
  void onSaveClicked();
  void onTestDbClicked();

private:
  struct SpecEditors {
    QDoubleSpinBox* a_total_len_std = nullptr;
    QDoubleSpinBox* a_total_len_tol = nullptr;
    QDoubleSpinBox* a_id_left_std = nullptr;
    QDoubleSpinBox* a_id_left_tol = nullptr;
    QDoubleSpinBox* a_od_left_std = nullptr;
    QDoubleSpinBox* a_od_left_tol = nullptr;
    QDoubleSpinBox* a_id_right_std = nullptr;
    QDoubleSpinBox* a_id_right_tol = nullptr;
    QDoubleSpinBox* a_od_right_std = nullptr;
    QDoubleSpinBox* a_od_right_tol = nullptr;

    QDoubleSpinBox* b_ad_len_std = nullptr;
    QDoubleSpinBox* b_ad_len_tol = nullptr;
    QDoubleSpinBox* b_bc_len_std = nullptr;
    QDoubleSpinBox* b_bc_len_tol = nullptr;
    QDoubleSpinBox* b_runout_left_std = nullptr;
    QDoubleSpinBox* b_runout_left_tol = nullptr;
    QDoubleSpinBox* b_runout_right_std = nullptr;
    QDoubleSpinBox* b_runout_right_tol = nullptr;
  };

  void setupCalibrationJudgeEditors();
  static QDoubleSpinBox* makeSpecSpinBox(QWidget* parent, double min, double max, double value = 0.0);
  static void setSpecRow(QGridLayout* grid, int row, const QString& title, QDoubleSpinBox*& stdSpin, QDoubleSpinBox*& tolSpin);

  void loadToUi(const core::AppConfig& cfg);
  core::AppConfig readFromUi() const;
  bool saveToIni(const core::AppConfig& cfg, QString* err);

  Ui::SettingsWidget* ui_ = nullptr;
  core::AppConfig cfg_;
  QString iniPath_;
  SpecEditors calSpecEditors_;
};
