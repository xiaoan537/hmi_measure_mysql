#pragma once
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariantMap>

#include <memory>

#include "core/config.hpp"
#include "core/plc_contract_v2.hpp"

class QLabel;
class DiagnosticsWidget;
class MesWorker;
class ProductionWidget;
class CalibrationWidget;
class DevToolsWidget;
class ManualMaintainWidget;
class SettingsWidget;

namespace core {
class PlcRuntimeServiceV2;
struct PlcMailboxSnapshot;
struct PlcPollEventsV26;
struct PlcRuntimeStatsV2;
struct PlcStatusBlockV2;
struct PlcTrayPartIdBlockV2;
}

namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(const core::AppConfig &cfg, const QString& iniPath, MesWorker *worker, QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void setupPlcRuntime(const core::AppConfig &cfg);
    void setupDiagnosticsBindings();
    void setupBusinessPageBindings();
    void updatePlcStatusLabel();
    void handlePlcRuntimeError(const QString &message);
    void onPlcStatsUpdated(const core::PlcRuntimeStatsV2 &stats);
    void onPlcStatusUpdated(const core::PlcStatusBlockV2 &status);
    void onPlcCommandUpdated(const core::PlcCommandBlockV2 &command);
    void onPlcTrayUpdated(const core::PlcTrayPartIdBlockV2 &tray);
    void onPlcMailboxSnapshotUpdated(const core::PlcMailboxSnapshot &snapshot);
    void onPlcEventsRaised(const core::PlcPollEventsV26 &events);
    void handleUiCommandRequested(const QString &cmd, const QVariantMap &args);
    void handleWriteTrayPartIdRequested(int slotIndex, const QString &partId);
    void handleReadMailboxRequested(QChar preferredPartType = QChar('A'),
                                    bool preferCalibrationContext = false);
    bool handleComputeResultRequested(QChar preferredPartType = QChar('A'),
                                      bool preferCalibrationContext = false,
                                      bool forceReloadMailbox = false);
    void handleAckMailboxRequested(bool preferCalibrationContext = false);
    void refreshManualMaintainLiveStatus();
    void appendProductionLog(const QString &text);
    void appendCalibrationLog(const QString &text);
    void attemptReconnectPlc(bool manual);
    void processAutoScanIdCheck();
    void processAutoMailboxFlow();
    void updateCalibrationAutoState(quint16 stepState);
    bool tryAutoContinueAfterIdCheck();
    bool tryAutoWritePcAck();
    void promptNgDecisionAndDispatch();
    qint16 currentCategoryModeForAutoFlow() const;
    bool evaluateIdCheckAgainstMes(QStringList *mismatchDetails, QVector<int> *mismatchSlots) const;
    bool loadMockExpectedPartIds(QVector<QString> *out, QString *err) const;
    QString resolveIdCheckMockFilePath() const;
    QString idCheckStrategyText() const;

private:
    enum class CalibrationAutoState {
        Idle = 0,
        WaitLoadSlot16,
        WaitPcConfirm,
        Measuring,
        WaitPcRead,
        Completed
    };
    enum class IdCheckStrategy {
        Bypass = 0,
        LocalMock,
        MesStrict
    };

private:
    Ui::MainWindow *ui_ = nullptr;
    QString iniPath_;
    DiagnosticsWidget *diagnosticsWidget_ = nullptr;
    DevToolsWidget *devToolsWidget_ = nullptr;
    ManualMaintainWidget *manualMaintainWidget_ = nullptr;
    ProductionWidget *productionWidget_ = nullptr;
    CalibrationWidget *calibrationWidget_ = nullptr;
    SettingsWidget *settingsWidget_ = nullptr;
    QLabel *lbDb_ = nullptr;
    QLabel *lbPlc_ = nullptr;
    QLabel *lbMes_ = nullptr;
    core::AppConfig appCfg_{};
    std::unique_ptr<core::PlcRuntimeServiceV2> plcRuntime_;
    quint16 lastMailboxReady_ = 0;
    core::PlcStatusBlockV2 lastStatus_{};
    std::unique_ptr<core::PlcMailboxSnapshot> lastMailboxSnapshot_;
    bool hasLastMailboxSnapshot_ = false;
    bool hasLastStatus_ = false;
    bool lastPlcConnectedKnown_ = false;
    bool lastPlcConnected_ = false;
    bool reconnectAttemptLogged_ = false;
    bool awaitingCmdReply_ = false;
    quint16 pendingCmdBits_ = 0;
    quint16 lastCmdResult_ = 0;
    quint16 lastRejectInstruction_ = 0;
    bool hasLastCommandSample_ = false;
    bool calibrationFlowExpected_ = false;
    qint16 lastCategoryMode_ = 0;
    core::PlcTrayPartIdBlockV2 lastTray_{};
    bool hasLastTray_ = false;
    QVector<QString> mesExpectedPartIds_;
    CalibrationAutoState calibrationAutoState_ = CalibrationAutoState::Idle;
    IdCheckStrategy idCheckStrategy_ = IdCheckStrategy::Bypass;
    bool lastComputeHasItems_ = false;
    bool lastComputeOverallOk_ = false;
    QChar lastComputePartType_ = QChar('A');
};
