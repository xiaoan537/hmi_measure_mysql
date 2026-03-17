#include "alarm_widget.hpp"
#include "calibration_widget.hpp"
#include "data_widget.hpp"
#include "dev_tools_widget.hpp"
#include "diagnostics_widget.hpp"
#include "mainwindow.hpp"
#include "mes_upload_widget.hpp"
#include "mes_worker.hpp"
#include "production_widget.hpp"
#include "raw_viewer_widget.hpp"
#include "settings_widget.hpp"
#include "todo_widget.hpp"

#include <QAction>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QStatusBar>

#include <QSizePolicy>

#include "ui_mainwindow.h"

MainWindow::MainWindow(const core::AppConfig &cfg, const QString &iniPath,
                       MesWorker *worker, QWidget *parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow), iniPath_(iniPath) {
  ui_->setupUi(this);

  // MainWindow ctor, setupUi 后
  setMinimumSize(800, 500);
  setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

  if (centralWidget()) {
    centralWidget()->setMinimumSize(0, 0);
    centralWidget()->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
  }

  ui_->stackedWidget->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
  ui_->navList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

  setWindowTitle(QStringLiteral("工件自动测量装置"));

  ui_->navList->clear();
  ui_->navList->addItems(
      {QStringLiteral("生产"), QStringLiteral("标定"), QStringLiteral("数据"),
       QStringLiteral("MES上传"), QStringLiteral("设置"),
       QStringLiteral("报警/事件"), QStringLiteral("诊断"),
       QStringLiteral("RAW查看"), QStringLiteral("开发调试"),
       QStringLiteral("手动(待做)"), QStringLiteral("报表(待做)"),
       QStringLiteral("用户权限(待做)")});

  ui_->stackedWidget->addWidget(new ProductionWidget(cfg, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(new CalibrationWidget(cfg, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(new DataWidget(cfg, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(
      new MesUploadWidget(cfg, worker, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(
      new SettingsWidget(cfg, iniPath_, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(new AlarmWidget(cfg, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(new DiagnosticsWidget(cfg, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(new RawViewerWidget(cfg, ui_->stackedWidget));
  ui_->stackedWidget->addWidget(new DevToolsWidget(cfg, ui_->stackedWidget));

  ui_->stackedWidget->addWidget(new TodoWidget(
      QStringLiteral("手动/维护（TODO）"),
      QStringLiteral("后续将增加手动读写槽位、单步调试、回零等维护功能。"),
      ui_->stackedWidget));
  ui_->stackedWidget->addWidget(new TodoWidget(
      QStringLiteral("统计报表（TODO）"),
      QStringLiteral("后续将增加良率、趋势、SPC/CPK 等统计视图。"),
      ui_->stackedWidget));
  ui_->stackedWidget->addWidget(
      new TodoWidget(QStringLiteral("用户与权限（TODO）"),
                     QStringLiteral("后续将增加登录、角色权限控制与操作审计。"),
                     ui_->stackedWidget));

  connect(ui_->navList, &QListWidget::currentRowChanged, ui_->stackedWidget,
          &QStackedWidget::setCurrentIndex);

  ui_->navList->setCurrentRow(0);

  auto *lbDb = new QLabel(QStringLiteral("数据库: -"), this);
  auto *lbPlc = new QLabel(QStringLiteral("PLC: -"), this);
  auto *lbMes = new QLabel(QStringLiteral("MES: -"), this);
  statusBar()->addPermanentWidget(lbDb);
  statusBar()->addPermanentWidget(lbPlc);
  statusBar()->addPermanentWidget(lbMes);

  auto *actToggleFullScreen = new QAction(this);
  actToggleFullScreen->setShortcut(QKeySequence(Qt::Key_F11));
  addAction(actToggleFullScreen);
  connect(actToggleFullScreen, &QAction::triggered, this, [this] {
    if (isFullScreen()) {
      showNormal();
    } else {
      showFullScreen();
    }
  });

  auto *actExitFullScreen = new QAction(this);
  actExitFullScreen->setShortcut(QKeySequence(Qt::Key_Escape));
  addAction(actExitFullScreen);
  connect(actExitFullScreen, &QAction::triggered, this, [this] {
    if (isFullScreen()) {
      showNormal();
    }
  });
}

MainWindow::~MainWindow() { delete ui_; }
