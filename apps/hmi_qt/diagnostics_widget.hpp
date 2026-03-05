#pragma once
#include <QWidget>
#include "core/config.hpp"

namespace Ui { class DiagnosticsWidget; }

class DiagnosticsWidget : public QWidget {
  Q_OBJECT
public:
  explicit DiagnosticsWidget(const core::AppConfig& cfg, QWidget* parent=nullptr);
  ~DiagnosticsWidget() override;

signals:
  void requestRefresh();
  void requestReadMailbox();
  void requestAckMailbox();

public slots:
  void setCommStats(int pollHz, int lastMs, int okCount, int errCount);
  void setStatusFields(int stepState, int machineState, int alarmCode, int alarmLevel, quint32 interlockMask, int measSeq);
  void setMailboxPreview(const QString& partType, const QString& slot0, const QString& slot1, const QString& partId0, const QString& partId1);

private:
  Ui::DiagnosticsWidget* ui_{};
  core::AppConfig cfg_;
};
