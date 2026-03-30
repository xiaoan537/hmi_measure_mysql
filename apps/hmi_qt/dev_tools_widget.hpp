#pragma once

#include <QWidget>
#include <QtGlobal>

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
};
