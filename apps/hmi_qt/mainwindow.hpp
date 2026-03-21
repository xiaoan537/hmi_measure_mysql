#pragma once
#include <QMainWindow>
#include <QString>

#include <memory>

#include "core/config.hpp"

class QLabel;
class DiagnosticsWidget;
class MesWorker;

namespace core {
class PlcRuntimeServiceV2;
class FakePlcRegisterClientV2;
struct PlcMailboxSnapshot;
struct PlcRuntimeStatsV2;
struct PlcStatusBlockV2;
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
    void updatePlcStatusLabel();
    void seedFakePlcDemoData();
    void handlePlcRuntimeError(const QString &message);
    void onPlcStatsUpdated(const core::PlcRuntimeStatsV2 &stats);
    void onPlcStatusUpdated(const core::PlcStatusBlockV2 &status);
    void onPlcMailboxSnapshotUpdated(const core::PlcMailboxSnapshot &snapshot);

private:
    Ui::MainWindow *ui_ = nullptr;
    QString iniPath_;
    DiagnosticsWidget *diagnosticsWidget_ = nullptr;
    QLabel *lbDb_ = nullptr;
    QLabel *lbPlc_ = nullptr;
    QLabel *lbMes_ = nullptr;
    std::unique_ptr<core::PlcRuntimeServiceV2> plcRuntime_;
    core::FakePlcRegisterClientV2 *fakePlcClient_ = nullptr;
};
