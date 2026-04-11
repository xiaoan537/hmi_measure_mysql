#pragma once

#include <QWidget>
#include <QtGlobal>
#include <QVariantMap>

#include "core/config.hpp"
#include "core/measurement_geometry_algorithms.hpp"

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
  void requestPlcReloadSlotIds();
  void requestPlcReadMailbox();
  void requestPlcAckMailbox();
  void requestPlcContinueAfterIdCheck();
  void requestPlcRequestRescanIds();
  void plcFlowModeChanged(int mode);
  void requestSetPlcMode(int mode);
  void requestPlcNamedCommand(const QString &cmd, const QVariantMap &args);
  void requestAxisCommand(int axisIndex, const QString &action);
  void requestReadAxisStatus(int axisIndex);
  void requestCylinderCommand(const QString &group, int index, const QString &action);
  void requestReadCylinderStatus();

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
  void onLoadAlgorithmJson();
  void onRunAlgorithmFromInput();
  void onFillAlgorithmExample();
  void onRunRunoutFromInput();
  void onFillRunoutExample();

private:
  bool insertViaIngest(const QString &partType, const QString &partId,
                       QString *err);
  void appendLog(const QString &text);
  QString plcFlowModeText(int mode) const;
  void refreshPlcActionEnableStates();

  bool parseDoubleSeriesText(const QString &text, QVector<double> *values,
                             QString *err) const;
  bool parseBoolSeriesText(const QString &text, int expectedSize,
                           QVector<bool> *values, QString *err) const;
  QVector<bool> defaultValidMask(int count) const;
  void loadAlgorithmJsonObject(const class QJsonObject &obj);
  QString summarizeCircleFit(const QString &title,
                             const core::DiameterChannelResult &r) const;
  QString summarizeThickness(const core::ThicknessResult &r) const;
  QString summarizeHarmonics(const QString &title,
                             const core::HarmonicAnalysisResult &r) const;
  QString summarizeRunout(const core::RunoutResult &r, int primaryMode) const;

private:
  Ui::DevToolsWidget *ui_ = nullptr;
  core::AppConfig cfg_;
  class QComboBox *plcFlowCombo_ = nullptr;
  class QLabel *lbPlcConn_ = nullptr;
  class QLabel *lbPlcMachine_ = nullptr;
  class QLabel *lbPlcStep_ = nullptr;
  class QLabel *lbPlcSeq_ = nullptr;
  class QPushButton *btnPlcPoll_ = nullptr;
  class QPushButton *btnPlcReloadIds_ = nullptr;
  class QPushButton *btnPlcContinue_ = nullptr;
  class QPushButton *btnPlcRescan_ = nullptr;
  class QPushButton *btnPlcReadMailbox_ = nullptr;
  class QPushButton *btnPlcAck_ = nullptr;

  class QComboBox *plcModeCombo_ = nullptr;
  class QPushButton *btnPlcWriteMode_ = nullptr;
  class QComboBox *cmdPartTypeCombo_ = nullptr;
  class QComboBox *axisCombo_ = nullptr;
  class QPushButton *btnAxisEnable_ = nullptr;
  class QPushButton *btnAxisReset_ = nullptr;
  class QPushButton *btnAxisHome_ = nullptr;
  class QPushButton *btnAxisStop_ = nullptr;
  class QPushButton *btnAxisJogFwd_ = nullptr;
  class QPushButton *btnAxisJogBwd_ = nullptr;
  class QPushButton *btnAxisReadSta_ = nullptr;
  class QComboBox *cylCombo_ = nullptr;
  class QPushButton *btnCylP_ = nullptr;
  class QPushButton *btnCylN_ = nullptr;
  class QPushButton *btnCylReset_ = nullptr;
  class QPushButton *btnCylReadSta_ = nullptr;

  class QPlainTextEdit *teAlgoInnerRaw_ = nullptr;
  class QPlainTextEdit *teAlgoOuterRaw_ = nullptr;
  class QPlainTextEdit *teAlgoInnerValid_ = nullptr;
  class QPlainTextEdit *teAlgoOuterValid_ = nullptr;
  class QDoubleSpinBox *spAlgoKIn_ = nullptr;
  class QDoubleSpinBox *spAlgoKOut_ = nullptr;
  class QCheckBox *cbAlgoUseExplicitKOut_ = nullptr;
  class QDoubleSpinBox *spAlgoProbeBase_ = nullptr;
  class QDoubleSpinBox *spAlgoAngleOffset_ = nullptr;
  class QDoubleSpinBox *spAlgoResidualIn_ = nullptr;
  class QDoubleSpinBox *spAlgoResidualOut_ = nullptr;
  class QPushButton *btnAlgoLoadJson_ = nullptr;
  class QPushButton *btnAlgoRun_ = nullptr;
  class QPushButton *btnAlgoFillExample_ = nullptr;

  class QPlainTextEdit *teRunoutRaw_ = nullptr;
  class QPlainTextEdit *teRunoutValid_ = nullptr;
  class QDoubleSpinBox *spRunoutK_ = nullptr;
  class QDoubleSpinBox *spRunoutAngleOffset_ = nullptr;
  class QDoubleSpinBox *spRunoutResidual_ = nullptr;
  class QDoubleSpinBox *spRunoutVAngle_ = nullptr;
  class QSpinBox *spRunoutInterp_ = nullptr;
  class QComboBox *cbRunoutPrimary_ = nullptr;
  class QPushButton *btnRunoutRun_ = nullptr;
  class QPushButton *btnRunoutFillExample_ = nullptr;
};
