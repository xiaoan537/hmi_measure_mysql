#include <QApplication>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QPushButton>
#include <QDir>
#include <QUuid>
#include <QDateTime>
#include <QFileInfo>
#include <QCoreApplication>
#include <limits>

// 你已有
#include "core/config.hpp"
#include "core/db.hpp"
#include "core/model.hpp"
#include "core/snapshot.hpp"
#include "core/raw_v2.hpp"

// 你新增的（若文件名不同，把这里改成你的实际文件名）
#include "mainwindow.hpp"
#include "mes_worker.hpp"

// 解析配置路径：
// 1) 优先使用命令行：--config=/path/to/app.ini
// 2) 其次尝试：当前工作目录 ./configs/app.ini
// 3) 最后尝试：从可执行文件目录回溯到工程根目录（适配在 build/ 下直接运行）
static QString resolveConfigPath(const QStringList &args)
{
  for (const QString &a : args) // 遍历命令行参数，寻找以 "--config=" 开头的参数，如果找到则提取路径并返回
  {
    if (a.startsWith("--config="))
    {
      return a.mid(QString("--config=").size());
    }
  }

  const QString cwdCandidate = "configs/app.ini"; // 定义当前工作目录下的配置文件路径，用于检查是否存在
  if (QFileInfo::exists(cwdCandidate))
    return cwdCandidate;

  const QString exeDir = QCoreApplication::applicationDirPath();  // 获取当前可执行文件所在目录
  const QString fromBuild = exeDir + "/../../../configs/app.ini"; // 从可执行文件目录回溯到工程根目录，拼接配置文件路径
  if (QFileInfo::exists(fromBuild))
    return fromBuild;

  // 最后兜底：仍返回默认相对路径，方便提示错误
  return cwdCandidate;
}

// 确保目录存在
static bool ensureDir(const QString &path, QString *err)
{
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
static QString makeMetaJson(const QString &partId, QChar partType)
{
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
static core::MeasurementSnapshot makeSnapshotA(const core::AppConfig &cfg, const QString &uuid, const QString &partId)
{
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
      for (int pt = 0; pt < P; ++pt)
      {
        const int idx = ring * (C * P) + ch * P + pt;
        s.confocal4[idx] = float(ch * 1000 + ring * 10 + pt);
      }
  s.confocal4[0] = std::numeric_limits<float>::quiet_NaN(); // 将第一个元素设为NaN，用于表示缺失值

  s.gt2r_mm3 = {123.456f,
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::quiet_NaN()}; // 仅填充第一个长度值，其他设为NaN
  return s;
}

// 生成模拟的测量快照 B
static core::MeasurementSnapshot makeSnapshotB(const core::AppConfig &cfg, const QString &uuid, const QString &partId)
{
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
      for (int pt = 0; pt < P; ++pt)
      {
        const int idx = ring * (C * P) + ch * P + pt;
        s.runout2[idx] = float(ch * 2000 + ring * 20 + pt);
      }
  s.runout2[10] = std::numeric_limits<float>::quiet_NaN();

  s.gt2r_mm3 = {223.456f, 45.678f, std::numeric_limits<float>::quiet_NaN()};
  return s;
}

// 持久化一次测量快照（包含原始数据和测量结果）
static bool persistOnce(const core::AppConfig &cfg,
                        const core::MeasurementSnapshot &snap,
                        const QString &partId,
                        QString *errOut)
{
  QString err;

  core::RawWriteInfoV2 rawInfo;
  if (!core::writeRawV2(cfg.paths.raw_dir, snap, &rawInfo, &err))
  {
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
  if (!db.open(cfg.db, &err))
  {
    if (errOut)
      *errOut = err;
    return false;
  }
  if (!db.ensureSchema(&err))
  {
    if (errOut)
      *errOut = err;
    return false;
  }
  if (!db.insertResultWithRawIndexV2(r, rawInfo, &err))
  {
    if (errOut)
      *errOut = err;
    return false;
  }

  core::MeasurementSnapshot s2;
  if (!core::readRawV2(rawInfo.final_path, &s2, &err))
  {
    if (errOut)
      *errOut = "DB OK but Raw readback failed: " + err;
    return false;
  }
  return true;
}

// 保留你原来的 A/B 插入测试：./hmi_qt --dev-ab
static int runDevAbWindow(QApplication &a, const core::AppConfig &cfg)
{
  QWidget w;
  w.setWindowTitle("HMI Measure - Dev Test (A/B)");
  auto *layout = new QVBoxLayout(&w);

  auto *btnA = new QPushButton("Insert A (CONF 4xR xP + GT2R mm + META JSON)");
  auto *btnB = new QPushButton("Insert B (RUNO 2xR xP + GT2R mm + META JSON)");
  btnA->setMinimumHeight(48);
  btnB->setMinimumHeight(48);
  layout->addWidget(btnA);
  layout->addWidget(btnB);

  QObject::connect(btnA, &QPushButton::clicked, [&]()
                   {
    QString err;
    if (!ensureDir(cfg.paths.data_root, &err) ||
        !ensureDir(cfg.paths.raw_dir, &err) ||
        !ensureDir(cfg.paths.log_dir, &err)) {
      QMessageBox::critical(nullptr, "Paths", err);
      return;
    }
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString partId = "A-TEST-0001";
    const auto snap = makeSnapshotA(cfg, uuid, partId);
    if (!persistOnce(cfg, snap, partId, &err)) {
      QMessageBox::critical(nullptr, "Failed", err);
      return;
    }
    QMessageBox::information(nullptr, "OK", "Inserted A record."); });

  QObject::connect(btnB, &QPushButton::clicked, [&]()
                   {
    QString err;
    if (!ensureDir(cfg.paths.data_root, &err) ||
        !ensureDir(cfg.paths.raw_dir, &err) ||
        !ensureDir(cfg.paths.log_dir, &err)) {
      QMessageBox::critical(nullptr, "Paths", err);
      return;
    }
    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString partId = "B-TEST-0001";
    const auto snap = makeSnapshotB(cfg, uuid, partId);
    if (!persistOnce(cfg, snap, partId, &err)) {
      QMessageBox::critical(nullptr, "Failed", err);
      return;
    }
    QMessageBox::information(nullptr, "OK", "Inserted B record."); });

  w.resize(700, 160);
  w.show();
  return a.exec();
}

int main(int argc, char *argv[])
{
  QApplication a(argc, argv);

  const QString iniPath = resolveConfigPath(QCoreApplication::arguments());
  if (!QFileInfo::exists(iniPath))
  {
    QMessageBox::critical(nullptr, "Config", QString("app.ini not found.\nTried: %1\nHint: run with --config=/full/path/to/app.ini").arg(iniPath));
    return 1;
  }
  const auto cfg = core::loadConfigIni(iniPath);

  // 保留你原来的 A/B 插入测试：./hmi_qt --dev-ab
  if (QCoreApplication::arguments().contains("--dev-ab"))
  {
    return runDevAbWindow(a, cfg);
  }

  // 默认启动：MainWindow + MesWorker
  // 下面两行根据你项目里的构造函数签名微调即可
  MesWorker worker(cfg);
  QString err;
  if (!worker.start(&err))
  {
    QMessageBox::critical(nullptr, "MES Worker", err);
    return 1;
  }

  MainWindow w(cfg, &worker);
  w.resize(1100, 650);
  w.show();

  return a.exec();
}
