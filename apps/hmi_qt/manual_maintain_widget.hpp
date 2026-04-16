#pragma once

#include <QWidget>
#include <QVariantMap>

namespace Ui {
class ManualMaintainWidget;
}

class ManualMaintainWidget : public QWidget {
  Q_OBJECT
public:
  explicit ManualMaintainWidget(QWidget *parent = nullptr);
  ~ManualMaintainWidget() override;

signals:
  void requestSetPlcMode(int mode);
  void requestPlcNamedCommand(const QString &cmd, const QVariantMap &args);
  void requestPlcReloadSlotIds();
  void requestPlcReadMailbox();
  void requestPlcAckMailbox();
  void requestPlcContinueAfterIdCheck();
  void requestAxisCommand(int axisIndex, const QString &action);
  void requestAxisJog(int axisIndex, const QString &direction, bool active);
  void requestAxisMove(int axisIndex, const QString &action,
                       double acc, double dec, double pos, double vel);
  void requestCylinderCommand(const QString &group, int index, const QString &action);

public slots:
  void setCurrentPlcMode(int mode);
  void setRuntimeSummary(bool connected, const QString &machineText, const QString &stepText);
  void setAxisStatesText(const QString &text);
  void setCylinderStatesText(const QString &text);
  void appendLog(const QString &text);

private:
  Ui::ManualMaintainWidget *ui_ = nullptr;
};
