#include "mainwindow.hpp"
#include "mes_upload_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

MainWindow::MainWindow(const core::AppConfig &cfg, MesWorker *worker, QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("HMI Measure");

    // 布局基础结构
    auto *central = new QWidget(this);       // 创建中央部件：首先创建一个QWidget作为主窗口的中央部件，所有内容都将放在这个部件中
    auto *layout = new QHBoxLayout(central); // 创建水平布局：创建一个QHBoxLayout（水平布局管理器），并将其应用于中央部件,QHBoxLayout会将子部件从左到右水平排列,这是实现左右分栏布局的关键

    // 左侧导航栏
    nav_ = new QListWidget(central);                                  // 创建导航列表：创建QListWidget作为导航栏
    nav_->addItems({"Production", "Data", "MES Upload", "Settings"}); // 添加导航项：添加四个导航选项
    nav_->setFixedWidth(160);                                         // 设置固定宽度：使用setFixedWidth(160)确保导航栏始终保持160像素宽度，不会随窗口大小变化而改变

    // 右侧内容区
    stack_ = new QStackedWidget(central); // 创建堆叠窗口：创建QStackedWidget作为右侧内容区,它可以包含多个子窗口部件,但一次只显示其中一个,通过切换索引来显示不同的内容

    // 添加页面：依次添加四个页面到堆叠窗口，前两个和第四个使用QLabel作为占位符，第三个使用MesUploadWidget实现实际功能
    stack_->addWidget(new QLabel("Production page (TODO)", central));
    stack_->addWidget(new QLabel("Data/Analysis page (TODO)", central));

    // MES page (MVP)
    stack_->addWidget(new MesUploadWidget(cfg, worker, central));

    // Settings placeholder (later we can put Dev tools / config)
    stack_->addWidget(new QLabel("Settings page (TODO)", central));

    // 布局组装
    layout->addWidget(nav_);      // 添加导航栏：将导航列表添加到水平布局的左侧
    layout->addWidget(stack_, 1); // 添加内容区：将堆叠窗口添加到水平布局的右侧，并设置拉伸因子为1，拉伸因子为1意味着当窗口大小变化时，堆叠窗口会占据所有额外的水平空间，导航栏保持固定宽度，内容区自适应窗口大小

    // 设置中央部件
    setCentralWidget(central); // 将包含所有子部件和布局的central部件设置为主窗口的中央部件，这样主窗口就会显示这些部件，这是Qt主窗口的标准做法，确保内容正确显示

    // 页面切换机制
    connect(nav_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex); // 信号槽连接：连接导航列表的currentRowChanged信号到堆叠窗口的setCurrentIndex槽，当用户点击导航列表中的不同项时，堆叠窗口会自动切换到对应索引的页面
    nav_->setCurrentRow(2);                                                                   // 设置默认页面：默认显示第2个页面（索引从0开始），即"MES Upload"页面
}
