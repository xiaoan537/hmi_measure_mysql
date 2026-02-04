#include <QApplication>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QPushButton>
#include <QDir>
#include <QUuid>
#include <QDateTime>
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

// 确保目录存在
static bool ensureDir(const QString &path, QString *err)
{
  QDir d;
  if (d.exists(path))
    return true;
  if (!d.mkpath(path))
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
static core::MeasurementSnapshot makeSnapshotA(const QString &uuid, const QString &partId)
{
  core::MeasurementSnapshot s;
  s.measurement_uuid = uuid;
  s.part_type = 'A';
  s.measured_at_utc = QDateTime::currentDateTimeUtc();
  s.meta_json = makeMetaJson(partId, 'A');

  s.confocal4.resize(4 * 16 * 72);
  for (int ch = 0; ch < 4; ++ch)
    for (int ring = 0; ring < 16; ++ring)
      for (int pt = 0; pt < 72; ++pt)
      {
        const int idx = ch * (16 * 72) + ring * 72 + pt;
        s.confocal4[idx] = float(ch * 1000 + ring * 10 + pt);
      } // 填充4个通道的16个环的72个点
  s.confocal4[0] = std::numeric_limits<float>::quiet_NaN(); // 将第一个元素设为NaN，用于表示缺失值

  s.gt2r_mm3 = {123.456f,
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::quiet_NaN()}; // 仅填充第一个长度值，其他设为NaN
  return s;
}

// 生成模拟的测量快照 B
static core::MeasurementSnapshot makeSnapshotB(const QString &uuid, const QString &partId)
{
  core::MeasurementSnapshot s;
  s.measurement_uuid = uuid;
  s.part_type = 'B';
  s.measured_at_utc = QDateTime::currentDateTimeUtc();
  s.meta_json = makeMetaJson(partId, 'B');

  s.runout2.resize(2 * 16 * 72);
  for (int ch = 0; ch < 2; ++ch)
    for (int ring = 0; ring < 16; ++ring)
      for (int pt = 0; pt < 72; ++pt)
      {
        const int idx = ch * (16 * 72) + ring * 72 + pt;
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

  auto *btnA = new QPushButton("Insert A (CONF 4x16x72 + GT2R mm + META JSON)");
  auto *btnB = new QPushButton("Insert B (RUNO 2x16x72 + GT2R mm + META JSON)");
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
    const auto snap = makeSnapshotA(uuid, partId);
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
    const auto snap = makeSnapshotB(uuid, partId);
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

  const auto cfg = core::loadConfigIni("configs/app.ini");

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
