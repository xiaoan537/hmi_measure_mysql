#include "core/db.hpp"

#include <QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>
#include "core/mes_payload.hpp"

namespace core {

bool Db::insertResult(const MeasureResult &r, QString *err) {
  /*
  启动数据库事务
  如果启动失败，记录错误并返回false
  事务确保操作的原子性：要么全部成功，要么全部失败
  */
  if (!db_.transaction()) {
    if (err)
      *err = db_.lastError().text();
    return false;
  }

  /*
  准备SQL插入语句
  创建与当前数据库连接关联的查询对象
  使用参数化查询（Prepared Statement）防止SQL注入
  冒号前缀的名称（如:measurement_uuid）是占位符，稍后会被实际值替换
  */
  QSqlQuery q(db_);
  q.prepare("INSERT INTO measure_result("
            " measurement_uuid, part_id, part_type, ok, measured_at_utc, "
            "total_len_mm, status"
            ") VALUES ("
            " :measurement_uuid, :part_id, :part_type, :ok, :measured_at_utc, "
            ":total_len_mm, :status"
            ");");

  /*
  1. **准备阶段**：`q.prepare(...)`发送带占位符的SQL模板给数据库驱动。
  2.
  **标签生成**：`QStringLiteral(":measurement_uuid")`在编译时高效生成占位符名称对应的
  `QString`。
  3. **数据打包**：`QVariant(r.measurement_uuid)`将变量
  `r.measurement_uuid`的值打包成通用容器。
  4. **执行绑定**：`bindValue`将第2步的“标签”和第3步的“数据包”关联起来。
  5. **最终执行**：调用
  `q.exec()`，数据库驱动将绑定的所有值填入SQL模板的对应位置，并执行完整的语句。
  */
  q.bindValue(QStringLiteral(":measurement_uuid"),
              QVariant(r.measurement_uuid));
  q.bindValue(QStringLiteral(":part_id"), QVariant(r.part_id));
  q.bindValue(QStringLiteral(":part_type"), QVariant(r.part_type));
  q.bindValue(QStringLiteral(":ok"), QVariant(r.ok ? 1 : 0));
  q.bindValue(QStringLiteral(":measured_at_utc"), QVariant(r.measured_at_utc));
  q.bindValue(QStringLiteral(":total_len_mm"), QVariant(r.total_len_mm));
  q.bindValue(QStringLiteral(":status"), QVariant(r.status));

  // 执行插入操作，将绑定的参数值插入到数据库
  if (!q.exec()) {
    db_.rollback(); // 如果执行失败，回滚事务以保持数据一致性
    if (err)
      *err = q.lastError().text();
    return false;
  }

  // 提交事务，使更改永久生效
  // 如果提交失败，记录错误并返回false
  if (!db_.commit()) {
    if (err)
      *err = db_.lastError().text();
    return false;
  }

  return true;
}

// 新版 raw index
// 插入接口（HMIRAW02），在main中调用时使用，上边的insertResult函数基本可以遗弃了。
bool Db::insertResultWithRawIndexV2(const MeasureResult &r,
                                    const RawWriteInfoV2 &raw, QString *err) {
  if (!db_.transaction()) {
    if (err)
      *err = db_.lastError().text();
    return false;
  }

  // 1) measure_result
  {
    QSqlQuery q(db_);
    q.prepare("INSERT INTO measure_result("
              " measurement_uuid, part_id, part_type, ok, measured_at_utc, "
              "total_len_mm, bc_len_mm, status"
              ") VALUES ("
              " :measurement_uuid, :part_id, :part_type, :ok, "
              ":measured_at_utc, :total_len_mm, :bc_len_mm, :status"
              ");");
    q.bindValue(":measurement_uuid", r.measurement_uuid);
    q.bindValue(":part_id", r.part_id);
    q.bindValue(":part_type", r.part_type);
    q.bindValue(":ok", r.ok ? 1 : 0);
    q.bindValue(":measured_at_utc", r.measured_at_utc);
    q.bindValue(":total_len_mm", r.total_len_mm);

    // A型 bc_len_mm 建议写NULL
    if (r.part_type == "A")
      q.bindValue(":bc_len_mm", QVariant());
    else
      q.bindValue(":bc_len_mm", r.bc_len_mm);

    q.bindValue(":status", r.status);

    if (!q.exec()) {
      db_.rollback();
      if (err)
        *err = q.lastError().text();
      return false;
    }
  }

  // 2) raw_file_index
  {
    QSqlQuery q(db_);
    q.prepare("INSERT INTO raw_file_index("
              " measurement_uuid, file_path, file_size_bytes, format_version, "
              "file_crc32, "
              " chunk_mask, scan_kind, main_channels, rings, points_per_ring, "
              "angle_step_deg, meta_json, created_at_utc"
              ") VALUES ("
              " :measurement_uuid, :file_path, :file_size_bytes, "
              ":format_version, :file_crc32, "
              " :chunk_mask, :scan_kind, :main_channels, :rings, "
              ":points_per_ring, :angle_step_deg, :meta_json, :created_at_utc"
              ");");

    q.bindValue(":measurement_uuid", r.measurement_uuid);
    q.bindValue(":file_path", raw.final_path);
    q.bindValue(":file_size_bytes",
                QVariant::fromValue<qulonglong>(raw.file_size_bytes));
    q.bindValue(":format_version", raw.format_version);
    q.bindValue(":file_crc32", raw.file_crc32);

    q.bindValue(":chunk_mask", QVariant::fromValue<qulonglong>(raw.chunk_mask));
    q.bindValue(":scan_kind", raw.scan_kind);
    q.bindValue(":main_channels", raw.main_channels);
    q.bindValue(":rings", raw.rings);
    q.bindValue(":points_per_ring", raw.points_per_ring);
    q.bindValue(":angle_step_deg", raw.angle_step_deg);
    q.bindValue(":meta_json", raw.meta_json);
    q.bindValue(":created_at_utc", r.measured_at_utc);

    if (!q.exec()) {
      db_.rollback();
      if (err)
        *err = q.lastError().text();
      return false;
    }
  }

  if (!db_.commit()) {
    if (err)
      *err = db_.lastError().text();
    return false;
  }
  return true;
}


} // namespace core

// db_legacy.cpp，把仍然服务旧链路、旧页面、旧结构的东西放进去，这个文件只负责，让你明白：这些是兼容层，不是未来主线。
