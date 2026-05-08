#pragma once

#include <QString>

class QApplication;

namespace core {
struct AppConfig;
}

int runDevAbWindow(QApplication &app, const core::AppConfig &cfg);

// 命令行开发工具入口：./hmi_qt --dev-ab
