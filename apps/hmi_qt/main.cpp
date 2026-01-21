#include <QApplication>
#include <QPushButton>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QWidget>
#include <QDir>
#include <QUuid>
#include <QDateTime>

#include <limits>

#include "core/config.hpp"
#include "core/db.hpp"
#include "core/model.hpp"

#include "core/snapshot.hpp"
#include "core/raw_v2.hpp"

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

static core::MeasurementSnapshot makeSnapshotA(const QString &uuid, const QString &partId)
{
  core::MeasurementSnapshot s;
  s.measurement_uuid = uuid;
  s.part_type = 'A';
  s.measured_at_utc = QDateTime::currentDateTimeUtc();
  s.meta_json = makeMetaJson(partId, 'A');

  // A型：CONF 4*16*72（单位 μm，允许 NaN）
  s.confocal4.resize(4 * 16 * 72);
  for (int ch = 0; ch < 4; ++ch)
  {
    for (int ring = 0; ring < 16; ++ring)
    {
      for (int pt = 0; pt < 72; ++pt)
      {
        const int idx = ch * (16 * 72) + ring * 72 + pt;
        s.confocal4[idx] = float(ch * 1000 + ring * 10 + pt);
      }
    }
  }
  s.confocal4[0] = std::numeric_limits<float>::quiet_NaN();

  // PLC给长度结果：单位 mm
  // gt2r[0]=total_len_mm, gt2r[1]=bc_len_mm(A型NaN), gt2r[2]=reserved NaN
  s.gt2r_mm3 = {
      123.456f,
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::quiet_NaN()};

  return s;
}

static core::MeasurementSnapshot makeSnapshotB(const QString &uuid, const QString &partId)
{
  core::MeasurementSnapshot s;
  s.measurement_uuid = uuid;
  s.part_type = 'B';
  s.measured_at_utc = QDateTime::currentDateTimeUtc();
  s.meta_json = makeMetaJson(partId, 'B');

  // B型：RUNO 2*16*72（单位 μm）
  s.runout2.resize(2 * 16 * 72);
  for (int ch = 0; ch < 2; ++ch)
  {
    for (int ring = 0; ring < 16; ++ring)
    {
      for (int pt = 0; pt < 72; ++pt)
      {
        const int idx = ch * (16 * 72) + ring * 72 + pt;
        s.runout2[idx] = float(ch * 2000 + ring * 20 + pt);
      }
    }
  }
  s.runout2[10] = std::numeric_limits<float>::quiet_NaN();

  // PLC给长度结果：单位 mm
  // gt2r[0]=total_len_mm, gt2r[1]=bc_len_mm(B型有), gt2r[2]=reserved NaN
  s.gt2r_mm3 = {
      223.456f,
      45.678f,
      std::numeric_limits<float>::quiet_NaN()};

  return s;
}

static bool persistOnce(const core::AppConfig &cfg,
                        const core::MeasurementSnapshot &snap,
                        const QString &partId,
                        QString *errOut)
{
  QString err;

  // 1) 写 raw
  core::RawWriteInfoV2 rawInfo;
  if (!core::writeRawV2(cfg.paths.raw_dir, snap, &rawInfo, &err))
  {
    if (errOut)
      *errOut = err;
    return false;
  }

  // 2) 组装 DB 结果（这里只放长度占位，后面接真实算法）
  core::MeasureResult r;
  r.measurement_uuid = snap.measurement_uuid;
  r.part_id = partId;
  r.part_type = QString(snap.part_type);
  r.ok = true;
  r.measured_at_utc = snap.measured_at_utc;
  r.total_len_mm = snap.gt2r_mm3[0];
  r.bc_len_mm = snap.gt2r_mm3[1];
  r.status = "READY";

  // 3) 入库（事务：结果+raw索引）
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

  // 4) 读回自检（证明 raw 格式可回放）
  core::MeasurementSnapshot s2;
  if (!core::readRawV2(rawInfo.final_path, &s2, &err))
  {
    if (errOut)
      *errOut = "DB OK but Raw readback failed: " + err;
    return false;
  }

  // 简单核对：uuid/part_type/长度
  if (s2.measurement_uuid != snap.measurement_uuid || s2.part_type != snap.part_type)
  {
    if (errOut)
      *errOut = "Raw readback mismatch (uuid/part_type)";
    return false;
  }
  if (s2.gt2r_mm3.size() != 3)
  {
    if (errOut)
      *errOut = "Raw readback missing GT2R";
    return false;
  }

  return true;
}

int main(int argc, char *argv[])
{
  QApplication a(argc, argv);

  QWidget w;
  w.setWindowTitle("HMI Measure - Dev Test (A/B)");

  auto *layout = new QVBoxLayout(&w);

  auto *btnA = new QPushButton("Insert A (CONF 4x16x72 + GT2R mm + META JSON)");
  auto *btnB = new QPushButton("Insert B (RUNO 2x16x72 + GT2R mm + META JSON)");
  btnA->setMinimumHeight(48);
  btnB->setMinimumHeight(48);

  layout->addWidget(btnA);
  layout->addWidget(btnB);

  const auto cfg = core::loadConfigIni("configs/app.ini");

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

    QMessageBox::information(nullptr, "OK",
      "Inserted A record.\n"
      "Raw format: HMIRAW02\n"
      "MES view: mes_v_measure_result"); });

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

    QMessageBox::information(nullptr, "OK",
      "Inserted B record.\n"
      "Raw format: HMIRAW02\n"
      "MES view: mes_v_measure_result"); });

  w.resize(700, 160);
  w.show();
  return a.exec();
}
