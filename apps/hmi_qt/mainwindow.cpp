#include "mainwindow.hpp"
#include "mes_upload_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

MainWindow::MainWindow(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("HMI Measure");

    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);

    nav_ = new QListWidget(central);
    nav_->addItems({"Production", "Data", "MES Upload", "Settings"});
    nav_->setFixedWidth(160);

    stack_ = new QStackedWidget(central);

    // placeholders
    stack_->addWidget(new QLabel("Production page (TODO)", central));
    stack_->addWidget(new QLabel("Data/Analysis page (TODO)", central));

    // MES page (MVP)
    stack_->addWidget(new MesUploadWidget(cfg, worker, central));

    // Settings placeholder (later we can put Dev tools / config)
    stack_->addWidget(new QLabel("Settings page (TODO)", central));

    layout->addWidget(nav_);
    layout->addWidget(stack_, 1);

    setCentralWidget(central);

    connect(nav_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    nav_->setCurrentRow(2); // 默认打开 MES Upload
}
