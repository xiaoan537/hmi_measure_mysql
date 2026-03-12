#pragma once

#include <QString>

class QApplication;

namespace core {
class Db;
struct AppConfig;
}

bool runDbSmokeTestNewSchema(core::Db &db, QString *err);
int runDevAbWindow(QApplication &app, const core::AppConfig &cfg);
