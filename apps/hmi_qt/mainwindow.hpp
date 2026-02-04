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
    MainWindow(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent = nullptr); // 构造函数接收配置、工作线程和父窗口参数

private:
    QListWidget *nav_ = nullptr;      // QListWidget 被用作 MainWindow 类的一个私有成员变量 nav_。它通常在 GUI 应用程序中充当导航栏，允许用户通过点击列表中的项目来切换 QStackedWidget 中显示的页面。
    QStackedWidget *stack_ = nullptr; // QStackedWidget 是 Qt 框架中的一个容器类，用于管理多个子窗口部件，但一次只显示其中一个。它通常与左侧的导航栏（QListWidget）配合使用，实现点击导航项切换右侧显示内容的功能。
};
