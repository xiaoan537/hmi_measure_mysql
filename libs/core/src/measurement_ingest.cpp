#include "core/measurement_ingest.hpp"

#include <QUuid>

#include "core/db.hpp"

namespace core {

namespace {

QString ensureUuid(const QString &v) {
  if (!v.trimmed().isEmpty()) {
    return v.trimmed();
  }
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString interfaceCodeForMeasureMode(const QString &measureMode) {
  const QString mode = measureMode.trimmed().toUpper();
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

} // namespace

MeasurementIngestService::MeasurementIngestService(Db &db) : db_(db) {}

bool MeasurementIngestService::validateRequest(
    const MeasurementIngestRequest &req, QString *err) const {
  auto fail = [&](const QString &msg) -> bool {
    if (err)
      *err = msg;
    return false;
  };

  if (req.cycle.part_type != "A" && req.cycle.part_type != "B") {
    return fail(QStringLiteral("cycle.part_type 必须是 A 或 B"));
  }

  if (req.items.isEmpty()) {
    return fail(QStringLiteral("items 不能为空"));
  }

  if (req.items.size() > 2) {
    return fail(QStringLiteral("当前版本 items 最多支持 2 个"));
  }

  if (!req.results.isEmpty() && req.results.size() != req.items.size()) {
    return fail(QStringLiteral("results 非空时，数量必须与 items 相同"));
  }

  if (!req.raws.isEmpty() && req.raws.size() != req.items.size()) {
    return fail(QStringLiteral("raws 非空时，数量必须与 items 相同"));
  }

  if (!req.reports.isEmpty() && req.reports.size() != req.items.size()) {
    return fail(QStringLiteral("reports 非空时，数量必须与 items 相同"));
  }

  for (int i = 0; i < req.items.size(); ++i) {
    const auto &it = req.items[i];
    if (it.item_index < 0 || it.item_index > 1) {
      return fail(QStringLiteral("items[%1].item_index 必须是 0 或 1").arg(i));
    }
    if (it.part_id.trimmed().isEmpty()) {
      return fail(QStringLiteral("items[%1].part_id 不能为空").arg(i));
    }
    if (it.run_kind != "PRODUCTION" && it.run_kind != "CALIBRATION") {
      return fail(QStringLiteral("items[%1].run_kind 必须是 PRODUCTION 或 CALIBRATION").arg(i));
    }
    if (it.run_kind == "PRODUCTION" && it.measure_mode.trimmed().isEmpty()) {
      return fail(QStringLiteral("items[%1].measure_mode 不能为空").arg(i));
    }
    if (it.result_judgement.trimmed().isEmpty()) {
      return fail(QStringLiteral("items[%1].result_judgement 不能为空").arg(i));
    }
    if (!req.reports.isEmpty()) {
      const auto &rp = req.reports[i];
      if (rp.create_mes_report && it.run_kind != QStringLiteral("PRODUCTION")) {
        return fail(QStringLiteral("items[%1] 是 CALIBRATION，不允许创建 MES report").arg(i));
      }
      if (rp.create_mes_report && interfaceCodeForMeasureMode(it.measure_mode).isEmpty()) {
        return fail(QStringLiteral("items[%1].measure_mode 无法映射到 MES 接口").arg(i));
      }
    }
  }

  return true;
}

bool MeasurementIngestService::ingest(const MeasurementIngestRequest &req,
                                      MeasurementIngestResponse *resp,
                                      QString *err) {
  auto fail = [&](const QString &msg) -> bool {
    if (err)
      *err = msg;
    return false;
  };

  if (!validateRequest(req, err)) {
    return false;
  }

  if (resp) {
    *resp = MeasurementIngestResponse{};
  }

  QString txErr;
  if (!db_.beginTx(&txErr)) {
    return fail(QStringLiteral("beginTx failed: %1").arg(txErr));
  }

  quint64 cycleId = 0;
  const QString cycleUuid = ensureUuid(req.cycle.cycle_uuid);
  const QDateTime measuredAtUtc = req.cycle.measured_at_utc.isValid()
                                      ? req.cycle.measured_at_utc
                                      : QDateTime::currentDateTimeUtc();

  if (!db_.insertPlcCycle(cycleUuid, req.cycle.part_type,
                          req.cycle.item_count, req.cycle.source_mode,
                          req.cycle.mailbox_header_json,
                          req.cycle.mailbox_meta_json, measuredAtUtc, &cycleId,
                          &txErr)) {
    db_.rollbackTx(nullptr);
    return fail(QStringLiteral("insertPlcCycle failed: %1").arg(txErr));
  }

  if (resp) {
    resp->plc_cycle_id = cycleId;
    resp->items.clear();
  }

  for (int i = 0; i < req.items.size(); ++i) {
    const auto &item = req.items[i];
    const IngestResultInput result =
        req.results.isEmpty() ? IngestResultInput{} : req.results[i];
    const IngestRawInput raw =
        req.raws.isEmpty() ? IngestRawInput{} : req.raws[i];
    const IngestReportInput report =
        req.reports.isEmpty() ? IngestReportInput{} : req.reports[i];

    quint64 cycleItemId = 0;
    quint64 measurementId = 0;
    quint64 measurementResultId = 0;
    quint64 mesReportId = 0;

    if (!db_.insertPlcCycleItem(cycleId, item.item_index, item.slot_index,
                                item.part_id, item.result_ok,
                                item.fail_reason_code, item.fail_reason_text,
                                item.is_valid, &cycleItemId, &txErr)) {
      db_.rollbackTx(nullptr);
      return fail(QStringLiteral("insertPlcCycleItem[%1] failed: %2")
                      .arg(i)
                      .arg(txErr));
    }

    const QString measurementUuid = ensureUuid(item.measurement_uuid);

    if (!db_.insertMeasurementEx(
            measurementUuid, QVariant::fromValue<qulonglong>(cycleId),
            QVariant::fromValue<qulonglong>(cycleItemId), item.task_id,
            item.task_item_id, item.part_id, req.cycle.part_type, item.slot_id,
            item.slot_index, QVariant(item.item_index), item.measure_mode,
            item.measure_round, item.result_judgement, item.upload_kind,
            measuredAtUtc, item.operator_id, item.review_status,
            item.fail_reason_code, item.fail_reason_text, item.status,
            &measurementId, &txErr, item.run_kind, item.attempt_kind,
            item.fail_class, item.is_effective, item.superseded_by)) {
      db_.rollbackTx(nullptr);
      return fail(QStringLiteral("insertMeasurementEx[%1] failed: %2")
                      .arg(i)
                      .arg(txErr));
    }

    if (!db_.insertMeasurementResultEx(
            measurementId, result.total_len_mm, result.ad_len_mm,
            result.bc_len_mm, result.id_left_mm, result.id_right_mm,
            result.od_left_mm, result.od_right_mm, result.runout_left_mm,
            result.runout_right_mm, result.tolerance_json, result.extra_json,
            &measurementResultId, &txErr)) {
      db_.rollbackTx(nullptr);
      return fail(QStringLiteral("insertMeasurementResultEx[%1] failed: %2")
                      .arg(i)
                      .arg(txErr));
    }

    if (!db_.bindCycleItemMeasurement(cycleItemId, measurementId, &txErr)) {
      db_.rollbackTx(nullptr);
      return fail(QStringLiteral("bindCycleItemMeasurement[%1] failed: %2")
                      .arg(i)
                      .arg(txErr));
    }

    bool rawWritten = false;
    if (raw.enabled) {
      if (!db_.insertRawFileIndexForMeasurement(
              measurementUuid, measurementId,
              QVariant::fromValue<qulonglong>(cycleId), raw.file_path,
              raw.file_size_bytes, raw.format_version, raw.file_crc32,
              raw.chunk_mask, raw.scan_kind, raw.main_channels, raw.rings,
              raw.points_per_ring, raw.angle_step_deg, raw.meta_json,
              raw.raw_kind, &txErr)) {
        db_.rollbackTx(nullptr);
        return fail(
            QStringLiteral("insertRawFileIndexForMeasurement[%1] failed: %2")
                .arg(i)
                .arg(txErr));
      }
      rawWritten = true;
    }

    bool reportCreated = false;
    if (report.create_mes_report) {
      const QString reportUuid = ensureUuid(report.report_uuid);
      const QString interfaceCode = report.interface_code.trimmed().isEmpty()
                                        ? interfaceCodeForMeasureMode(item.measure_mode)
                                        : report.interface_code.trimmed();
      const QString reportType = report.report_type.trimmed().isEmpty()
                                     ? QStringLiteral("MEASURE_RESULT")
                                     : report.report_type.trimmed();
      if (!db_.createMesReport(measurementId, item.task_id, item.task_item_id,
                               reportUuid, reportType,
                               interfaceCode, report.business_key,
                               report.need_upload, report.report_status,
                               report.payload_json, &mesReportId, &txErr)) {
        db_.rollbackTx(nullptr);
        return fail(
            QStringLiteral("createMesReport[%1] failed: %2").arg(i).arg(txErr));
      }
      reportCreated = true;
    }

    if (resp) {
      MeasurementIngestItemResult r;
      r.plc_cycle_item_id = cycleItemId;
      r.measurement_id = measurementId;
      r.measurement_result_id = measurementResultId;
      r.raw_written = rawWritten;
      r.report_created = reportCreated;
      r.mes_report_id = mesReportId;
      resp->items.push_back(r);
    }
  }

  if (!db_.commitTx(&txErr)) {
    db_.rollbackTx(nullptr);
    return fail(QStringLiteral("commitTx failed: %1").arg(txErr));
  }

  return true;
}

} // namespace core
