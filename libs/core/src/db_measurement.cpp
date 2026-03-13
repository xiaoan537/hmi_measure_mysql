#include "core/db.hpp"

#include <QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>

namespace core {

bool Db::insertPlcCycle(const QString &cycle_uuid, quint64 meas_seq,
                        const QString &part_type, int item_count,
                        const QString &source_mode,
                        const QString &mailbox_header_json,
                        const QString &mailbox_meta_json,
                        const QDateTime &measured_at_utc, quint64 *new_id,
                        QString *err) {
  QSqlQuery q(db_);
  q.prepare(
      "INSERT INTO plc_cycle("
      " cycle_uuid, meas_seq, part_type, item_count, source_mode, "
      " mailbox_header_json, mailbox_meta_json, measured_at_utc, created_at_utc"
      ") VALUES ("
      " :cycle_uuid, :meas_seq, :part_type, :item_count, :source_mode, "
      " :mailbox_header_json, :mailbox_meta_json, :measured_at_utc, NOW(3)"
      ");");

  q.bindValue(":cycle_uuid", cycle_uuid);
  q.bindValue(":meas_seq", QVariant::fromValue<qulonglong>(meas_seq));
  q.bindValue(":part_type", part_type);
  q.bindValue(":item_count", item_count);
  q.bindValue(":source_mode", source_mode);
  q.bindValue(":mailbox_header_json", mailbox_header_json);
  q.bindValue(":mailbox_meta_json", mailbox_meta_json);
  q.bindValue(":measured_at_utc", measured_at_utc);

  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }

  if (new_id)
    *new_id = q.lastInsertId().toULongLong();
  return true;
}

bool Db::insertPlcCycleItem(quint64 plc_cycle_id, int item_index,
                            const QVariant &slot_index, const QString &part_id,
                            const QVariant &result_ok,
                            const QString &fail_reason_code,
                            const QString &fail_reason_text, bool is_valid,
                            quint64 *new_id, QString *err) {
  QSqlQuery q(db_);
  q.prepare("INSERT INTO plc_cycle_item("
            " plc_cycle_id, item_index, slot_index, part_id, result_ok, "
            " fail_reason_code, fail_reason_text, is_valid, created_at_utc"
            ") VALUES ("
            " :plc_cycle_id, :item_index, :slot_index, :part_id, :result_ok, "
            " :fail_reason_code, :fail_reason_text, :is_valid, NOW(3)"
            ");");

  q.bindValue(":plc_cycle_id", QVariant::fromValue<qulonglong>(plc_cycle_id));
  q.bindValue(":item_index", item_index);
  q.bindValue(":slot_index", slot_index);
  q.bindValue(":part_id", part_id);
  q.bindValue(":result_ok", result_ok);
  q.bindValue(":fail_reason_code", fail_reason_code);
  q.bindValue(":fail_reason_text", fail_reason_text);
  q.bindValue(":is_valid", is_valid ? 1 : 0);

  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }

  if (new_id)
    *new_id = q.lastInsertId().toULongLong();
  return true;
}

bool Db::insertMeasurementEx(
    const QString &measurement_uuid, const QVariant &plc_cycle_id,
    const QVariant &plc_cycle_item_id, const QVariant &task_id,
    const QVariant &task_item_id, const QString &part_id,
    const QString &part_type, const QString &slot_id,
    const QVariant &slot_index, const QVariant &item_index,
    const QString &measure_mode, int measure_round,
    const QString &result_judgement, const QString &upload_kind,
    const QDateTime &measured_at_utc, const QString &operator_id,
    const QString &review_status, const QString &fail_reason_code,
    const QString &fail_reason_text, const QString &status, quint64 *new_id,
    QString *err) {
  QSqlQuery q(db_);
  q.prepare("INSERT INTO measurement ("
            " measurement_uuid, "
            " plc_cycle_id, plc_cycle_item_id, task_id, task_item_id, "
            " part_id, part_type, slot_id, slot_index, item_index, "
            " measure_mode, measure_round, result_judgement, upload_kind, "
            " measured_at_utc, operator_id, "
            " review_status, fail_reason_code, fail_reason_text, "
            " status, created_at_utc, updated_at_utc"
            ") VALUES ("
            " :measurement_uuid, "
            " :plc_cycle_id, :plc_cycle_item_id, :task_id, :task_item_id, "
            " :part_id, :part_type, :slot_id, :slot_index, :item_index, "
            " :measure_mode, :measure_round, :result_judgement, :upload_kind, "
            " :measured_at_utc, :operator_id, "
            " :review_status, :fail_reason_code, :fail_reason_text, "
            " :status, NOW(3), NOW(3)"
            ");");

  q.bindValue(":measurement_uuid", measurement_uuid);
  q.bindValue(":plc_cycle_id", plc_cycle_id);
  q.bindValue(":plc_cycle_item_id", plc_cycle_item_id);
  q.bindValue(":task_id", task_id);
  q.bindValue(":task_item_id", task_item_id);

  q.bindValue(":part_id", part_id);
  q.bindValue(":part_type", part_type);
  q.bindValue(":slot_id", slot_id);
  q.bindValue(":slot_index", slot_index);
  q.bindValue(":item_index", item_index);

  q.bindValue(":measure_mode",
              measure_mode); // NORMAL / RETEST / MANUAL / MIL_CHECK
  q.bindValue(":measure_round", measure_round); // 1 / 2 / 3 / 9
  q.bindValue(":result_judgement",
              result_judgement); // OK / NG / INVALID / ABORTED
  q.bindValue(":upload_kind", upload_kind);

  q.bindValue(":measured_at_utc", measured_at_utc);
  q.bindValue(":operator_id", operator_id);

  q.bindValue(":review_status", review_status); // PENDING / APPROVED / ...
  q.bindValue(":fail_reason_code", fail_reason_code);
  q.bindValue(":fail_reason_text", fail_reason_text);

  q.bindValue(":status", status); // NEW / READY / REPORTED / ARCHIVED

  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }

  if (new_id)
    *new_id = q.lastInsertId().toULongLong();
  return true;
}

bool Db::insertMeasurementResultEx(
    quint64 measurement_id, const QVariant &total_len_mm,
    const QVariant &ad_len_mm, const QVariant &bc_len_mm,
    const QVariant &id_left_mm, const QVariant &id_right_mm,
    const QVariant &od_left_mm, const QVariant &od_right_mm,
    const QVariant &runout_left_mm, const QVariant &runout_right_mm,
    const QString &tolerance_json, const QString &extra_json, quint64 *new_id,
    QString *err) {
  QSqlQuery q(db_);
  q.prepare("INSERT INTO measurement_result ("
            " measurement_id, "
            " total_len_mm, ad_len_mm, bc_len_mm, "
            " id_left_mm, id_right_mm, od_left_mm, od_right_mm, "
            " runout_left_mm, runout_right_mm, "
            " tolerance_json, extra_json, created_at_utc"
            ") VALUES ("
            " :measurement_id, "
            " :total_len_mm, :ad_len_mm, :bc_len_mm, "
            " :id_left_mm, :id_right_mm, :od_left_mm, :od_right_mm, "
            " :runout_left_mm, :runout_right_mm, "
            " :tolerance_json, :extra_json, NOW(3)"
            ");");

  q.bindValue(":measurement_id",
              QVariant::fromValue<qulonglong>(measurement_id));

  q.bindValue(":total_len_mm", total_len_mm); // A型
  q.bindValue(":ad_len_mm", ad_len_mm);       // B型
  q.bindValue(":bc_len_mm", bc_len_mm);       // B型

  q.bindValue(":id_left_mm", id_left_mm);   // A型
  q.bindValue(":id_right_mm", id_right_mm); // A型
  q.bindValue(":od_left_mm", od_left_mm);   // A型
  q.bindValue(":od_right_mm", od_right_mm); // A型

  q.bindValue(":runout_left_mm", runout_left_mm);   // B型
  q.bindValue(":runout_right_mm", runout_right_mm); // B型

  q.bindValue(":tolerance_json", tolerance_json);
  q.bindValue(":extra_json", extra_json);

  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }

  if (new_id)
    *new_id = q.lastInsertId().toULongLong();
  return true;
}


bool Db::bindCycleItemMeasurement(quint64 plc_cycle_item_id,
                                  quint64 measurement_id, QString *err) {
  QSqlQuery q(db_);
  q.prepare("UPDATE plc_cycle_item "
            "SET measurement_id = :measurement_id "
            "WHERE id = :id;");
  q.bindValue(":measurement_id",
              QVariant::fromValue<qulonglong>(measurement_id));
  q.bindValue(":id", QVariant::fromValue<qulonglong>(plc_cycle_item_id));

  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }
  return true;
}

// 这个接口先做“查最新 N 条”，后面 Data 页面很好接
QVector<MeasurementListRowEx> Db::queryLatestMeasurementsEx(int limit,
                                                            QString *err) {
  QVector<MeasurementListRowEx> out;
  if (limit <= 0)
    limit = 50;

  const QString sql = QString(
      "SELECT "
      "  m.id, m.measurement_uuid, "
      "  m.part_id, m.part_type, m.slot_id, IFNULL(m.slot_index, -1), "
      "  m.measure_mode, m.measure_round, m.result_judgement, m.review_status, "
      "  m.measured_at_utc, "
      "  r.total_len_mm, r.ad_len_mm, r.bc_len_mm, "
      "  r.id_left_mm, r.id_right_mm, r.od_left_mm, r.od_right_mm, "
      "  r.runout_left_mm, r.runout_right_mm "
      "FROM measurement m "
      "LEFT JOIN measurement_result r ON r.measurement_id = m.id "
      "ORDER BY m.id DESC "
      "LIMIT %1;").arg(limit);

  QSqlQuery q(db_);
  if (!q.exec(sql)) {
    if (err)
      *err = q.lastError().text();
    return out;
  }

  while (q.next()) {
    MeasurementListRowEx row;
    row.measurement_id = q.value(0).toULongLong();
    row.measurement_uuid = q.value(1).toString();

    row.part_id = q.value(2).toString();
    row.part_type = q.value(3).toString();
    row.slot_id = q.value(4).toString();
    row.slot_index = q.value(5).toInt();

    row.measure_mode = q.value(6).toString();
    row.measure_round = q.value(7).toInt();
    row.result_judgement = q.value(8).toString();
    row.review_status = q.value(9).toString();
    row.measured_at_utc = q.value(10).toDateTime();

    row.has_total_len = !q.value(11).isNull();
    row.has_ad_len = !q.value(12).isNull();
    row.has_bc_len = !q.value(13).isNull();
    row.has_id_left = !q.value(14).isNull();
    row.has_id_right = !q.value(15).isNull();
    row.has_od_left = !q.value(16).isNull();
    row.has_od_right = !q.value(17).isNull();
    row.has_runout_left = !q.value(18).isNull();
    row.has_runout_right = !q.value(19).isNull();

    if (row.has_total_len)
      row.total_len_mm = q.value(11).toDouble();
    if (row.has_ad_len)
      row.ad_len_mm = q.value(12).toDouble();
    if (row.has_bc_len)
      row.bc_len_mm = q.value(13).toDouble();
    if (row.has_id_left)
      row.id_left_mm = q.value(14).toDouble();
    if (row.has_id_right)
      row.id_right_mm = q.value(15).toDouble();
    if (row.has_od_left)
      row.od_left_mm = q.value(16).toDouble();
    if (row.has_od_right)
      row.od_right_mm = q.value(17).toDouble();
    if (row.has_runout_left)
      row.runout_left_mm = q.value(18).toDouble();
    if (row.has_runout_right)
      row.runout_right_mm = q.value(19).toDouble();

    out.push_back(row);
  }

  return out;
}

// 这个接口适合后面详情页、RAW Viewer、Data 页右侧详情面板使用
bool Db::getMeasurementDetailExById(quint64 measurement_id,
                                    MeasurementDetailEx *out, QString *err) {
  if (!out) {
    if (err)
      *err = "output pointer is null";
    return false;
  }

  QSqlQuery q(db_);
  q.prepare(
      "SELECT "
      "  m.id, m.measurement_uuid, "
      "  m.plc_cycle_id, m.plc_cycle_item_id, m.task_id, m.task_item_id, "
      "  m.part_id, m.part_type, m.slot_id, m.slot_index, m.item_index, "
      "  m.measure_mode, m.measure_round, m.result_judgement, m.upload_kind, "
      "  m.measured_at_utc, m.operator_id, "
      "  m.review_status, m.reviewer_id, m.reviewed_at_utc, m.review_note, "
      "  m.fail_reason_code, m.fail_reason_text, m.status, "
      "  r.total_len_mm, r.ad_len_mm, r.bc_len_mm, "
      "  r.id_left_mm, r.id_right_mm, r.od_left_mm, r.od_right_mm, "
      "  r.runout_left_mm, r.runout_right_mm, "
      "  r.tolerance_json, r.extra_json "
      "FROM measurement m "
      "LEFT JOIN measurement_result r ON r.measurement_id = m.id "
      "WHERE m.id = :id "
      "LIMIT 1;");
  q.bindValue(":id", QVariant::fromValue<qulonglong>(measurement_id));

  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }

  if (!q.next()) {
    out->found = false;
    return true;
  }

  out->found = true;
  out->measurement_id = q.value(0).toULongLong();
  out->measurement_uuid = q.value(1).toString();

  out->plc_cycle_id = q.value(2);
  out->plc_cycle_item_id = q.value(3);
  out->task_id = q.value(4);
  out->task_item_id = q.value(5);

  out->part_id = q.value(6).toString();
  out->part_type = q.value(7).toString();
  out->slot_id = q.value(8).toString();
  out->slot_index = q.value(9);
  out->item_index = q.value(10);

  out->measure_mode = q.value(11).toString();
  out->measure_round = q.value(12).toInt();
  out->result_judgement = q.value(13).toString();
  out->upload_kind = q.value(14).toString();

  out->measured_at_utc = q.value(15).toDateTime();
  out->operator_id = q.value(16).toString();

  out->review_status = q.value(17).toString();
  out->reviewer_id = q.value(18).toString();
  out->reviewed_at_utc = q.value(19).toDateTime();
  out->review_note = q.value(20).toString();

  out->fail_reason_code = q.value(21).toString();
  out->fail_reason_text = q.value(22).toString();
  out->status = q.value(23).toString();

  out->total_len_mm = q.value(24);
  out->ad_len_mm = q.value(25);
  out->bc_len_mm = q.value(26);
  out->id_left_mm = q.value(27);
  out->id_right_mm = q.value(28);
  out->od_left_mm = q.value(29);
  out->od_right_mm = q.value(30);
  out->runout_left_mm = q.value(31);
  out->runout_right_mm = q.value(32);

  out->tolerance_json = q.value(33).toString();
  out->extra_json = q.value(34).toString();

  return true;
}

// 这张 raw_file_index 不是一个简单“路径索引表”，它已经承载了一部分 raw
// 文件元信息,意味着以后接 raw_v2 时，这张表依然很有价值，不只是简单挂个路径。
bool Db::insertRawFileIndexForMeasurement(
    const QString &measurement_uuid, quint64 measurement_id,
    const QVariant &plc_cycle_id, const QString &file_path,
    quint64 file_size_bytes, int format_version, quint64 file_crc32,
    quint64 chunk_mask, const QString &scan_kind, int main_channels, int rings,
    int points_per_ring, double angle_step_deg, const QString &meta_json,
    const QString &raw_kind, QString *err) {
  QSqlQuery q(db_);
  q.prepare(
      "INSERT INTO raw_file_index ("
      " measurement_uuid, measurement_id, plc_cycle_id, "
      " file_path, file_size_bytes, format_version, file_crc32, chunk_mask, "
      " scan_kind, main_channels, rings, points_per_ring, angle_step_deg, "
      " meta_json, raw_kind, created_at_utc"
      ") VALUES ("
      " :measurement_uuid, :measurement_id, :plc_cycle_id, "
      " :file_path, :file_size_bytes, :format_version, :file_crc32, "
      ":chunk_mask, "
      " :scan_kind, :main_channels, :rings, :points_per_ring, :angle_step_deg, "
      " :meta_json, :raw_kind, NOW(3)"
      ");");

  q.bindValue(":measurement_uuid", measurement_uuid);
  q.bindValue(":measurement_id",
              QVariant::fromValue<qulonglong>(measurement_id));
  q.bindValue(":plc_cycle_id", plc_cycle_id);

  q.bindValue(":file_path", file_path);
  q.bindValue(":file_size_bytes",
              QVariant::fromValue<qulonglong>(file_size_bytes));
  q.bindValue(":format_version", format_version);
  q.bindValue(":file_crc32", QVariant::fromValue<qulonglong>(file_crc32));
  q.bindValue(":chunk_mask", QVariant::fromValue<qulonglong>(chunk_mask));

  q.bindValue(":scan_kind", scan_kind); // "A" / "B" 或你当前旧链路里的值
  q.bindValue(":main_channels", main_channels);
  q.bindValue(":rings", rings);
  q.bindValue(":points_per_ring", points_per_ring);
  q.bindValue(":angle_step_deg", angle_step_deg);

  q.bindValue(":meta_json", meta_json);
  q.bindValue(":raw_kind", raw_kind); // MAILBOX_V2 / DERIVED / EXPORT

  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }
  return true;
}

/*
SQL查询的作用
    这段SQL查询（固定部分 + 动态部分）的核心作用是：
    联合两个表进行查询：
        主表：measure_result - 存储测量结果数据
        从表：mes_outbox - 存储MES上传队列数据
        通过LEFT JOIN连接，获取测量结果及其上传状态的完整信息
    提供灵活的过滤条件：
        固定部分：基本的表连接和基础筛选条件（状态为'READY'、时间范围）
        动态部分：根据用户需求添加的额外过滤条件（工件ID、类型、合格性、MES状态等）
完整的工作流程
    用户在UI界面设置过滤条件：
        选择时间范围
        输入工件ID（可选）
        选择工件类型（可选）
        选择合格性（可选）
        选择MES状态（可选）
    UI层调用queryMesUploadRows函数：
        将用户设置的过滤条件封装为MesUploadFilter结构体
        调用数据库层的查询函数
    数据库层动态构建SQL查询：
        基础SQL语句（固定部分）
        根据过滤条件添加WHERE子句（动态部分）
        执行查询并获取结果
    结果处理和返回：
        将查询结果映射到MesUploadRow结构体数组
        返回给UI层
    UI层显示结果：
        将MesUploadRow数组显示在表格或列表中
        用户可以看到测量结果及其MES上传状态
*/
/*
queryMesUploadRows()函数，固定的 SQL
查询语句用于从两个数据表中联合查询测量结果及其对应的消息状态信息。
使用一定的筛选条件，结合两个表，筛选出需要的字段数据，然后再动态动态构建SQL查询，根据用户提供的过滤条件
（`MesUploadFilter`
结构体）向基础SQL语句添加额外的筛选条件（**就是上位机界面中数据管理UI中的筛选需要显示的数据**）。
然后将`MesUploadFilter`
结构体中的值和SQL的占位符进行绑定，进行动态筛选。最后将查询结果进行遍历轮询，
将每一个结果放入到`MesUploadRow`结构体中，最终将所有轮询的结果放入到QVector<MesUploadRow>数组中返回，
等待数据管理界面的调用。
*/
// 查询：measure_result LEFT JOIN mes_outbox
// 根据过滤条件查询测量结果及其MES上传状态。返回一个MesUploadRow数组，每个元素包含一个测量结果的完整信息和上传状态。

} // namespace core
