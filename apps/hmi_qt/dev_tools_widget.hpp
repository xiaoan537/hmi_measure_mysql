#pragma once

#include <QWidget>
#include <QtGlobal>

#include "core/config.hpp"

namespace Ui {
class DevToolsWidget;
}


enum class PlcFlowModeUi : int {
  Manual = 0,
  SemiAuto = 1,
  FullAuto = 2,
};

class DevToolsWidget : public QWidget {
  Q_OBJECT
public:
  explicit DevToolsWidget(const core::AppConfig &cfg, QWidget *parent = nullptr);
  ~DevToolsWidget() override;

signals:
  void requestPlcPollOnce();
  void requestPlcAckMailbox();
  void requestPlcContinueAfterIdCheck();
  void requestPlcRequestRescanIds();
  void plcFlowModeChanged(int mode);

public slots:
  void setPlcFlowMode(int mode);
  void setPlcRuntimeSummary(bool connected, const QString &machineText,
                            const QString &stepText, quint32 scanSeq,
                            quint32 measSeq);
  void appendPlcLog(const QString &text);

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
  QString plcFlowModeText(int mode) const;

private:
  Ui::DevToolsWidget *ui_ = nullptr;
  core::AppConfig cfg_;
  class QComboBox *plcFlowCombo_ = nullptr;
  class QLabel *lbPlcConn_ = nullptr;
  class QLabel *lbPlcMachine_ = nullptr;
  class QLabel *lbPlcStep_ = nullptr;
  class QLabel *lbPlcSeq_ = nullptr;
};
