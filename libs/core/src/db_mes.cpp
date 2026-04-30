#include "core/db.hpp"

#include <QDateTime>
#include <QUuid>
#include <QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>

#include "core/mes_payload.hpp"

namespace core {
namespace {

QString normalizedUpper(const QString &v) { return v.trimmed().toUpper(); }

QString interfaceCodeForMeasureMode(const QString &measureMode) {
  const QString mode = normalizedUpper(measureMode);
  if (mode == QStringLiteral("NORMAL"))
    return QStringLiteral("MES_PROD_NORMAL_RESULT");
  if (mode == QStringLiteral("SECOND"))
    return QStringLiteral("MES_PROD_SECOND_RESULT");
  if (mode == QStringLiteral("THIRD"))
    return QStringLiteral("MES_PROD_THIRD_RESULT");
  if (mode == QStringLiteral("MIL"))
    return QStringLiteral("MES_PROD_MIL_RESULT");
  return QString();
}

QString defaultBusinessKey(const QString &taskCardNo, const QString &partId,
                           const QString &interfaceCode,
                           const QString &measurementUuid) {
  const QString left = taskCardNo.trimmed().isEmpty() ? measurementUuid : taskCardNo;
  return QStringLiteral("%1|%2|%3").arg(left, partId, interfaceCode);
}

struct QueueSourceRow {
  quint64 measurement_id = 0;
  QVariant task_id;
  QVariant task_item_id;
  QString task_card_no;
  QString measurement_uuid;
  QString part_id;
  QString part_type;
  QString measure_mode;
  QString attempt_kind;
  QString result_judgement;
  QString status;
  QDateTime measured_at_utc;
  QVariant total_len_mm;
  QVariant bc_len_mm;
  bool ok = false;
  bool effective = true;
  QString run_kind;
};

bool loadQueueSourceRow(QSqlDatabase &db, const QString &uuid, QueueSourceRow *out,
                        QString *err) {
  if (!out)
    return false;

  QSqlQuery q(db);
  q.prepare(
      "SELECT "
      "  m.id, m.task_id, m.task_item_id, IFNULL(t.task_card_no, ''), "
      "  m.measurement_uuid, m.part_id, m.part_type, "
      "  IFNULL(m.measure_mode, ''), "
      "  IFNULL(m.attempt_kind, CASE WHEN UPPER(m.measure_mode)='RETEST' THEN 'RETEST' ELSE 'PRIMARY' END), "
      "  m.result_judgement, m.status, m.measured_at_utc, "
      "  r.total_len_mm, r.bc_len_mm, "
      "  CASE WHEN m.result_judgement='OK' THEN 1 ELSE 0 END AS ok_flag, "
      "  IFNULL(m.is_effective, 1), IFNULL(m.run_kind, 'PRODUCTION') "
      "FROM measurement m "
      "LEFT JOIN measurement_result r ON r.measurement_id = m.id "
      "LEFT JOIN mes_task t ON t.id = m.task_id "
      "WHERE m.measurement_uuid=:u LIMIT 1;");
  q.bindValue(":u", uuid);
  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }
  if (!q.next()) {
    if (err)
      *err = QStringLiteral("measurement not found for uuid");
    return false;
  }

  out->measurement_id = q.value(0).toULongLong();
  out->task_id = q.value(1);
  out->task_item_id = q.value(2);
  out->task_card_no = q.value(3).toString();
  out->measurement_uuid = q.value(4).toString();
  out->part_id = q.value(5).toString();
  out->part_type = q.value(6).toString();
  out->measure_mode = q.value(7).toString();
  out->attempt_kind = q.value(8).toString();
  out->result_judgement = q.value(9).toString();
  out->status = q.value(10).toString();
  out->measured_at_utc = q.value(11).toDateTime();
  out->total_len_mm = q.value(12);
  out->bc_len_mm = q.value(13);
  out->ok = q.value(14).toInt() != 0;
  out->effective = q.value(15).toInt() != 0;
  out->run_kind = q.value(16).toString();
  return true;
}

bool loadRawForMeasurement(QSqlDatabase &db, const QString &uuid,
                           RawWriteInfoV2 *raw, QString *err) {
  if (!raw)
    return false;
  QSqlQuery r(db);
  r.prepare("SELECT file_path, file_size_bytes, format_version, file_crc32, "
            "chunk_mask, scan_kind, main_channels, "
            "rings, points_per_ring, angle_step_deg, meta_json "
            "FROM raw_file_index WHERE measurement_uuid=:u LIMIT 1;");
  r.bindValue(":u", uuid);
  if (!r.exec()) {
    if (err)
      *err = r.lastError().text();
    return false;
  }
  if (!r.next()) {
    if (err)
      *err = QStringLiteral("raw_file_index not found for uuid");
    return false;
  }

  raw->final_path = r.value(0).toString();
  raw->file_size_bytes = r.value(1).toULongLong();
  raw->format_version = r.value(2).toInt();
  raw->file_crc32 = r.value(3).toULongLong();
  raw->chunk_mask = r.value(4).toULongLong();
  raw->scan_kind = r.value(5).toString();
  raw->main_channels = r.value(6).toInt();
  raw->rings = r.value(7).toInt();
  raw->points_per_ring = r.value(8).toInt();
  raw->angle_step_deg = r.value(9).toFloat();
  raw->meta_json = r.value(10).toString();
  return true;
}

QString buildPayloadFromSource(const QueueSourceRow &src, const RawWriteInfoV2 &raw) {
  MeasureResult mr;
  mr.measurement_uuid = src.measurement_uuid;
  mr.part_id = src.part_id;
  mr.part_type = src.part_type;
  mr.ok = src.ok;
  mr.measured_at_utc = src.measured_at_utc;
  mr.total_len_mm = src.total_len_mm.isNull() ? 0.0 : src.total_len_mm.toDouble();
  mr.bc_len_mm = src.bc_len_mm.isNull() ? 0.0 : src.bc_len_mm.toDouble();
  mr.status = src.status;
  return buildMesPayloadV1(mr, raw);
}

QString interfaceCodeSqlExpr() {
  return QStringLiteral(
      "CASE UPPER(IFNULL(m.measure_mode, '')) "
      " WHEN 'NORMAL' THEN 'MES_PROD_NORMAL_RESULT' "
      " WHEN 'SECOND' THEN 'MES_PROD_SECOND_RESULT' "
      " WHEN 'THIRD' THEN 'MES_PROD_THIRD_RESULT' "
      " WHEN 'MIL' THEN 'MES_PROD_MIL_RESULT' "
      " ELSE '' END");
}

} // namespace

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
  q.bindValue(":report_type", report_type);
  q.bindValue(":interface_code", interface_code);
  q.bindValue(":business_key", business_key);
  q.bindValue(":need_upload", need_upload ? 1 : 0);
  q.bindValue(":report_status", report_status);
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

QVector<core::MesUploadRow>
core::Db::queryMesUploadRows(const MesUploadFilter &f, int limit,
                             QString *err) {
  QVector<MesUploadRow> out;
  QSqlQuery q(db_);

  const QString ifaceExpr = interfaceCodeSqlExpr();
  QString sql =
      QStringLiteral(
          "SELECT "
          "  IFNULL(mr.id, 0), IFNULL(mr.report_uuid, ''), "
          "  m.measurement_uuid, m.part_id, m.part_type, IFNULL(t.task_card_no, ''), "
          "  IFNULL(m.run_kind, 'PRODUCTION'), "
          "  IFNULL(m.measure_mode, ''), "
          "  IFNULL(m.attempt_kind, CASE WHEN UPPER(m.measure_mode)='RETEST' THEN 'RETEST' ELSE 'PRIMARY' END), "
          "  IFNULL(m.is_effective, 1), "
          "  CASE WHEN m.result_judgement='OK' THEN 1 ELSE 0 END AS ok_flag, "
          "  m.measured_at_utc, "
          "  r.total_len_mm, r.bc_len_mm, "
          "  IFNULL(mr.interface_code, %1) AS interface_code, "
          "  IFNULL(mr.report_status, 'NOT_CREATED') AS report_status, "
          "  o.status AS mes_status, o.attempt_count, o.last_error, o.updated_at_utc "
          "FROM measurement m "
          "LEFT JOIN measurement_result r ON r.measurement_id = m.id "
          "LEFT JOIN mes_task t ON t.id = m.task_id "
          "LEFT JOIN mes_report mr ON mr.id = ("
          "  SELECT mr2.id FROM mes_report mr2 "
          "  WHERE mr2.measurement_id = m.id "
          "  ORDER BY mr2.id DESC LIMIT 1"
          ") "
          "LEFT JOIN mes_outbox o ON o.id = ("
          "  SELECT o2.id FROM mes_outbox o2 "
          "  WHERE ((mr.id IS NOT NULL AND o2.mes_report_id = mr.id) "
          "      OR (mr.id IS NULL AND o2.measurement_uuid = m.measurement_uuid AND o2.event_type='MEASURE_RESULT_READY')) "
          "  ORDER BY o2.id DESC LIMIT 1"
          ") "
          "WHERE m.measured_at_utc >= :from_utc AND m.measured_at_utc <= :to_utc "
          "  AND IFNULL(m.run_kind, 'PRODUCTION') = 'PRODUCTION' "
          "  AND IFNULL(m.is_effective, 1) = 1 ")
          .arg(ifaceExpr);

  if (!f.part_id_like.trimmed().isEmpty())
    sql += QStringLiteral(" AND m.part_id LIKE :part_id_like ");
  if (!f.task_card_no_like.trimmed().isEmpty())
    sql += QStringLiteral(" AND IFNULL(t.task_card_no, '') LIKE :task_card_no_like ");
  if (!f.part_type.trimmed().isEmpty())
    sql += QStringLiteral(" AND m.part_type = :part_type ");
  if (f.ok_filter == 0 || f.ok_filter == 1)
    sql += QStringLiteral(" AND CASE WHEN m.result_judgement='OK' THEN 1 ELSE 0 END = :ok ");
  if (!f.mes_status.trimmed().isEmpty()) {
    if (f.mes_status == QStringLiteral("NOT_QUEUED"))
      sql += QStringLiteral(" AND o.id IS NULL ");
    else
      sql += QStringLiteral(" AND o.status = :mes_status ");
  }

  sql += QStringLiteral(" ORDER BY m.measured_at_utc DESC, m.id DESC LIMIT :limit;");

  q.prepare(sql);
  q.bindValue(":from_utc", f.from_utc);
  q.bindValue(":to_utc", f.to_utc);
  if (!f.part_id_like.trimmed().isEmpty())
    q.bindValue(":part_id_like", QStringLiteral("%") + f.part_id_like + QStringLiteral("%"));
  if (!f.task_card_no_like.trimmed().isEmpty())
    q.bindValue(":task_card_no_like", QStringLiteral("%") + f.task_card_no_like + QStringLiteral("%"));
  if (!f.part_type.trimmed().isEmpty())
    q.bindValue(":part_type", f.part_type);
  if (f.ok_filter == 0 || f.ok_filter == 1)
    q.bindValue(":ok", f.ok_filter);
  if (!f.mes_status.trimmed().isEmpty() && f.mes_status != QStringLiteral("NOT_QUEUED"))
    q.bindValue(":mes_status", f.mes_status);
  q.bindValue(":limit", limit);

  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return out;
  }

  while (q.next()) {
    MesUploadRow r;
    r.mes_report_id = q.value(0).toULongLong();
    r.report_uuid = q.value(1).toString();
    r.measurement_uuid = q.value(2).toString();
    r.part_id = q.value(3).toString();
    r.part_type = q.value(4).toString();
    r.task_card_no = q.value(5).toString();
    r.run_kind = q.value(6).toString();
    r.measure_mode = q.value(7).toString();
    r.attempt_kind = q.value(8).toString();
    r.is_effective = q.value(9).toInt() != 0;
    r.ok = q.value(10).toInt() != 0;
    r.measured_at_utc = q.value(11).toDateTime();
    r.total_len_mm = q.value(12).isNull() ? 0.0 : q.value(12).toDouble();
    r.bc_len_mm = q.value(13).isNull() ? 0.0 : q.value(13).toDouble();
    r.interface_code = q.value(14).toString();
    r.report_status = q.value(15).toString();

    if (q.value(16).isNull()) {
      r.mes_status = QStringLiteral("NOT_QUEUED");
      r.attempt_count = 0;
    } else {
      r.mes_status = q.value(16).toString();
      r.attempt_count = q.value(17).toInt();
      r.last_error = q.value(18).toString();
      r.mes_updated_at_utc = q.value(19).toDateTime();
    }

    out.push_back(r);
  }

  return out;
}

bool core::Db::queueMesUploadByUuid(const QString &uuid, QString *err) {
  QueueSourceRow src;
  if (!loadQueueSourceRow(db_, uuid, &src, err))
    return false;

  if (src.run_kind != QStringLiteral("PRODUCTION")) {
    if (err)
      *err = QStringLiteral("CALIBRATION measurement is not allowed to upload MES");
    return false;
  }
  if (!src.effective) {
    if (err)
      *err = QStringLiteral("measurement already superseded by a newer effective record");
    return false;
  }

  const QString interfaceCode = interfaceCodeForMeasureMode(src.measure_mode);
  if (interfaceCode.isEmpty()) {
    if (err)
      *err = QStringLiteral("unsupported measure_mode for MES upload: %1").arg(src.measure_mode);
    return false;
  }

  RawWriteInfoV2 raw;
  if (!loadRawForMeasurement(db_, uuid, &raw, err))
    return false;
  const QString payload = buildPayloadFromSource(src, raw);
  const QString businessKey = defaultBusinessKey(src.task_card_no, src.part_id, interfaceCode,
                                                 src.measurement_uuid);

  if (!db_.transaction()) {
    if (err)
      *err = db_.lastError().text();
    return false;
  }

  quint64 mesReportId = 0;
  QString reportUuid;
  {
    QSqlQuery q(db_);
    q.prepare("SELECT id, report_uuid FROM mes_report WHERE measurement_id=:mid ORDER BY id DESC LIMIT 1;");
    q.bindValue(":mid", QVariant::fromValue<qulonglong>(src.measurement_id));
    if (!q.exec()) {
      db_.rollback();
      if (err)
        *err = q.lastError().text();
      return false;
    }

    if (q.next()) {
      mesReportId = q.value(0).toULongLong();
      reportUuid = q.value(1).toString();
      QSqlQuery up(db_);
      up.prepare("UPDATE mes_report SET "
                 " interface_code=:iface, business_key=:bk, need_upload=1, "
                 " report_status='PENDING', payload_json=:payload, updated_at_utc=NOW(3) "
                 "WHERE id=:id;");
      up.bindValue(":iface", interfaceCode);
      up.bindValue(":bk", businessKey);
      up.bindValue(":payload", payload);
      up.bindValue(":id", QVariant::fromValue<qulonglong>(mesReportId));
      if (!up.exec()) {
        db_.rollback();
        if (err)
          *err = up.lastError().text();
        return false;
      }
    } else {
      reportUuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
      QString createErr;
      if (!createMesReport(src.measurement_id, src.task_id, src.task_item_id,
                           reportUuid, QStringLiteral("MEASURE_RESULT"),
                           interfaceCode, businessKey, true,
                           QStringLiteral("PENDING"), payload, &mesReportId,
                           &createErr)) {
        db_.rollback();
        if (err)
          *err = createErr;
        return false;
      }
    }
  }

  {
    QSqlQuery q(db_);
    q.prepare("SELECT id, status FROM mes_outbox "
              "WHERE (mes_report_id=:rid) "
              "   OR (measurement_uuid=:u AND event_type='MEASURE_RESULT_READY') "
              "ORDER BY id DESC LIMIT 1;");
    q.bindValue(":rid", QVariant::fromValue<qulonglong>(mesReportId));
    q.bindValue(":u", uuid);
    if (!q.exec()) {
      db_.rollback();
      if (err)
        *err = q.lastError().text();
      return false;
    }

    if (q.next()) {
      const quint64 outboxId = q.value(0).toULongLong();
      const QString status = q.value(1).toString();
      if (status == QStringLiteral("SENT")) {
        db_.rollback();
        if (err)
          *err = QStringLiteral("Already SENT; skip.");
        return false;
      }
      QSqlQuery up(db_);
      up.prepare("UPDATE mes_outbox SET "
                 " mes_report_id=:rid, measurement_uuid=:u, "
                 " event_type='MEASURE_RESULT_READY', payload_json=:payload, "
                 " status='PENDING', next_retry_at_utc=NOW(3), "
                 " last_error=NULL, updated_at_utc=NOW(3) "
                 "WHERE id=:id;");
      up.bindValue(":rid", QVariant::fromValue<qulonglong>(mesReportId));
      up.bindValue(":u", uuid);
      up.bindValue(":payload", payload);
      up.bindValue(":id", QVariant::fromValue<qulonglong>(outboxId));
      if (!up.exec()) {
        db_.rollback();
        if (err)
          *err = up.lastError().text();
        return false;
      }
    } else {
      QSqlQuery ins(db_);
      ins.prepare("INSERT INTO mes_outbox("
                  " measurement_uuid, event_type, mes_report_id, payload_json,"
                  " status, attempt_count, next_retry_at_utc,"
                  " created_at_utc, updated_at_utc"
                  ") VALUES ("
                  " :u, 'MEASURE_RESULT_READY', :rid, :payload,"
                  " 'PENDING', 0, NOW(3), NOW(3), NOW(3)"
                  ");");
      ins.bindValue(":u", uuid);
      ins.bindValue(":rid", QVariant::fromValue<qulonglong>(mesReportId));
      ins.bindValue(":payload", payload);
      if (!ins.exec()) {
        db_.rollback();
        if (err)
          *err = ins.lastError().text();
        return false;
      }
    }
  }

  if (!db_.commit()) {
    if (err)
      *err = db_.lastError().text();
    return false;
  }

  return true;
}

int core::Db::retryFailed(const QVector<QString> &uuids, QString *err) {
  if (uuids.isEmpty())
    return 0;

  int cnt = 0;
  for (const auto &u : uuids) {
    QSqlQuery q(db_);
    q.prepare("UPDATE mes_outbox SET status='PENDING', last_error=NULL, "
              "next_retry_at_utc=NOW(3), updated_at_utc=NOW(3) "
              "WHERE measurement_uuid=:u AND status='FAILED';");
    q.bindValue(":u", u);
    if (!q.exec()) {
      if (err)
        *err = q.lastError().text();
      return cnt;
    }
    cnt += q.numRowsAffected();
  }
  return cnt;
}

bool core::Db::resetStaleSending(int stale_seconds, QString *err) {
  QSqlQuery q(db_);
  q.prepare("UPDATE mes_outbox SET status='FAILED', "
            "updated_at_utc=NOW(3), last_error=IFNULL(last_error,'') "
            "WHERE status='SENDING' AND updated_at_utc < (NOW(3) - "
            "INTERVAL :s SECOND);");
  q.bindValue(":s", stale_seconds);
  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }

  QSqlQuery q2(db_);
  q2.prepare("UPDATE mes_report mr "
             "JOIN mes_outbox o ON o.mes_report_id = mr.id "
             "SET mr.report_status='FAILED', mr.updated_at_utc=NOW(3) "
             "WHERE o.status='FAILED' AND o.updated_at_utc >= (NOW(3) - INTERVAL :s SECOND);");
  q2.bindValue(":s", stale_seconds);
  if (!q2.exec()) {
    if (err)
      *err = q2.lastError().text();
    return false;
  }
  return true;
}

bool core::Db::fetchNextDueOutbox(MesOutboxTask *task, QString *err) {
  if (!task)
    return false;
  QSqlQuery q(db_);
  const QString ifaceExpr = QStringLiteral(
      "CASE UPPER(IFNULL(m.measure_mode, '')) "
      " WHEN 'NORMAL' THEN 'MES_PROD_NORMAL_RESULT' "
      " WHEN 'SECOND' THEN 'MES_PROD_SECOND_RESULT' "
      " WHEN 'THIRD' THEN 'MES_PROD_THIRD_RESULT' "
      " WHEN 'MIL' THEN 'MES_PROD_MIL_RESULT' "
      " ELSE '' END");
  q.prepare(QStringLiteral(
      "SELECT o.id, IFNULL(o.mes_report_id, 0), IFNULL(mr.report_uuid, ''), "
      "       o.measurement_uuid, IFNULL(mr.interface_code, %1), "
      "       IFNULL(mr.business_key, ''), o.payload_json, o.attempt_count "
      "FROM mes_outbox o "
      "LEFT JOIN mes_report mr ON mr.id = o.mes_report_id "
      "LEFT JOIN measurement m ON m.measurement_uuid = o.measurement_uuid "
      "WHERE o.status IN ('PENDING','FAILED') "
      "  AND o.next_retry_at_utc <= NOW(3) "
      "ORDER BY o.id ASC LIMIT 1;").arg(ifaceExpr));
  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }
  if (!q.next())
    return false;

  task->id = q.value(0).toULongLong();
  task->mes_report_id = q.value(1).toULongLong();
  task->report_uuid = q.value(2).toString();
  task->measurement_uuid = q.value(3).toString();
  task->interface_code = q.value(4).toString();
  task->business_key = q.value(5).toString();
  task->payload_json = q.value(6).toString();
  task->attempt_count = q.value(7).toInt();
  return true;
}

bool core::Db::markOutboxSending(quint64 id, QString *err) {
  QSqlQuery q(db_);
  q.prepare("UPDATE mes_outbox SET status='SENDING', "
            "updated_at_utc=NOW(3) WHERE id=:id;");
  q.bindValue(":id", QVariant::fromValue<qulonglong>(id));
  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }

  QSqlQuery q2(db_);
  q2.prepare("UPDATE mes_report mr "
             "JOIN mes_outbox o ON o.mes_report_id = mr.id "
             "SET mr.report_status='SENDING', mr.updated_at_utc=NOW(3) "
             "WHERE o.id=:id;");
  q2.bindValue(":id", QVariant::fromValue<qulonglong>(id));
  if (!q2.exec()) {
    if (err)
      *err = q2.lastError().text();
    return false;
  }
  return true;
}

bool core::Db::markOutboxSent(quint64 id, int http_code, const QString &resp,
                              QString *err) {
  QSqlQuery q(db_);
  q.prepare("UPDATE mes_outbox SET "
            " status='SENT', last_http_code=:c, last_response_body=:r, "
            " last_error=NULL, updated_at_utc=NOW(3) "
            "WHERE id=:id;");
  q.bindValue(":c", http_code);
  q.bindValue(":r", resp.left(4000));
  q.bindValue(":id", QVariant::fromValue<qulonglong>(id));
  if (!q.exec()) {
    if (err)
      *err = q.lastError().text();
    return false;
  }

  QSqlQuery q2(db_);
  q2.prepare("UPDATE mes_report mr "
             "JOIN mes_outbox o ON o.mes_report_id = mr.id "
             "SET mr.report_status='SENT', mr.response_json=:resp, mr.updated_at_utc=NOW(3) "
             "WHERE o.id=:id;");
  q2.bindValue(":resp", resp.left(4000));
  q2.bindValue(":id", QVariant::fromValue<qulonglong>(id));
  if (!q2.exec()) {
    if (err)
      *err = q2.lastError().text();
    return false;
  }
  return true;
}

bool core::Db::markOutboxFailed(quint64 id, int http_code, const QString &resp,
                                const QString &error, int next_retry_seconds,
                                QString *err) {
  QSqlQuery q(db_);
  q.prepare("UPDATE mes_outbox SET "
            " status='FAILED', attempt_count=attempt_count+1, "
            " last_http_code=:c, last_response_body=:r, last_error=:e, "
            " next_retry_at_utc=(NOW(3) + INTERVAL :s SECOND), "
            " updated_at_utc=NOW(3) "
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

  QSqlQuery q2(db_);
  q2.prepare("UPDATE mes_report mr "
             "JOIN mes_outbox o ON o.mes_report_id = mr.id "
             "SET mr.report_status='FAILED', mr.response_json=:resp, mr.updated_at_utc=NOW(3) "
             "WHERE o.id=:id;");
  q2.bindValue(":resp", error.left(4000));
  q2.bindValue(":id", QVariant::fromValue<qulonglong>(id));
  if (!q2.exec()) {
    if (err)
      *err = q2.lastError().text();
    return false;
  }
  return true;
}

} // namespace core
