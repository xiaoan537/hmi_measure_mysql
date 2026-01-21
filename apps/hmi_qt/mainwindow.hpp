#pragma once
#include <QMainWindow>
#include <QListWidget>
#include <QStackedWidget>

#include "core/config.hpp"
class MesWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent = nullptr);

private:
    QListWidget *nav_ = nullptr;
    QStackedWidget *stack_ = nullptr;
};
