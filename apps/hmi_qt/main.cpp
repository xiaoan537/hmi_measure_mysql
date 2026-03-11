#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QVariant>
#include <limits>

// 你已有
#include "core/config.hpp"
#include "core/db.hpp"
#include "core/model.hpp"
#include "core/raw_v2.hpp"
#include "core/snapshot.hpp"

// 你新增的（若文件名不同，把这里改成你的实际文件名）
#include "mainwindow.hpp"
#include "mes_worker.hpp"

// 解析配置路径：
// 1) 优先使用命令行：--config=/path/to/app.ini
// 2) 其次尝试：当前工作目录 ./configs/app.ini
// 3) 最后尝试：从可执行文件目录回溯到工程根目录（适配在 build/ 下直接运行）
static QString resolveConfigPath(const QStringList &args) {
  for (const QString &a : args) // 遍历命令行参数，寻找以 "--config="
                                // 开头的参数，如果找到则提取路径并返回
  {
    if (a.startsWith("--config=")) {
      return a.mid(QString("--config=").size());
    }
  }

  const QString cwdCandidate =
      "configs/app.ini"; // 定义当前工作目录下的配置文件路径，用于检查是否存在
  if (QFileInfo::exists(cwdCandidate))
    return cwdCandidate;

  const QString exeDir =
      QCoreApplication::applicationDirPath(); // 获取当前可执行文件所在目录
  const QString fromBuild =
      exeDir +
      "/../../../configs/app.ini"; // 从可执行文件目录回溯到工程根目录，拼接配置文件路径
  if (QFileInfo::exists(fromBuild))
    return fromBuild;

  // 最后兜底：仍返回默认相对路径，方便提示错误
  return cwdCandidate;
}

// 确保目录存在
static bool ensureDir(const QString &path, QString *err) {
  QDir d;
  if (d.exists(path)) // 目录存在，直接返回 true
    return true;
  if (!d.mkpath(path)) // 如果不存在，尝试创建目录
  {
    if (err)
      *err = QString("Failed to create dir: %1").arg(path);
    return false;
  }
  return true;
}

// 生成模拟的 META JSON 字符串
static QString makeMetaJson(const QString &partId, QChar partType) {
  // 先手写JSON字符串即可（后面我们可以换成 QJsonDocument 更严谨）
  // 注意：真实项目里要对字符串做转义，这里仅用于测试
  return QString("{"
                 "\"barcode\":\"%1\","
                 "\"part_type\":\"%2\","
                 "\"record_seq\":%3,"
                 "\"slot\":%4,"
                 "\"sw_version\":\"dev-0.1\""
                 "}")
      .arg(partId)
      .arg(partType)
      .arg(QDateTime::currentSecsSinceEpoch() % 100000) // 假seq
      .arg(1);                                          // 假槽位
}

// 生成模拟的测量快照 A
static core::MeasurementSnapshot makeSnapshotA(const core::AppConfig &cfg,
                                               const QString &uuid,
                                               const QString &partId) {
  core::MeasurementSnapshot s;
  s.measurement_uuid = uuid;
  s.part_type = 'A';
  s.measured_at_utc = QDateTime::currentDateTimeUtc();
  s.meta_json = makeMetaJson(partId, 'A');

  // 扫描参数由 app.ini 配置（与 PLC 约定一致）
  s.conf_spec.rings = cfg.scan_a.rings;
  s.conf_spec.points_per_ring = cfg.scan_a.points_per_ring;
  s.conf_spec.angle_step_deg = cfg.scan_a.angle_step_deg;
  s.conf_spec.order_code = cfg.scan_a.order_code;

  const int R = s.conf_spec.rings;
  const int P = s.conf_spec.points_per_ring;
  const int C = 4;
  s.confocal4.resize(C * R * P);
  // 当前采用 ring -> ch -> pt（与 PLC Arrays 写入一致）
  for (int ring = 0; ring < R; ++ring)
    for (int ch = 0; ch < C; ++ch)
      for (int pt = 0; pt < P; ++pt) {
        const int idx = ring * (C * P) + ch * P + pt;
        s.confocal4[idx] = float(ch * 1000 + ring * 10 + pt);
      }
  s.confocal4[0] = std::numeric_limits<
      float>::quiet_NaN(); // 将第一个元素设为NaN，用于表示缺失值

  s.gt2r_mm3 = {123.456f, std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<
                    float>::quiet_NaN()}; // 仅填充第一个长度值，其他设为NaN
  return s;
}

// 生成模拟的测量快照 B
static core::MeasurementSnapshot makeSnapshotB(const core::AppConfig &cfg,
                                               const QString &uuid,
                                               const QString &partId) {
  core::MeasurementSnapshot s;
  s.measurement_uuid = uuid;
  s.part_type = 'B';
  s.measured_at_utc = QDateTime::currentDateTimeUtc();
  s.meta_json = makeMetaJson(partId, 'B');

  s.run_spec.rings = cfg.scan_b.rings;
  s.run_spec.points_per_ring = cfg.scan_b.points_per_ring;
  s.run_spec.angle_step_deg = cfg.scan_b.angle_step_deg;
  s.run_spec.order_code = cfg.scan_b.order_code;

  const int R = s.run_spec.rings;
  const int P = s.run_spec.points_per_ring;
  const int C = 2;
  s.runout2.resize(C * R * P);
  for (int ring = 0; ring < R; ++ring)
    for (int ch = 0; ch < C; ++ch)
      for (int pt = 0; pt < P; ++pt) {
        const int idx = ring * (C * P) + ch * P + pt;
        s.runout2[idx] = float(ch * 2000 + ring * 20 + pt);
      }
  s.runout2[10] = std::numeric_limits<float>::quiet_NaN();

  s.gt2r_mm3 = {223.456f, 45.678f, std::numeric_limits<float>::quiet_NaN()};
  return s;
}

static bool runDbSmokeTestNewSchema(core::Db &db, QString *err) {
  qDebug() << "[SMOKE] entered";
  auto fail = [&](const QString &e) -> bool {
    if (err)
      *err = e;
    return false;
  };

  auto now = QDateTime::currentDateTimeUtc();

  // ---------- A型 ----------
  {
    quint64 cycleId = 0;
    quint64 itemId = 0;
    quint64 measurementId = 0;
    quint64 resultId = 0;
    quint64 reportId = 0;
    QString e;

    if (!db.beginTx(&e))
      return fail(e);

    if (!db.insertPlcCycle(
            QUuid::createUuid().toString(QUuid::WithoutBraces), 100001, "A", 1,
            "AUTO", R"({"meas_seq":100001,"part_type":"A","item_count":1})",
            R"({"source":"smoke_test"})", now, &cycleId, &e)) {
      db.rollbackTx(nullptr);
      return fail("insertPlcCycle(A) failed: " + e);
    }

    if (!db.insertPlcCycleItem(cycleId, 0,
                               QVariant(3),   // slot_index
                               "A-PART-0001", // part_id
                               QVariant(1),   // result_ok
                               "", "", true, &itemId, &e)) {
      db.rollbackTx(nullptr);
      return fail("insertPlcCycleItem(A) failed: " + e);
    }

    if (!db.insertMeasurementEx(
            QUuid::createUuid().toString(QUuid::WithoutBraces),
            QVariant::fromValue<qulonglong>(cycleId),
            QVariant::fromValue<qulonglong>(itemId),
            QVariant(), // task_id 先空
            QVariant(), // task_item_id 先空
            "A-PART-0001", "A", "SLOT-03", QVariant(3), QVariant(0), "NORMAL",
            1, "OK", "FIRST_MEASURE", now, "tester", "PENDING", "", "", "READY",
            &measurementId, &e)) {
      db.rollbackTx(nullptr);
      return fail("insertMeasurementEx(A) failed: " + e);
    }

    if (!db.insertMeasurementResultEx(measurementId,
                                      QVariant(123.456), // total_len_mm
                                      QVariant(),        // ad_len_mm
                                      QVariant(),        // bc_len_mm
                                      QVariant(16.111),  // id_left_mm
                                      QVariant(15.999),  // id_right_mm
                                      QVariant(20.333),  // od_left_mm
                                      QVariant(19.888),  // od_right_mm
                                      QVariant(),        // runout_left_mm
                                      QVariant(),        // runout_right_mm
                                      "{}", R"({"note":"A型冒烟测试"})",
                                      &resultId, &e)) {
      db.rollbackTx(nullptr);
      return fail("insertMeasurementResultEx(A) failed: " + e);
    }

    if (!db.bindCycleItemMeasurement(itemId, measurementId, &e)) {
      db.rollbackTx(nullptr);
      return fail("bindCycleItemMeasurement(A) failed: " + e);
    }

    if (!db.createMesReport(
            measurementId, QVariant(), QVariant(),
            QUuid::createUuid().toString(QUuid::WithoutBraces), "FIRST_MEASURE",
            "", "SMOKE-A-0001", true, "PENDING",
            R"({"part_id":"A-PART-0001","report_type":"FIRST_MEASURE"})",
            &reportId, &e)) {
      db.rollbackTx(nullptr);
      return fail("createMesReport(A) failed: " + e);
    }

    if (!db.commitTx(&e)) {
      db.rollbackTx(nullptr);
      return fail("commit(A) failed: " + e);
    }
  }

  // ---------- B型 ----------
  {
    quint64 cycleId = 0;
    quint64 itemId = 0;
    quint64 measurementId = 0;
    quint64 resultId = 0;
    quint64 reportId = 0;
    QString e;

    if (!db.beginTx(&e))
      return fail(e);

    if (!db.insertPlcCycle(
            QUuid::createUuid().toString(QUuid::WithoutBraces), 100002, "B", 1,
            "AUTO", R"({"meas_seq":100002,"part_type":"B","item_count":1})",
            R"({"source":"smoke_test"})", now.addSecs(1), &cycleId, &e)) {
      db.rollbackTx(nullptr);
      return fail("insertPlcCycle(B) failed: " + e);
    }

    if (!db.insertPlcCycleItem(cycleId, 0, QVariant(7), "B-PART-0001",
                               QVariant(1), "", "", true, &itemId, &e)) {
      db.rollbackTx(nullptr);
      return fail("insertPlcCycleItem(B) failed: " + e);
    }

    if (!db.insertMeasurementEx(
            QUuid::createUuid().toString(QUuid::WithoutBraces),
            QVariant::fromValue<qulonglong>(cycleId),
            QVariant::fromValue<qulonglong>(itemId), QVariant(), QVariant(),
            "B-PART-0001", "B", "SLOT-07", QVariant(7), QVariant(0), "NORMAL",
            1, "OK", "FIRST_MEASURE", now.addSecs(1), "tester", "PENDING", "",
            "", "READY", &measurementId, &e)) {
      db.rollbackTx(nullptr);
      return fail("insertMeasurementEx(B) failed: " + e);
    }

    if (!db.insertMeasurementResultEx(measurementId,
                                      QVariant(),       // total_len_mm
                                      QVariant(88.123), // ad_len_mm
                                      QVariant(33.456), // bc_len_mm
                                      QVariant(),       // id_left_mm
                                      QVariant(),       // id_right_mm
                                      QVariant(),       // od_left_mm
                                      QVariant(),       // od_right_mm
                                      QVariant(0.012),  // runout_left_mm
                                      QVariant(0.018),  // runout_right_mm
                                      "{}", R"({"note":"B型冒烟测试"})",
                                      &resultId, &e)) {
      db.rollbackTx(nullptr);
      return fail("insertMeasurementResultEx(B) failed: " + e);
    }

    if (!db.bindCycleItemMeasurement(itemId, measurementId, &e)) {
      db.rollbackTx(nullptr);
      return fail("bindCycleItemMeasurement(B) failed: " + e);
    }

    if (!db.createMesReport(
            measurementId, QVariant(), QVariant(),
            QUuid::createUuid().toString(QUuid::WithoutBraces), "FIRST_MEASURE",
            "", "SMOKE-B-0001", true, "PENDING",
            R"({"part_id":"B-PART-0001","report_type":"FIRST_MEASURE"})",
            &reportId, &e)) {
      db.rollbackTx(nullptr);
      return fail("createMesReport(B) failed: " + e);
    }

    if (!db.commitTx(&e)) {
      db.rollbackTx(nullptr);
      return fail("commit(B) failed: " + e);
    }
  }

  return true;
}

// 持久化一次测量快照（包含原始数据和测量结果）
static bool persistOnce(const core::AppConfig &cfg,
                        const core::MeasurementSnapshot &snap,
                        const QString &partId, QString *errOut) {
  QString err;

  core::RawWriteInfoV2 rawInfo;
  if (!core::writeRawV2(cfg.paths.raw_dir, snap, &rawInfo, &err)) {
    if (errOut)
      *errOut = err;
    return false;
  }

  core::MeasureResult r;
  r.measurement_uuid = snap.measurement_uuid;
  r.part_id = partId;
  r.part_type = QString(snap.part_type);
  r.ok = true;
  r.measured_at_utc = snap.measured_at_utc;
  r.total_len_mm = snap.gt2r_mm3[0];
  r.bc_len_mm = snap.gt2r_mm3[1];
  r.status = "READY";

  core::Db db;
  if (!db.open(cfg.db, &err)) {
    if (errOut)
      *errOut = err;
    return false;
  }
  if (!db.ensureSchema(&err)) {
    if (errOut)
      *errOut = err;
    return false;
  }
  if (!db.insertResultWithRawIndexV2(r, rawInfo, &err)) {
    if (errOut)
      *errOut = err;
    return false;
  }

  core::MeasurementSnapshot s2;
  if (!core::readRawV2(rawInfo.final_path, &s2, &err)) {
    if (errOut)
      *errOut = "DB OK but Raw readback failed: " + err;
    return false;
  }
  return true;
}

// 保留你原来的 A/B 插入测试：./hmi_qt --dev-ab
static int runDevAbWindow(QApplication &a, const core::AppConfig &cfg) {
  QWidget w;
  w.setWindowTitle("工件自动测量上位机 - Dev测试(A/B)");
  auto *layout = new QVBoxLayout(&w);

  auto *btnA = new QPushButton("Insert A (CONF 4xR xP + GT2R mm + META JSON)");
  auto *btnB = new QPushButton("Insert B (RUNO 2xR xP + GT2R mm + META JSON)");
  btnA->setMinimumHeight(48);
  btnB->setMinimumHeight(48);
  layout->addWidget(btnA);
  layout->addWidget(btnB);

  QObject::connect(btnA, &QPushButton::clicked, [&]() {
    QString err;
    if (!ensureDir(cfg.paths.data_root, &err) ||
        !ensureDir(cfg.paths.raw_dir, &err) ||
        !ensureDir(cfg.paths.log_dir, &err)) {
      QMessageBox::critical(nullptr, "路径", err);
      return;
    }
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString partId = "A-TEST-0001";
    const auto snap = makeSnapshotA(cfg, uuid, partId);
    if (!persistOnce(cfg, snap, partId, &err)) {
      QMessageBox::critical(nullptr, "失败", err);
      return;
    }
    QMessageBox::information(nullptr, "成功", "已插入A型记录。");
  });

  QObject::connect(btnB, &QPushButton::clicked, [&]() {
    QString err;
    if (!ensureDir(cfg.paths.data_root, &err) ||
        !ensureDir(cfg.paths.raw_dir, &err) ||
        !ensureDir(cfg.paths.log_dir, &err)) {
      QMessageBox::critical(nullptr, "路径", err);
      return;
    }
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString partId = "B-TEST-0001";
    const auto snap = makeSnapshotB(cfg, uuid, partId);
    if (!persistOnce(cfg, snap, partId, &err)) {
      QMessageBox::critical(nullptr, "失败", err);
      return;
    }
    QMessageBox::information(nullptr, "成功", "已插入B型记录。");
  });

  w.resize(700, 160);
  w.show();
  return a.exec();
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
  const auto cfg = core::loadConfigIni(iniPath);

  QString err;
  core::Db db;
  if (!db.open(cfg.db, &err)) {
    QMessageBox::critical(nullptr, "数据库", err);
    return 1;
  }
  if (!db.ensureSchema(&err)) {
    QMessageBox::critical(nullptr, "数据库", err);
    return 1;
  }

  // 调用runDbSmokeTestNewSchema函数（函数体在上边），往数据库表中写入假数据，测试数据库是否正常，目前先注释掉，以防每次运行代码都会自动写入假数据；后边可以根据需要打开，或者将下边的调用代码写到其他位置，使用按钮触发。
  // QString smokeErr;
  // const bool smokeOk = runDbSmokeTestNewSchema(db, &smokeErr);
  // qDebug() << "[SMOKE] result =" << smokeOk << ", err =" << smokeErr;

  // if (!smokeOk) {
  //   QMessageBox::warning(nullptr, "数据库测试",
  //                        "DbSmokeTest 失败：\n" + smokeErr);
  // } else {
  //   QMessageBox::information(nullptr, "数据库测试",
  //                            "DbSmokeTest 成功，新表写入链路正常。");
  // }

  // 保留你原来的 A/B 插入测试：./hmi_qt --dev-ab
  if (QCoreApplication::arguments().contains("--dev-ab")) {
    return runDevAbWindow(a, cfg);
  }

  // 默认启动：MainWindow + MesWorker
  // 下面两行根据你项目里的构造函数签名微调即可
  MesWorker worker(cfg);
  // QString err;
  if (!worker.start(&err)) {
    QMessageBox::critical(nullptr, "MES工作线程", err);
    return 1;
  }

  MainWindow w(cfg, iniPath, &worker);
  w.resize(1100, 650);
  w.show();

  return a.exec();
}
