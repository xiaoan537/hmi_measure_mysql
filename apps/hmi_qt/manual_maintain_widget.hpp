#pragma once

#include <QWidget>
#include <QVariantMap>

class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QPlainTextEdit;

class ManualMaintainWidget : public QWidget {
  Q_OBJECT
public:
  explicit ManualMaintainWidget(QWidget *parent = nullptr);

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
  int selectedPartType() const;
  int selectedTargetMode() const;

private:
  QLabel *lbConn_ = nullptr;
  QLabel *lbMachine_ = nullptr;
  QLabel *lbStep_ = nullptr;
  QLabel *lbCurrentMode_ = nullptr;
  QComboBox *targetModeCombo_ = nullptr;
  QComboBox *cmdPartTypeCombo_ = nullptr;
  QComboBox *axisCombo_ = nullptr;
  QComboBox *cylCombo_ = nullptr;
  QDoubleSpinBox *spAcc_ = nullptr;
  QDoubleSpinBox *spDec_ = nullptr;
  QDoubleSpinBox *spPos_ = nullptr;
  QDoubleSpinBox *spVel_ = nullptr;
  QPlainTextEdit *axisStateEdit_ = nullptr;
  QPlainTextEdit *cylStateEdit_ = nullptr;
  QPlainTextEdit *logEdit_ = nullptr;
};
