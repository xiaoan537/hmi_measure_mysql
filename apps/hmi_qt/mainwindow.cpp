#include "mainwindow.hpp"
#include "mes_upload_widget.hpp"
#include "production_widget.hpp"
#include "mes_worker.hpp"

#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QStatusBar>

#include "ui_mainwindow.h"

MainWindow::MainWindow(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow)
{
    ui_->setupUi(this);
    setWindowTitle("HMI Measure");

    // Left nav items
    ui_->navList->clear();
    ui_->navList->addItems({"Production", "Data", "MES Upload", "Settings"});

    // Pages
    ui_->stackedWidget->addWidget(new ProductionWidget(cfg, ui_->stackedWidget));
    ui_->stackedWidget->addWidget(new QLabel("Data/Analysis page (TODO)", ui_->stackedWidget));
    ui_->stackedWidget->addWidget(new MesUploadWidget(cfg, worker, ui_->stackedWidget));
    ui_->stackedWidget->addWidget(new QLabel("Settings page (TODO)", ui_->stackedWidget));

    connect(ui_->navList, &QListWidget::currentRowChanged,
            ui_->stackedWidget, &QStackedWidget::setCurrentIndex);

    ui_->navList->setCurrentRow(0);

    // Status bar quick indicators (placeholder; will be wired to services later)
    auto *lbDb = new QLabel("DB: -", this);
    auto *lbPlc = new QLabel("PLC: -", this);
    auto *lbMes = new QLabel("MES: -", this);
    statusBar()->addPermanentWidget(lbDb);
    statusBar()->addPermanentWidget(lbPlc);
    statusBar()->addPermanentWidget(lbMes);
}

MainWindow::~MainWindow()
{
    delete ui_;
}
