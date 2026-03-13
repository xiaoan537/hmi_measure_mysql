#pragma once

#include <QWidget>

#include "core/config.hpp"

namespace Ui {
class DevToolsWidget;
}

class DevToolsWidget : public QWidget {
  Q_OBJECT
public:
  explicit DevToolsWidget(const core::AppConfig &cfg, QWidget *parent = nullptr);
  ~DevToolsWidget() override;

private slots:
  void onInsertATest();
  void onInsertBTest();
  void onRunSmoke();
  void onQueryLatest();
  void onClearLog();

private:
  bool insertViaIngest(const QString &partType, const QString &partId,
                       QString *err);
  void appendLog(const QString &text);

private:
  Ui::DevToolsWidget *ui_ = nullptr;
  core::AppConfig cfg_;
};
