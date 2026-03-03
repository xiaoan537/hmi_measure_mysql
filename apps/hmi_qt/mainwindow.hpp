#pragma once
#include <QMainWindow>
#include <QString>

#include "core/config.hpp"
class MesWorker;

namespace Ui { class MainWindow; }

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(const core::AppConfig &cfg, const QString& iniPath, MesWorker *worker, QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui_ = nullptr;
    QString iniPath_;
};
