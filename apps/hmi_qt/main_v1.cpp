#include <QApplication> // 包含 QApplication 类，QT 应用程序的基础类
#include <QPushButton>  // 包含 QPushButton 类，QT 的按钮控件类
#include <QMessageBox>  // 包含 QMessageBox 类，QT 的消息对话框类
#include <QDir>         // 包含 QDir 类，QT 的目录操作类
#include <QUuid>        // 包含 QUuid 类，QT 的 UUID 类，UUID生成 ，用于生成唯一标识符
#include <limits>

#include "core/config.hpp" // 包含配置头文件，加载应用配置
#include "core/db.hpp"     // 包含数据库头文件，数据库操作类
#include "core/model.hpp"  // 包含数据模型头文件，测量结果数据结构
#include "core/raw_writer.hpp"

// 辅助函数：ensureDir()
/*
  确保指定路径存在，如果不存在则创建。
  若目录已存在，直接返回 true
  使用 mkpath() 递归创建父目录
  失败时将错误信息写入指针参数 err（如果非空）
*/
static bool ensureDir(const QString &path, QString *err)
{
  QDir d;
  if (d.exists(path)) // 目录存在，直接返回
    return true;
  if (!d.mkpath(path)) // 创建目录失败，写入错误信息
  {
    if (err)
      *err = QString("Failed to create dir: %1").arg(path);
    return false;
  }
  return true;
}

// main() 函数核心逻辑
int main(int argc, char *argv[])
{
  // 创建 Qt 应用和一个按钮控件，显示提示信息
  QApplication a(argc, argv);

  QPushButton btn("Insert one READY record into MySQL");
  btn.resize(420, 60);

  // 按钮点击事件（信号槽） ，使用 lambda 表达式处理按钮点击事件
  // 当点击按钮时，执行回调函数（lambda 表达式）
  QObject::connect(&btn, &QPushButton::clicked, [&]()
                   {
                     QString err;

                     // 1) 读配置
                     // 加载 INI 配置文件，校验数据库名是否为空
                     const auto cfg = core::loadConfigIni("configs/app.ini");
                     if (cfg.db.name.trimmed().isEmpty())
                     {
                       QMessageBox::critical(nullptr, "Config", "db.name is empty in configs/app.ini");
                       return;
                     }

                     // 2) 确保数据目录存在（可靠性地基之一）
                     /*
                     创建三个必要的数据目录：
                       data_root：数据根目录
                       raw_dir：原始数据目录
                       log_dir：日志目录
                     */
                     if (!ensureDir(cfg.paths.data_root, &err) ||
                         !ensureDir(cfg.paths.raw_dir, &err) ||
                         !ensureDir(cfg.paths.log_dir, &err))
                     {
                       QMessageBox::critical(nullptr, "Paths", err);
                       return;
                     }

                     // 3) 连库 + 建表
                     /*
                     创建数据库对象
                     连接 MySQL
                     自动创建表结构（若表不存在）
                     */
                     core::Db db;
                     if (!db.open(cfg.db, &err))
                     {
                       QMessageBox::critical(nullptr, "DB open failed", err);
                       return;
                     }
                     if (!db.ensureSchema(&err))
                     {
                       QMessageBox::critical(nullptr, "Schema failed", err);
                       return;
                     }

                     // 4) 插入一条记录（示例：READY）
                     core::MeasureResult r;
                     r.measurement_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces); // 36字符含'-'
                     r.part_id = "TEST-0001";
                     r.part_type = "A";
                     r.ok = true;
                     r.measured_at_utc = QDateTime::currentDateTimeUtc();
                     r.total_len_mm = 123.456;
                     r.status = "READY";

                     // if (!db.insertResult(r, &err)) {
                     //   QMessageBox::critical(nullptr, "Insert failed", err);
                     //   return;
                     // }

                     // // 提示用户数据已插入，MES 可从视图读取
                     // QMessageBox::information(nullptr, "OK", "Inserted READY record.\nMES can read from view: mes_v_measure_result");

                     // 4) 生成假 confocal（4*16*72）
                     QVector<float> confocal;
                     confocal.resize(4 * 16 * 72);
                     for (int ch = 0; ch < 4; ++ch)
                     {
                       for (int ring = 0; ring < 16; ++ring)
                       {
                         for (int pt = 0; pt < 72; ++pt)
                         {
                           const int idx = ch * (16 * 72) + ring * 72 + pt;
                           confocal[idx] = float(ch * 1000 + ring * 10 + pt); // 假数据
                         }
                       }
                     }
                     // 模拟几个无效点 NaN
                     confocal[0] = std::numeric_limits<float>::quiet_NaN();
                     confocal[123] = std::numeric_limits<float>::quiet_NaN();

                     // 5) 写原始文件（可靠落盘）
                     core::RawWriteInfo rawInfo;
                     if (!core::writeRawV1_ConfocalOnly(
                             cfg.paths.raw_dir,
                             r.measurement_uuid,
                             r.measured_at_utc,
                             'A',
                             confocal,
                             &rawInfo,
                             &err))
                     {
                       QMessageBox::critical(nullptr, "Raw write failed", err);
                       return;
                     }

                     // 6) DB 事务：结果 + raw 索引一起提交
                     if (!db.insertResultWithRawIndex(r, rawInfo, &err))
                     {
                       QMessageBox::critical(nullptr, "Insert failed", err);
                       return;
                     }

                     QMessageBox::information(nullptr, "OK",
                                              "Inserted READY record + raw file.\n"
                                              "MES view: mes_v_measure_result\n"
                                              "Raw file: " +
                                                  rawInfo.final_path);
                   });

  btn.show();
  return a.exec(); // 进入事件循环，等待用户交互
}
