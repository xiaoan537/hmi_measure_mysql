#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

#include "login_dialog.hpp"

#include "core/config.hpp"
#include "core/db.hpp"
#include "dev_tools.hpp"
#include "mainwindow.hpp"
#include "mes_worker.hpp"

static QString resolveConfigPath(const QStringList &args) {
  for (const QString &a : args) {
    if (a.startsWith("--config=")) {
      return a.mid(QString("--config=").size());
    }
  }

  const QString cwdCandidate = "configs/app.ini";
  if (QFileInfo::exists(cwdCandidate)) return cwdCandidate;

  const QString exeDir = QCoreApplication::applicationDirPath();
  const QString fromBuild = exeDir + "/../../../configs/app.ini";
  if (QFileInfo::exists(fromBuild)) return fromBuild;

  return cwdCandidate;
}

int main(int argc, char *argv[]) {
  QApplication a(argc, argv);

  const QString iniPath = resolveConfigPath(QCoreApplication::arguments());
  if (!QFileInfo::exists(iniPath)) {
    QMessageBox::critical(
        nullptr, QStringLiteral("配置"),
        QStringLiteral("未找到 app.ini。\n尝试路径：%1\n提示：运行时使用 "
                       "--config=/完整/路径/app.ini")
            .arg(iniPath));
    return 1;
  }

  auto cfg = core::loadConfigIni(iniPath);

  QString err;
  core::Db db;
  if (!db.open(cfg.db, &err)) {
    QMessageBox::critical(nullptr, QStringLiteral("数据库"), err);
    return 1;
  }
  if (!db.ensureSchema(&err)) {
    QMessageBox::critical(nullptr, QStringLiteral("数据库"), err);
    return 1;
  }

  if (QCoreApplication::arguments().contains("--dev-ab")) {
    return runDevAbWindow(a, cfg);
  }

  {
    QSettings s(iniPath, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("login"));
    const bool loginEnabled = (s.value(QStringLiteral("enabled"), 1).toInt() != 0);
    s.endGroup();
    if (loginEnabled) {
      LoginDialog dlg(cfg, iniPath);
      if (dlg.exec() != QDialog::Accepted) {
        return 0;
      }
      cfg.mes.sysid = dlg.sysid();
    }
  }

  MesWorker worker(cfg);
  if (!worker.start(&err)) {
    QMessageBox::critical(nullptr, QStringLiteral("MES工作线程"), err);
    return 1;
  }

  MainWindow w(cfg, iniPath, &worker);
  w.resize(1100, 650);
  w.show();

  return a.exec();
}

// main.cpp减负，让main.cpp只做这件事：QApplication初始化、解析--config、加载配置、准备目录、启动服务、启动MainWindow，不再继承开发测试实现
