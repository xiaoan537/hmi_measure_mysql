#pragma once
#include <QWidget>
#include "core/config.hpp"

namespace Ui { class AlarmWidget; }

class AlarmWidget : public QWidget {
  Q_OBJECT
public:
  explicit AlarmWidget(const core::AppConfig& cfg, QWidget* parent=nullptr);
  ~AlarmWidget() override;

signals:
  void requestResetAlarm();
  void requestRefresh();

public slots:
  void setCurrentAlarm(int alarmCode, int alarmLevel, quint32 interlockMask);
  void addEventRow(const QString& time, const QString& type, const QString& message);

private:
  Ui::AlarmWidget* ui_{};
  core::AppConfig cfg_;
};
