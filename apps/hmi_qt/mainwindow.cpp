#include "mainwindow.hpp"
#include "mes_upload_widget.hpp"
#include "production_widget.hpp"
#include "mes_worker.hpp"
#include "data_widget.hpp"
#include "settings_widget.hpp"
#include "alarm_widget.hpp"
#include "diagnostics_widget.hpp"
#include "raw_viewer_widget.hpp"
#include "todo_widget.hpp"

#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QStatusBar>

#include "ui_mainwindow.h"

MainWindow::MainWindow(const core::AppConfig &cfg, const QString& iniPath, MesWorker *worker, QWidget *parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow), iniPath_(iniPath)
{
    ui_->setupUi(this);
    setWindowTitle(QStringLiteral("工件自动测量装置"));

    // Left nav items
    ui_->navList->clear();
    ui_->navList->addItems({QStringLiteral("生产"), QStringLiteral("数据"), QStringLiteral("MES上传"), QStringLiteral("设置"), QStringLiteral("报警/事件"), QStringLiteral("诊断"), QStringLiteral("RAW查看"), QStringLiteral("手动(待做)"), QStringLiteral("报表(待做)"), QStringLiteral("用户权限(待做)")});

    // Pages
    ui_->stackedWidget->addWidget(new ProductionWidget(cfg, ui_->stackedWidget));

    auto* dataPage = new DataWidget(cfg, ui_->stackedWidget);
    ui_->stackedWidget->addWidget(dataPage);

    ui_->stackedWidget->addWidget(new MesUploadWidget(cfg, worker, ui_->stackedWidget));

    auto* settingsPage = new SettingsWidget(cfg, iniPath_, ui_->stackedWidget);
    ui_->stackedWidget->addWidget(settingsPage);


// Alarm / Events
ui_->stackedWidget->addWidget(new AlarmWidget(cfg, ui_->stackedWidget));

// Diagnostics / PLC monitor
ui_->stackedWidget->addWidget(new DiagnosticsWidget(cfg, ui_->stackedWidget));

// RAW viewer
ui_->stackedWidget->addWidget(new RawViewerWidget(cfg, ui_->stackedWidget));

// TODO placeholders
ui_->stackedWidget->addWidget(new TodoWidget(QStringLiteral("手动/维护（TODO）"),
    QStringLiteral("后续将增加手动读写槽位、单步调试、回零等维护功能。"), ui_->stackedWidget));
ui_->stackedWidget->addWidget(new TodoWidget(QStringLiteral("统计报表（TODO）"),
    QStringLiteral("后续将增加良率、趋势、SPC/CPK 等统计视图。"), ui_->stackedWidget));
ui_->stackedWidget->addWidget(new TodoWidget(QStringLiteral("用户与权限（TODO）"),
    QStringLiteral("后续将增加登录、角色权限控制与操作审计。"), ui_->stackedWidget));

    connect(ui_->navList, &QListWidget::currentRowChanged,
            ui_->stackedWidget, &QStackedWidget::setCurrentIndex);

    ui_->navList->setCurrentRow(0);

    // Status bar quick indicators (placeholder; will be wired to services later)
    auto *lbDb = new QLabel(QStringLiteral("数据库: -"), this);
    auto *lbPlc = new QLabel(QStringLiteral("PLC: -"), this);
    auto *lbMes = new QLabel(QStringLiteral("MES: -"), this);
    statusBar()->addPermanentWidget(lbDb);
    statusBar()->addPermanentWidget(lbPlc);
    statusBar()->addPermanentWidget(lbMes);
}

MainWindow::~MainWindow()
{
    delete ui_;
}
