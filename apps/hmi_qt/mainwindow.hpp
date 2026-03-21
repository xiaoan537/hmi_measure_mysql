#pragma once
#include <QMainWindow>
#include <QString>
#include <QVector>
#include <QVariantMap>

#include <memory>

#include "core/config.hpp"

class QLabel;
class DiagnosticsWidget;
class MesWorker;
class ProductionWidget;
class CalibrationWidget;

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
    void handleUiCommandRequested(const QString &cmd, const QVariantMap &args);
    void handleWriteTrayPartIdsRequested(const QVector<QString> &slotIds);
    void handleAckMailboxRequested();

private:
    Ui::MainWindow *ui_ = nullptr;
    QString iniPath_;
    DiagnosticsWidget *diagnosticsWidget_ = nullptr;
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
};
