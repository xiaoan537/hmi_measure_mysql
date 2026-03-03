#pragma once
#include <QMainWindow>

#include "core/config.hpp"
class MesWorker;

namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui_ = nullptr;
};
