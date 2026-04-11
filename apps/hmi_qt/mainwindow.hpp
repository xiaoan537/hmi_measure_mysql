#pragma once
#include <QMainWindow>
#include <QString>
#include <QVector>
#include <QVariantMap>

#include <memory>

#include "core/config.hpp"
#include "core/plc_contract_v2.hpp"
#include "core/plc_polling_v2.hpp"

class QLabel;
class DiagnosticsWidget;
class MesWorker;
class ProductionWidget;
class CalibrationWidget;
class DevToolsWidget;

namespace core {
class PlcRuntimeServiceV2;
class FakePlcRegisterClientV2;
struct PlcMailboxSnapshot;
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
    void seedFakePlcDemoData();
    void handlePlcRuntimeError(const QString &message);
    void onPlcStatsUpdated(const core::PlcRuntimeStatsV2 &stats);
    void onPlcStatusUpdated(const core::PlcStatusBlockV2 &status);
    void onPlcTrayUpdated(const core::PlcTrayPartIdBlockV2 &tray);
    void onPlcMailboxSnapshotUpdated(const core::PlcMailboxSnapshot &snapshot);
    void onPlcEventsRaised(const core::PlcPollEventsV2 &events);
    void onPlcFlowModeChanged(int mode);
    void handleUiCommandRequested(const QString &cmd, const QVariantMap &args);
    void handleWriteTrayPartIdsRequested(const QVector<QString> &slotIds);
    void handleReadMailboxRequested(QChar preferredPartType = QChar('A'));
    void handleAckMailboxRequested();

private:
    Ui::MainWindow *ui_ = nullptr;
    QString iniPath_;
    DiagnosticsWidget *diagnosticsWidget_ = nullptr;
    DevToolsWidget *devToolsWidget_ = nullptr;
    ProductionWidget *productionWidget_ = nullptr;
    CalibrationWidget *calibrationWidget_ = nullptr;
    QLabel *lbDb_ = nullptr;
    QLabel *lbPlc_ = nullptr;
    QLabel *lbMes_ = nullptr;
    std::unique_ptr<core::PlcRuntimeServiceV2> plcRuntime_;
    core::FakePlcRegisterClientV2 *fakePlcClient_ = nullptr;
    quint32 plcCommandSeq_ = 1;
    quint16 lastMailboxReady_ = 0;
    quint32 lastMailboxSeq_ = 0;
    int plcFlowMode_ = 0;
    quint32 lastAutoContinueScanSeq_ = 0;
    quint32 lastAutoAckMeasSeq_ = 0;
    core::PlcStatusBlockV2 lastStatus_{};
    bool hasLastStatus_ = false;
};
