#include "core/db.hpp"

#include <QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>
#include "core/mes_payload.hpp"

namespace core {

bool Db::createMesReport(quint64 measurement_id, const QVariant &task_id,
                         const QVariant &task_item_id,
                         const QString &report_uuid, const QString &report_type,
                         const QString &interface_code,
                         const QString &business_key, bool need_upload,
                         const QString &report_status,
                         const QString &payload_json, quint64 *new_id,
                         QString *err) {
  QSqlQuery q(db_);
  q.prepare("INSERT INTO mes_report ("
            " measurement_id, task_id, task_item_id, "
            " report_uuid, report_type, interface_code, business_key, "
            " need_upload, report_status, payload_json, "
            " created_at_utc, updated_at_utc"
            ") VALUES ("
            " :measurement_id, :task_id, :task_item_id, "
            " :report_uuid, :report_type, :interface_code, :business_key, "
            " :need_upload, :report_status, :payload_json, "
            " NOW(3), NOW(3)"
            ");");

  q.bindValue(":measurement_id",
              QVariant::fromValue<qulonglong>(measurement_id));
  q.bindValue(":task_id", task_id);
  q.bindValue(":task_item_id", task_item_id);

  q.bindValue(":report_uuid", report_uuid);
  q.bindValue(":report_type",
              report_type); // FIRST_MEASURE / RETEST_2_PASS / ...
  q.bindValue(":interface_code", interface_code); // 先可空
  q.bindValue(":business_key", business_key);
  q.bindValue(":need_upload", need_upload ? 1 : 0);
  q.bindValue(":report_status", report_status); // PENDING / APPROVED / ...
  q.bindValue(":payload_json", payload_json);

  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }

  if (new_id)
    *new_id = q.lastInsertId().toULongLong();
  return true;
}

/*
这段代码是 Db::insertResult 方法的完整实现，用于将测量结果数据插入到数据库中。
这个方法的主要作用是将一个测量结果记录安全地插入到数据库中，使用了事务机制确保数据一致性。
*/

QVector<core::MesUploadRow>
core::Db::queryMesUploadRows(const MesUploadFilter &f, int limit,
                             QString *err) {
  QVector<MesUploadRow> out;
  QSqlQuery q(db_);

  /*
  表连接：
      主表：measure_result（别名 r）- 存储测量结果
      从表：mes_outbox（别名 o）- 存储MES上传队列
      使用 LEFT JOIN 连接，确保即使没有对应的MES上传记录也能返回测量结果
  连接条件：
      通过 measurement_uuid 字段关联两表
      只关联 event_type='MEASURE_RESULT_READY' 的MES记录
  筛选条件：
      r.status='READY' - 只查询状态为"READY"的测量结果
      时间范围过滤：measured_at_utc 在指定时间区间内
  查询字段：
      测量结果信息：UUID、工件ID、类型、合格状态、测量时间、长度数据
      MES上传信息：状态、尝试次数、错误信息、更新时间
  */
  // SQL
  // 查询语句用于从两个数据表中联合查询测量结果及其对应的消息状态信息。使用一定的筛选条件（就是上位机界面中数据管理UI中的筛选需要显示的数据），结合两个表，
  // 筛选出需要的字段数据，然后再动态动态构建SQL查询，根据用户提供的过滤条件（`MesUploadFilter`
  // 结构体）向基础SQL语句添加额外的筛选条件。
  QString sql =
      "SELECT "
      " r.measurement_uuid, r.part_id, r.part_type, r.ok, r.measured_at_utc, "
      "r.total_len_mm, r.bc_len_mm, "
      " o.status AS mes_status, o.attempt_count, o.last_error, "
      "o.updated_at_utc "
      "FROM measure_result r "
      "LEFT JOIN mes_outbox o "
      "  ON o.measurement_uuid = r.measurement_uuid AND "
      "o.event_type='MEASURE_RESULT_READY' "
      "WHERE r.status='READY' "
      "  AND r.measured_at_utc >= :from_utc AND r.measured_at_utc <= :to_utc ";

  /*
  动态构建SQL查询语句的部分，根据用户提供的过滤条件（MesUploadFilter
  结构体）向基础SQL语句添加额外的筛选条件
  支持多条件组合过滤，用户可以根据需要指定任意组合的过滤条件
  */
  if (!f.part_id_like.trimmed()
           .isEmpty()) // 工件ID模糊匹配（LIKE），如果非空，添加模糊匹配条件，允许用户通过部分工件ID进行搜索，例如：用户输入"ABC"可以匹配"ABC123"、"XYZABC"等
    sql += " AND r.part_id LIKE :part_id_like ";
  if (!f.part_type.trimmed().isEmpty()) // 工件类型精确匹配
    sql += " AND r.part_type = :part_type ";
  if (f.ok_filter == 0 || f.ok_filter == 1) // 合格性过滤
    sql += " AND r.ok = :ok ";
  if (!f.mes_status.trimmed().isEmpty()) // MES状态过滤
  {
    if (f.mes_status == "NOT_QUEUED")
      sql += " AND o.id IS NULL ";
    else
      sql += " AND o.status = :mes_status ";
  }

  sql += " ORDER BY r.measured_at_utc DESC LIMIT :limit;";

  q.prepare(sql);                       // sql准备
  q.bindValue(":from_utc", f.from_utc); // 参数绑定
  q.bindValue(":to_utc", f.to_utc);
  if (!f.part_id_like.trimmed().isEmpty())
    q.bindValue(":part_id_like", "%" + f.part_id_like + "%");
  if (!f.part_type.trimmed().isEmpty())
    q.bindValue(":part_type", f.part_type);
  if (f.ok_filter == 0 || f.ok_filter == 1)
    q.bindValue(":ok", f.ok_filter);
  if (!f.mes_status.trimmed().isEmpty() && f.mes_status != "NOT_QUEUED")
    q.bindValue(":mes_status", f.mes_status);
  q.bindValue(":limit", limit);

  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return out;
  }

  /*
  SQL查询结果的结构
      当执行SQL查询后，数据库返回的结果确实是一个表格形式的数据结构，包含：
          多行数据（每行代表一条记录）
          多列数据（每列代表一个字段）
  遍历查询结果的过程
      初始状态：执行查询后，结果指针位于第一行之前（没有指向任何行）
      使用q.next()遍历行：
          每次调用q.next()会将指针移动到下一行
          当成功移动到新行时返回true
          当到达结果集末尾时返回false，循环结束
      使用q.value(index)访问列：
          index是列的索引，从0开始
          索引顺序对应SQL查询中SELECT语句的字段顺序
  */
  // 经过sql查询之后，会得到一个查询结果，sql查询的结果是一个表格，其中每一行对应一个测量结果，每一列对应一个字段,这样就可以遍历查询结果。
  // 处理查询结果的部分，负责将数据库查询结果映射到 MesUploadRow
  // 结构体。遍历查询结果，将每行数据填充到MesUploadRow对象并添加到输出列表
  while (q.next()) {
    MesUploadRow r;
    r.measurement_uuid = q.value(0).toString();
    r.part_id = q.value(1).toString();
    r.part_type = q.value(2).toString();
    r.ok = q.value(3).toInt() != 0;
    r.measured_at_utc = q.value(4).toDateTime();
    r.total_len_mm = q.value(5).isNull() ? 0.0 : q.value(5).toDouble();
    r.bc_len_mm = q.value(6).isNull() ? 0.0 : q.value(6).toDouble();

    // outbox may be NULL
    if (q.value(7).isNull()) {
      r.mes_status = "NOT_QUEUED";
      r.attempt_count = 0;
    } else {
      r.mes_status = q.value(7).toString();
      r.attempt_count = q.value(8).toInt();
      r.last_error = q.value(9).toString();
      r.mes_updated_at_utc = q.value(10).toDateTime();
    }

    out.push_back(r);
  }

  return out;
}

// 入队：若mes_outbox中已存在：状态为SENT -> 则拒绝，避免重复上传；状态为其他 ->
// 则重置为 PENDING；不存在 -> 生成 payload + INSERT
bool core::Db::queueMesUploadByUuid(const QString &uuid, QString *err) {
  // 检查是否已经存在待上传记录， 即mes_outbox表 检查是否已经存在该 UUID
  // 的待上传记录
  QSqlQuery q(db_);
  q.prepare("SELECT id, status FROM mes_outbox WHERE measurement_uuid=:u AND "
            "event_type='MEASURE_RESULT_READY' LIMIT 1;");
  q.bindValue(":u", uuid);
  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }

  // 如果存在且状态为 "SENT"，则返回错误，避免重复上传
  if (q.next()) {
    const auto status = q.value(1).toString();
    if (status == "SENT") {
      if (err)
        *err = "Already SENT; skip.";
      return false;
    }
    // 如果存在且状态不是 "SENT"，则重置为 PENDING
    QSqlQuery u(db_);
    u.prepare(
        "UPDATE mes_outbox "
        "SET status='PENDING', next_retry_at_utc=UTC_TIMESTAMP(3), "
        "updated_at_utc=UTC_TIMESTAMP(3) "
        "WHERE measurement_uuid=:u AND event_type='MEASURE_RESULT_READY';");
    u.bindValue(":u", uuid);
    if (!u.exec()) {
      if (err)
        *err = u.lastError().text();
      return false;
    }
    return true;
  }

  // 如果不存在：从 DB 读 measure_result + raw_file_index，构建
  // payload_json，INSERT 到 mes_outbox表中
  core::MeasureResult mr;
  {
    QSqlQuery r(db_);
    r.prepare("SELECT measurement_uuid, part_id, part_type, ok, "
              "measured_at_utc, total_len_mm, bc_len_mm, status "
              "FROM measure_result WHERE measurement_uuid=:u LIMIT 1;");
    r.bindValue(":u", uuid);
    if (!r.exec()) {
      if (err)
        *err = r.lastError().text();
      return false;
    }
    if (!r.next()) {
      if (err)
        *err = "measure_result not found for uuid";
      return false;
    }

    mr.measurement_uuid = r.value(0).toString();
    mr.part_id = r.value(1).toString();
    mr.part_type = r.value(2).toString();
    mr.ok = r.value(3).toInt() != 0;
    mr.measured_at_utc = r.value(4).toDateTime();
    mr.total_len_mm = r.value(5).isNull() ? 0.0 : r.value(5).toDouble();
    mr.bc_len_mm = r.value(6).isNull() ? 0.0 : r.value(6).toDouble();
    mr.status = r.value(7).toString();
  }

  core::RawWriteInfoV2 raw;
  {
    QSqlQuery r(db_);
    r.prepare("SELECT file_path, file_size_bytes, format_version, file_crc32, "
              "chunk_mask, scan_kind, main_channels,"
              " rings, points_per_ring, angle_step_deg, meta_json "
              "FROM raw_file_index WHERE measurement_uuid=:u LIMIT 1;");
    r.bindValue(":u", uuid);
    if (!r.exec()) {
      if (err)
        *err = r.lastError().text();
      return false;
    }
    if (!r.next()) {
      if (err)
        *err = "raw_file_index not found for uuid";
      return false;
    }

    raw.final_path = r.value(0).toString();
    raw.file_size_bytes = r.value(1).toULongLong();
    raw.format_version = r.value(2).toInt();
    raw.file_crc32 = r.value(3).toULongLong();
    raw.chunk_mask = r.value(4).toULongLong();
    raw.scan_kind = r.value(5).toString();
    raw.main_channels = r.value(6).toInt();
    raw.rings = r.value(7).toInt();
    raw.points_per_ring = r.value(8).toInt();
    raw.angle_step_deg = r.value(9).toFloat();
    raw.meta_json = r.value(10).toString();
  }

  // 3）构建 payload_json 消息负载
  const QString payload = core::buildMesPayloadV1(mr, raw);

  QSqlQuery ins(db_);
  ins.prepare(
      "INSERT INTO mes_outbox("
      " measurement_uuid, event_type, payload_json,"
      " status, attempt_count, next_retry_at_utc,"
      " created_at_utc, updated_at_utc"
      ") VALUES ("
      " :u, 'MEASURE_RESULT_READY', :p,"
      " 'PENDING', 0, UTC_TIMESTAMP(3), UTC_TIMESTAMP(3), UTC_TIMESTAMP(3)"
      ");");
  ins.bindValue(":u", uuid);
  ins.bindValue(":p", payload);
  if (!ins.exec()) {
    if (err)
      *err = ins.lastError().text();
    return false;
  }

  return true;
}

// 将mes_outbox中状态为FAILED的记录重置为PENDING，这里重试次数没有增加1
int core::Db::retryFailed(const QVector<QString> &uuids, QString *err) {
  if (uuids.isEmpty())
    return 0;

  // 简单实现：逐条 update（MVP够用）
  int cnt = 0;
  for (const auto &u : uuids) {
    QSqlQuery q(db_);
    q.prepare(
        "UPDATE mes_outbox SET status='PENDING', "
        "next_retry_at_utc=UTC_TIMESTAMP(3), updated_at_utc=UTC_TIMESTAMP(3) "
        "WHERE measurement_uuid=:u AND event_type='MEASURE_RESULT_READY' AND "
        "status='FAILED';");
    q.bindValue(":u", u);
    if (!q.exec()) {
      if (err)
        *err = q.lastError().text();
      return cnt;
    }
    // 在数据库操作（如INSERT、UPDATE、DELETE）后，numRowsAffected用于表示操作影响的数据库表中的行数。
    cnt += q.numRowsAffected();
  }
  return cnt;
}

// 将mes_outbox中状态为SENDING且更新时间超过stale_seconds的记录重置为FAILED,重置过期发送中的记录为失败状态
bool core::Db::resetStaleSending(int stale_seconds, QString *err) {
  QSqlQuery q(db_);
  q.prepare("UPDATE mes_outbox SET status='FAILED', "
            "updated_at_utc=UTC_TIMESTAMP(3), last_error=IFNULL(last_error,'') "
            "WHERE status='SENDING' AND updated_at_utc < (UTC_TIMESTAMP(3) - "
            "INTERVAL :s SECOND);");
  q.bindValue(":s", stale_seconds);
  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }
  return true;
}

// 从mes_outbox中获取下一个待处理任务，这里只获取状态为PENDING或FAILED的记录，且next_retry_at_utc小于等于当前时间的记录，
// 这里按照id升序排序，确保先处理旧的记录
bool core::Db::fetchNextDueOutbox(MesOutboxTask *task, QString *err) {
  if (!task)
    return false;
  QSqlQuery q(db_);
  q.prepare("SELECT id, measurement_uuid, payload_json, attempt_count "
            "FROM mes_outbox "
            "WHERE event_type='MEASURE_RESULT_READY' "
            "  AND status IN ('PENDING','FAILED') "
            "  AND next_retry_at_utc <= UTC_TIMESTAMP(3) "
            "ORDER BY id ASC LIMIT 1;");
  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }
  if (!q.next())
    return false;

  task->id = q.value(0).toULongLong();
  task->measurement_uuid = q.value(1).toString();
  task->payload_json = q.value(2).toString();
  task->attempt_count = q.value(3).toInt();
  return true;
}

// 将mes_outbox中状态为PENDING或FAILED的记录更新为SENDING状态，这里重试次数增加1
bool core::Db::markOutboxSending(quint64 id, QString *err) {
  QSqlQuery q(db_);
  q.prepare("UPDATE mes_outbox SET status='SENDING', "
            "updated_at_utc=UTC_TIMESTAMP(3) WHERE id=:id;");
  q.bindValue(":id", QVariant::fromValue<qulonglong>(id));
  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }
  return true;
}

// 将mes_outbox中状态为SENDING的记录更新为SENT状态，这里记录http_code和响应体
bool core::Db::markOutboxSent(quint64 id, int http_code, const QString &resp,
                              QString *err) {
  QSqlQuery q(db_);
  q.prepare("UPDATE mes_outbox SET "
            " status='SENT', last_http_code=:c, last_response_body=:r, "
            "last_error=NULL,"
            " updated_at_utc=UTC_TIMESTAMP(3) "
            "WHERE id=:id;");
  q.bindValue(":c", http_code);
  q.bindValue(
      ":r",
      resp.left(
          4000)); // 防止过大, 截断响应体,
                  // 防止数据库字段长度限制。resp.left(4000)的意思是取响应体的前4000个字符
  q.bindValue(":id", QVariant::fromValue<qulonglong>(id));
  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }
  return true;
}

// 将mes_outbox中状态为SENDING的记录更新为FAILED状态，这里记录http_code、响应体和错误信息，
// 并设置下一次重试时间为当前时间加上next_retry_seconds秒
bool core::Db::markOutboxFailed(quint64 id, int http_code, const QString &resp,
                                const QString &error, int next_retry_seconds,
                                QString *err) {
  QSqlQuery q(db_);
  q.prepare("UPDATE mes_outbox SET "
            " status='FAILED', attempt_count=attempt_count+1, "
            " last_http_code=:c, last_response_body=:r, last_error=:e, "
            " next_retry_at_utc=(UTC_TIMESTAMP(3) + INTERVAL :s SECOND), "
            " updated_at_utc=UTC_TIMESTAMP(3) "
            "WHERE id=:id;");
  q.bindValue(":c", http_code);
  q.bindValue(":r", resp.left(4000));
  q.bindValue(":e", error.left(2000));
  q.bindValue(":s", next_retry_seconds);
  q.bindValue(":id", QVariant::fromValue<qulonglong>(id));
  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }
  return true;
}

} // namespace core

// db_mes.cpp 实现了数据库操作相关的功能，包括插入、查询、更新和删除mes_outbox表中的记录。这个文件只负责report、outbox、API log、task
