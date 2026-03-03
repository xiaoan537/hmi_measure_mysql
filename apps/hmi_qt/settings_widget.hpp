#pragma once

#include <QWidget>

#include "core/config.hpp"

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
  void loadToUi(const core::AppConfig& cfg);
  core::AppConfig readFromUi() const;
  bool saveToIni(const core::AppConfig& cfg, QString* err);

  Ui::SettingsWidget* ui_ = nullptr;
  core::AppConfig cfg_;
  QString iniPath_;
};
