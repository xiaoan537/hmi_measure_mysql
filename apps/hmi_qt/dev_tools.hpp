#pragma once

#include <QString>

class QApplication;

namespace core {
class Db;
struct AppConfig;
}

bool runDbSmokeTestNewSchema(core::Db &db, QString *err);
int runDevAbWindow(QApplication &app, const core::AppConfig &cfg);

// dev_tools.hpp 定义了一些开发工具的函数，包括运行数据库烟雾测试、打开数据库调试窗口等。
