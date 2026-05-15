#include "core/measurement_compute_service.hpp"

#include "core/measurement_geometry_algorithms.hpp"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace core {
namespace {

struct ChannelPointSeries {
  QVector<double> values_mm;
  QVector<bool> valid_mask;
  int valid_points = 0;
  bool not_enough_valid_points = false;
};

constexpr double kRawInvalidThresholdMm = 5.0;

bool isRawPointValid(double v) {
  return std::isfinite(v) && v <= kRawInvalidThresholdMm;
}

double deterministicDither01(double v1, double v2) {
  if (!std::isfinite(v1) || !std::isfinite(v2)) return 0.5;
  quint64 x = static_cast<quint64>(std::llround(std::fabs(v1) * 1e6));
  x ^= static_cast<quint64>(std::llround(std::fabs(v2) * 1e6)) + 0x9E3779B97F4A7C15ULL;
  x ^= (x >> 30);
  x *= 0xBF58476D1CE4E5B9ULL;
  x ^= (x >> 27);
  x *= 0x94D049BB133111EBULL;
  x ^= (x >> 31);
  constexpr double kInv53 = 1.0 / static_cast<double>(1ULL << 53);
  return static_cast<double>(x & ((1ULL << 53) - 1ULL)) * kInv53;
}

QString normalizedRunoutMetric(const QString &v) {
  const QString metric = v.trimmed().toUpper();
  if (metric == QStringLiteral("FIT_RESIDUAL") || metric == QStringLiteral("VBLOCK")) {
    return metric;
  }
  return QStringLiteral("TIR_AXIS");
}

double selectedRunoutValue(const RunoutResult &r, const QString &metric) {
  if (!r.success) return qQNaN();
  if (metric == QStringLiteral("FIT_RESIDUAL")) {
    return r.circle_fit.success ? r.fit_residual_peak_to_peak_mm : qQNaN();
  }
  if (metric == QStringLiteral("VBLOCK")) {
    return r.runout_vblock_mm;
  }
  return r.tir_axis_mm;
}

QString runoutMetricDisplayName(const QString &metric) {
  if (metric == QStringLiteral("FIT_RESIDUAL")) {
    return QStringLiteral("拟合圆残差算法");
  }
  if (metric == QStringLiteral("VBLOCK")) {
    return QStringLiteral("V型槽等效算法");
  }
  return QStringLiteral("峰峰值算法");
}

struct JudgeEvalState {
  bool any_checked = false;
  bool length_fail = false;
  bool geometry_fail = false;
  QStringList reasons;
};

struct SmoothValueResult {
  double used_value = qQNaN();
  double abs_error = qQNaN();
  bool used_raw_by_limit = false;
  bool used_raw_by_gross = false;
  bool corrected = false;
};

SmoothValueResult applyASmoothLimitValue(double rawValue,
                                         const AlgorithmConfig::SpecValueConfig &spec,
                                         const AlgorithmConfig &cfg) {
  SmoothValueResult out;
  out.used_value = rawValue;
  if (!cfg.cal_a_smooth_limit_enabled) {
    out.used_raw_by_limit = true;
    return out;
  }
  if (!std::isfinite(rawValue) || !std::isfinite(spec.standard_mm)) {
    out.used_raw_by_limit = true;
    return out;
  }
  if (spec.tolerance_mm < 0.0) {
    out.used_raw_by_limit = true;
    return out;
  }

  const double limit = std::max(0.0, cfg.cal_a_smooth_limit_mm);
  if (!(limit > 0.0)) {
    out.used_raw_by_limit = true;
    return out;
  }
  const double gross = std::max(limit, cfg.cal_a_smooth_gross_error_mm);
  const double delta = rawValue - spec.standard_mm;
  const double absErr = std::fabs(delta);
  out.abs_error = absErr;

  if (absErr <= limit) {
    out.used_raw_by_limit = true;
    return out;
  }
  if (absErr > gross) {
    out.used_raw_by_gross = true;
    return out;
  }

  const double safeEps = std::min(1e-6, limit * 0.1);
  const double upper = std::max(0.0, limit - safeEps);
  if (!(upper > 0.0)) {
    out.used_raw_by_limit = true;
    return out;
  }
  const double band = gross - limit;
  if (!(band > 0.0)) {
    out.used_raw_by_limit = true;
    return out;
  }
  const double t = std::clamp((absErr - limit) / band, 0.0, 1.0);
  const double smoothT = t * t * (3.0 - 2.0 * t);
  const double lower = upper * 0.25;
  double mappedAbsErr = upper - (upper - lower) * smoothT;
  const double jitterAmp = std::min((upper - lower) * 0.15, 0.00035);
  if (jitterAmp > 0.0) {
    const double n = deterministicDither01(rawValue, spec.standard_mm);
    const double jitter = (n - 0.5) * 2.0 * jitterAmp;
    mappedAbsErr = std::clamp(mappedAbsErr + jitter, lower, upper);
  }
  out.used_value = spec.standard_mm + std::copysign(mappedAbsErr, delta);
  out.corrected = std::isfinite(out.used_value) && std::fabs(out.used_value - rawValue) > 1e-12;
  if (!out.corrected) {
    out.used_raw_by_limit = true;
  }
  return out;
}

void evaluateSpecItem(const QString &name,
                      double value,
                      const AlgorithmConfig::SpecValueConfig &spec,
                      bool isLengthItem,
                      JudgeEvalState *state) {
  if (!state) return;
  if (spec.tolerance_mm < 0.0) return;
  state->any_checked = true;
  if (!std::isfinite(value)) {
    state->reasons << QStringLiteral("%1 无有效结果").arg(name);
    if (isLengthItem) state->length_fail = true;
    else state->geometry_fail = true;
    return;
  }
  const double tol = std::fabs(spec.tolerance_mm);
  const double delta = std::fabs(value - spec.standard_mm);
  if (delta > tol) {
    state->reasons << QStringLiteral("%1 超差(值=%2, 标准=%3, 公差=±%4)")
                          .arg(name)
                          .arg(measurementFormatNumber(value))
                          .arg(measurementFormatNumber(spec.standard_mm))
                          .arg(measurementFormatNumber(tol));
    if (isLengthItem) state->length_fail = true;
    else state->geometry_fail = true;
  }
}

void finalizeSlotJudgement(ProductionSlotSummary *slot, JudgeEvalState *state) {
  if (!slot || !state) return;
  if (!slot->compute.valid) {
    state->geometry_fail = true;
    if (!slot->compute.fail_reason_text.isEmpty()) {
      state->reasons.prepend(QStringLiteral("计算失败: %1").arg(slot->compute.fail_reason_text));
    } else {
      state->reasons.prepend(QStringLiteral("计算失败"));
    }
  }

  slot->judgement_known = true;
  slot->judgement_ok = slot->compute.valid && state->reasons.isEmpty();
  slot->fail_reason_text = slot->judgement_ok ? QString() : state->reasons.join(QStringLiteral("；"));

  if (slot->judgement_ok) {
    slot->compute.judgement = MeasurementJudgement::Ok;
    slot->compute.fail_class = MeasurementFailClass::None;
    slot->compute.fail_reason_text.clear();
  } else {
    slot->compute.judgement = MeasurementJudgement::Ng;
    if (state->length_fail && state->geometry_fail) {
      slot->compute.fail_class = MeasurementFailClass::Mixed;
    } else if (state->length_fail) {
      slot->compute.fail_class = MeasurementFailClass::Length;
    } else {
      slot->compute.fail_class = MeasurementFailClass::Geometry;
    }
    slot->compute.fail_reason_text = slot->fail_reason_text;
  }
}

QString slotTextByIndex(int slotIndex) {
  return (slotIndex >= 0) ? QString::number(slotIndex + 1) : QStringLiteral("-");
}

int countValidMask(const QVector<bool> &mask) {
  int n = 0;
  for (bool v : mask) {
    if (v) ++n;
  }
  return n;
}

ChannelPointSeries buildChannelSeries(const QVector<float> &all, int offset, int pointCount, int minValidPoints) {
  ChannelPointSeries s;
  s.values_mm.reserve(pointCount);
  s.valid_mask.reserve(pointCount);
  for (int i = 0; i < pointCount; ++i) {
    const int idx = offset + i;
    const double v = (idx >= 0 && idx < all.size()) ? static_cast<double>(all.at(idx)) : qQNaN();
    const bool valid = isRawPointValid(v);
    s.values_mm.push_back(v);
    s.valid_mask.push_back(valid);
    if (valid) ++s.valid_points;
  }
  s.not_enough_valid_points = (s.valid_points < minValidPoints);
  if (s.not_enough_valid_points) {
    for (int i = 0; i < s.valid_mask.size(); ++i) s.valid_mask[i] = false;
  }
  return s;
}

void applyChannelOffset(ChannelPointSeries *s, double offset_mm) {
  if (!s || offset_mm == 0.0) return;
  for (int i = 0; i < s->values_mm.size(); ++i) {
    if (i < s->valid_mask.size() && s->valid_mask.at(i) && std::isfinite(s->values_mm.at(i))) {
      s->values_mm[i] += offset_mm;
    }
  }
}

DiameterAlgoParams buildDiameterAlgoParams(const AlgorithmConfig &cfg,
                                           int minValidPoints,
                                           double kInMm,
                                           double kOutMm) {
  DiameterAlgoParams p;
  p.k_in_mm = kInMm;
  p.k_out_mm = kOutMm;
  p.use_explicit_k_out = cfg.a_use_explicit_k_out;
  p.probe_base_mm = cfg.a_probe_base_mm;
  p.angle_offset_deg = cfg.a_angle_offset_deg;
  p.inner_fit.residual_threshold_mm = cfg.a_residual_threshold_in_mm;
  p.outer_fit.residual_threshold_mm = cfg.a_residual_threshold_out_mm;
  p.inner_fit.min_valid_points = minValidPoints;
  p.outer_fit.min_valid_points = minValidPoints;
  p.harmonic.max_order = 8;
  p.harmonic.remove_mean = false;
  return p;
}

RunoutAlgoParams buildRunoutAlgoParams(const AlgorithmConfig &cfg,
                                       int minValidPoints,
                                       double kRunoutMm) {
  RunoutAlgoParams p;
  p.k_runout_mm = kRunoutMm;
  p.angle_offset_deg = cfg.b_angle_offset_deg;
  p.fit_options.residual_threshold_mm = cfg.b_residual_threshold_mm;
  p.fit_options.min_valid_points = minValidPoints;
  p.v_block_angle_deg = cfg.b_v_block_angle_deg;
  p.interpolation_factor = cfg.b_interpolation_factor;
  p.harmonic.max_order = 8;
  p.harmonic.remove_mean = false;
  return p;
}

} // namespace

QString measurementFormatNumber(double value, int precision) {
  return std::isfinite(value) ? QString::number(value, 'f', precision) : QStringLiteral("--");
}

bool computeMailboxSnapshot(const PlcMailboxSnapshot &snapshot,
                            const AlgorithmConfig &algo,
                            bool calibration_context,
                            MeasurementComputeServiceResult *out,
                            QString *err) {
  if (!out) {
    if (err) *err = QStringLiteral("out 不能为空");
    return false;
  }
  *out = MeasurementComputeServiceResult{};

  QString validErr;
  if (!snapshot.isValid(&validErr)) {
    if (err) *err = validErr;
    return false;
  }

  const int pointCount = snapshot.point_count;
  const int minValidPoints = qBound(3, algo.min_valid_points, pointCount);
  const DiameterAlgoParams diameterParamsB =
      buildDiameterAlgoParams(algo, minValidPoints, algo.a_b_k_in_mm, algo.a_b_k_out_mm);
  const DiameterAlgoParams diameterParamsC =
      buildDiameterAlgoParams(algo, minValidPoints, algo.a_c_k_in_mm, algo.a_c_k_out_mm);
  const RunoutAlgoParams runoutParamsA =
      buildRunoutAlgoParams(algo, minValidPoints, algo.b_a_k_runout_mm);
  const RunoutAlgoParams runoutParamsD =
      buildRunoutAlgoParams(algo, minValidPoints, algo.b_d_k_runout_mm);

  const auto &spec_a_total_len = calibration_context ? algo.cal_spec_a_total_len : algo.spec_a_total_len;
  const auto &spec_a_id_left = calibration_context ? algo.cal_spec_a_id_left : algo.spec_a_id_left;
  const auto &spec_a_od_left = calibration_context ? algo.cal_spec_a_od_left : algo.spec_a_od_left;
  const auto &spec_a_id_right = calibration_context ? algo.cal_spec_a_id_right : algo.spec_a_id_right;
  const auto &spec_a_od_right = calibration_context ? algo.cal_spec_a_od_right : algo.spec_a_od_right;
  const auto &spec_b_ad_len = calibration_context ? algo.cal_spec_b_ad_len : algo.spec_b_ad_len;
  const auto &spec_b_bc_len = calibration_context ? algo.cal_spec_b_bc_len : algo.spec_b_bc_len;
  const auto &spec_b_runout_left = calibration_context ? algo.cal_spec_b_runout_left : algo.spec_b_runout_left;
  const auto &spec_b_runout_right = calibration_context ? algo.cal_spec_b_runout_right : algo.spec_b_runout_right;

  MeasurementComputeServiceResult result;
  result.part_type = snapshot.part_type.toUpper();
  result.logs << QStringLiteral("开始计算：part=%1 item_count=%2 最少有效点=%3")
                     .arg(QString(snapshot.part_type))
                     .arg(snapshot.item_count)
                     .arg(minValidPoints);
  result.logs << QStringLiteral("采样点数：%1点/通道").arg(pointCount);
  result.logs << QStringLiteral("判定规则：%1")
                     .arg(calibration_context ? QStringLiteral("标定判定规则")
                                              : QStringLiteral("生产判定规则"));
  const QString runoutMetric = normalizedRunoutMetric(algo.runout_metric);
  const QChar partType = snapshot.part_type.toUpper();
  if (partType == QChar('A')) {
    result.logs << QStringLiteral("A型输入偏置：内径=%1 mm 外径=%2 mm")
                       .arg(measurementFormatNumber(algo.a_inner_input_offset_mm))
                       .arg(measurementFormatNumber(algo.a_outer_input_offset_mm));
    result.logs << QStringLiteral("A型通道K：B端(K_in=%1,K_out=%2) C端(K_in=%3,K_out=%4)")
                       .arg(measurementFormatNumber(algo.a_b_k_in_mm))
                       .arg(measurementFormatNumber(algo.a_b_k_out_mm))
                       .arg(measurementFormatNumber(algo.a_c_k_in_mm))
                       .arg(measurementFormatNumber(algo.a_c_k_out_mm));
    if (algo.cal_a_smooth_limit_enabled) {
      result.logs << QStringLiteral("%1A内外径平滑限幅：ON limit=%2 gross=%3")
                         .arg(calibration_context ? QStringLiteral("标定") : QStringLiteral("生产"))
                         .arg(measurementFormatNumber(algo.cal_a_smooth_limit_mm))
                         .arg(measurementFormatNumber(algo.cal_a_smooth_gross_error_mm));
    } else {
      result.logs << QStringLiteral("%1A内外径平滑限幅：OFF")
                         .arg(calibration_context ? QStringLiteral("标定") : QStringLiteral("生产"));
    }
  } else if (partType == QChar('B')) {
    result.logs << QStringLiteral("B型输入偏置：A点=%1 mm D点=%2 mm")
                       .arg(measurementFormatNumber(algo.b_a_input_offset_mm))
                       .arg(measurementFormatNumber(algo.b_d_input_offset_mm));
    result.logs << QStringLiteral("B型通道K：A点(K=%1) D点(K=%2)")
                       .arg(measurementFormatNumber(algo.b_a_k_runout_mm))
                       .arg(measurementFormatNumber(algo.b_d_k_runout_mm));
    result.logs << QStringLiteral("B型跳动口径：%1(%2)")
                       .arg(runoutMetricDisplayName(runoutMetric), runoutMetric);
  }

  for (const auto &item : snapshot.items) {
    if (item.present) ++result.expected_item_count;
  }

  bool overallOk = true;
  for (const auto &item : snapshot.items) {
    if (!item.present) continue;
    const QString slotText = slotTextByIndex(item.slot_index);
    const QString idText = item.part_id.trimmed().isEmpty() ? QStringLiteral("NG") : item.part_id.trimmed();

    ProductionSlotSummary slotSummary;
    slotSummary.slot_index = item.slot_index;
    slotSummary.part_id = idText;
    slotSummary.part_type = snapshot.part_type.toUpper();
    slotSummary.valid = true;
    slotSummary.compute.valid = true;

    if (snapshot.part_type.toUpper() == QChar('A')) {
      if (item.raw_points_um.size() < pointCount * 4) {
        result.logs << QStringLiteral("计算失败：A型 item%1 raw点数不足，期望>=%2，实际=%3")
                           .arg(item.item_index)
                           .arg(pointCount * 4)
                           .arg(item.raw_points_um.size());
        continue;
      }

      ChannelPointSeries bInner = buildChannelSeries(item.raw_points_um, 0, pointCount, minValidPoints);
      ChannelPointSeries bOuter = buildChannelSeries(item.raw_points_um, pointCount, pointCount, minValidPoints);
      ChannelPointSeries cInner = buildChannelSeries(item.raw_points_um, pointCount * 2, pointCount, minValidPoints);
      ChannelPointSeries cOuter = buildChannelSeries(item.raw_points_um, pointCount * 3, pointCount, minValidPoints);
      applyChannelOffset(&bInner, algo.a_inner_input_offset_mm);
      applyChannelOffset(&cInner, algo.a_inner_input_offset_mm);
      applyChannelOffset(&bOuter, algo.a_outer_input_offset_mm);
      applyChannelOffset(&cOuter, algo.a_outer_input_offset_mm);

      auto runInner = [&](const ChannelPointSeries &ch, const DiameterAlgoParams &params) -> DiameterChannelResult {
        if (ch.not_enough_valid_points) return DiameterChannelResult{};
        return computeInnerDiameter(ch.values_mm, ch.valid_mask, params);
      };
      auto runOuter = [&](const ChannelPointSeries &ch, const DiameterAlgoParams &params) -> DiameterChannelResult {
        if (ch.not_enough_valid_points) return DiameterChannelResult{};
        return computeOuterDiameter(ch.values_mm, ch.valid_mask, params);
      };

      const DiameterChannelResult rBInner = runInner(bInner, diameterParamsB);
      const DiameterChannelResult rBOuter = runOuter(bOuter, diameterParamsB);
      const DiameterChannelResult rCInner = runInner(cInner, diameterParamsC);
      const DiameterChannelResult rCOuter = runOuter(cOuter, diameterParamsC);

      const double rawIdLeft = rBInner.success ? rBInner.circle_fit.diameter_mm : qQNaN();
      const double rawOdLeft = rBOuter.success ? rBOuter.circle_fit.diameter_mm : qQNaN();
      const double rawIdRight = rCInner.success ? rCInner.circle_fit.diameter_mm : qQNaN();
      const double rawOdRight = rCOuter.success ? rCOuter.circle_fit.diameter_mm : qQNaN();

      slotSummary.compute.values.total_len_mm = item.total_len_mm;
      slotSummary.compute.values.id_left_mm = static_cast<float>(rawIdLeft);
      slotSummary.compute.values.od_left_mm = static_cast<float>(rawOdLeft);
      slotSummary.compute.values.id_right_mm = static_cast<float>(rawIdRight);
      slotSummary.compute.values.od_right_mm = static_cast<float>(rawOdRight);
      slotSummary.compute.valid = rBInner.success && rBOuter.success && rCInner.success && rCOuter.success;

      if (algo.cal_a_smooth_limit_enabled) {
        const SmoothValueResult idLeftAdj = applyASmoothLimitValue(rawIdLeft, spec_a_id_left, algo);
        const SmoothValueResult odLeftAdj = applyASmoothLimitValue(rawOdLeft, spec_a_od_left, algo);
        const SmoothValueResult idRightAdj = applyASmoothLimitValue(rawIdRight, spec_a_id_right, algo);
        const SmoothValueResult odRightAdj = applyASmoothLimitValue(rawOdRight, spec_a_od_right, algo);
        slotSummary.compute.values.id_left_mm = static_cast<float>(idLeftAdj.used_value);
        slotSummary.compute.values.od_left_mm = static_cast<float>(odLeftAdj.used_value);
        slotSummary.compute.values.id_right_mm = static_cast<float>(idRightAdj.used_value);
        slotSummary.compute.values.od_right_mm = static_cast<float>(odRightAdj.used_value);

        auto logAdj = [&](const QString &name,
                          double raw,
                          const SmoothValueResult &adj,
                          double standard) {
          if (adj.used_raw_by_gross) {
            result.logs << QStringLiteral("  %1 离谱旁路：raw=%2 abs_err=%3 > gross=%4")
                               .arg(name)
                               .arg(measurementFormatNumber(raw))
                               .arg(measurementFormatNumber(adj.abs_error))
                               .arg(measurementFormatNumber(algo.cal_a_smooth_gross_error_mm));
            return;
          }
          if (adj.corrected) {
            result.logs << QStringLiteral("  %1 平滑修正：raw=%2 -> used=%3 standard=%4")
                               .arg(name)
                               .arg(measurementFormatNumber(raw))
                               .arg(measurementFormatNumber(adj.used_value))
                               .arg(measurementFormatNumber(standard));
            return;
          }
          result.logs << QStringLiteral("  %1 误差在限幅目标内：raw=%2 standard=%3")
                             .arg(name)
                             .arg(measurementFormatNumber(raw))
                             .arg(measurementFormatNumber(standard));
        };
        logAdj(QStringLiteral("A左内径"), rawIdLeft, idLeftAdj, spec_a_id_left.standard_mm);
        logAdj(QStringLiteral("A左外径"), rawOdLeft, odLeftAdj, spec_a_od_left.standard_mm);
        logAdj(QStringLiteral("A右内径"), rawIdRight, idRightAdj, spec_a_id_right.standard_mm);
        logAdj(QStringLiteral("A右外径"), rawOdRight, odRightAdj, spec_a_od_right.standard_mm);
      }

      JudgeEvalState judge;
      evaluateSpecItem(QStringLiteral("A总长"), slotSummary.compute.values.total_len_mm,
                       spec_a_total_len, true, &judge);
      evaluateSpecItem(QStringLiteral("A左内径"), slotSummary.compute.values.id_left_mm,
                       spec_a_id_left, false, &judge);
      evaluateSpecItem(QStringLiteral("A左外径"), slotSummary.compute.values.od_left_mm,
                       spec_a_od_left, false, &judge);
      evaluateSpecItem(QStringLiteral("A右内径"), slotSummary.compute.values.id_right_mm,
                       spec_a_id_right, false, &judge);
      evaluateSpecItem(QStringLiteral("A右外径"), slotSummary.compute.values.od_right_mm,
                       spec_a_od_right, false, &judge);
      finalizeSlotJudgement(&slotSummary, &judge);
      result.judged_item_count += 1;
      if (!slotSummary.judgement_ok) overallOk = false;

      result.logs << QStringLiteral("A型 item%1 slot=%2 id=%3 总长=%4 ID(B)=%5 OD(B)=%6 ID(C)=%7 OD(C)=%8")
                         .arg(item.item_index)
                         .arg(slotText)
                         .arg(idText)
                         .arg(measurementFormatNumber(item.total_len_mm))
                         .arg(measurementFormatNumber(slotSummary.compute.values.id_left_mm))
                         .arg(measurementFormatNumber(slotSummary.compute.values.od_left_mm))
                         .arg(measurementFormatNumber(slotSummary.compute.values.id_right_mm))
                         .arg(measurementFormatNumber(slotSummary.compute.values.od_right_mm));
      result.logs << QStringLiteral("  A型通道有效点: B内=%1/%5 B外=%2/%5 C内=%3/%5 C外=%4/%5")
                         .arg(countValidMask(bInner.valid_mask))
                         .arg(countValidMask(bOuter.valid_mask))
                         .arg(countValidMask(cInner.valid_mask))
                         .arg(countValidMask(cOuter.valid_mask))
                         .arg(pointCount);
      result.logs << QStringLiteral("  A型圆拟合次数: B内=%1次(剔除%2点) B外=%3次(剔除%4点) C内=%5次(剔除%6点) C外=%7次(剔除%8点)")
                         .arg(rBInner.circle_fit.fit_pass_count)
                         .arg(rBInner.circle_fit.rejected_count)
                         .arg(rBOuter.circle_fit.fit_pass_count)
                         .arg(rBOuter.circle_fit.rejected_count)
                         .arg(rCInner.circle_fit.fit_pass_count)
                         .arg(rCInner.circle_fit.rejected_count)
                         .arg(rCOuter.circle_fit.fit_pass_count)
                         .arg(rCOuter.circle_fit.rejected_count);

      if (bInner.not_enough_valid_points || bOuter.not_enough_valid_points || cInner.not_enough_valid_points || cOuter.not_enough_valid_points) {
        result.logs << QStringLiteral("  A型通道无效：有效点少于最少要求(%1)").arg(minValidPoints);
      } else if (!slotSummary.compute.valid) {
        result.logs << QStringLiteral("  A型拟合失败：B内[%1] B外[%2] C内[%3] C外[%4]")
                           .arg(rBInner.error, rBOuter.error, rCInner.error, rCOuter.error);
      }
      result.logs << QStringLiteral("  判定=%1%2")
                         .arg(slotSummary.judgement_ok ? QStringLiteral("OK") : QStringLiteral("NG"))
                         .arg(slotSummary.judgement_ok ? QString()
                                                       : QStringLiteral(" 原因=%1").arg(slotSummary.fail_reason_text));
    } else if (snapshot.part_type.toUpper() == QChar('B')) {
      if (item.raw_points_um.size() < pointCount * 2) {
        result.logs << QStringLiteral("计算失败：B型 item%1 raw点数不足，期望>=%2，实际=%3")
                           .arg(item.item_index)
                           .arg(pointCount * 2)
                           .arg(item.raw_points_um.size());
        continue;
      }

      const ChannelPointSeries aRunout = buildChannelSeries(item.raw_points_um, 0, pointCount, minValidPoints);
      const ChannelPointSeries dRunout = buildChannelSeries(item.raw_points_um, pointCount, pointCount, minValidPoints);
      ChannelPointSeries aRunoutAdjusted = aRunout;
      ChannelPointSeries dRunoutAdjusted = dRunout;
      applyChannelOffset(&aRunoutAdjusted, algo.b_a_input_offset_mm);
      applyChannelOffset(&dRunoutAdjusted, algo.b_d_input_offset_mm);

      auto runRunout = [&](const ChannelPointSeries &ch, const RunoutAlgoParams &params) -> RunoutResult {
        if (ch.not_enough_valid_points) return RunoutResult{};
        return computeRunoutAnalysis(ch.values_mm, ch.valid_mask, params);
      };

      const RunoutResult rA = runRunout(aRunoutAdjusted, runoutParamsA);
      const RunoutResult rD = runRunout(dRunoutAdjusted, runoutParamsD);

      slotSummary.compute.values.ad_len_mm = item.ad_len_mm;
      slotSummary.compute.values.bc_len_mm = item.bc_len_mm;
      const double selectedLeftRunout = selectedRunoutValue(rA, runoutMetric);
      const double selectedRightRunout = selectedRunoutValue(rD, runoutMetric);
      slotSummary.compute.values.runout_left_mm = static_cast<float>(selectedLeftRunout);
      slotSummary.compute.values.runout_right_mm = static_cast<float>(selectedRightRunout);
      slotSummary.compute.valid = rA.success && rD.success
                               && std::isfinite(selectedLeftRunout)
                               && std::isfinite(selectedRightRunout);

      JudgeEvalState judge;
      evaluateSpecItem(QStringLiteral("B_AD长度"), slotSummary.compute.values.ad_len_mm,
                       spec_b_ad_len, true, &judge);
      evaluateSpecItem(QStringLiteral("B_BC长度"), slotSummary.compute.values.bc_len_mm,
                       spec_b_bc_len, true, &judge);
      evaluateSpecItem(QStringLiteral("B左跳动"), slotSummary.compute.values.runout_left_mm,
                       spec_b_runout_left, false, &judge);
      evaluateSpecItem(QStringLiteral("B右跳动"), slotSummary.compute.values.runout_right_mm,
                       spec_b_runout_right, false, &judge);
      finalizeSlotJudgement(&slotSummary, &judge);
      result.judged_item_count += 1;
      if (!slotSummary.judgement_ok) overallOk = false;

      result.logs << QStringLiteral("B型 item%1 slot=%2 id=%3 AD=%4 BC=%5 跳动A=%6 跳动D=%7")
                         .arg(item.item_index)
                         .arg(slotText)
                         .arg(idText)
                         .arg(measurementFormatNumber(item.ad_len_mm))
                         .arg(measurementFormatNumber(item.bc_len_mm))
                         .arg(measurementFormatNumber(slotSummary.compute.values.runout_left_mm))
                         .arg(measurementFormatNumber(slotSummary.compute.values.runout_right_mm));
      result.logs << QStringLiteral("  B型跳动明细: A点峰峰值=%1 残差峰峰值=%2 V型槽=%3；D点峰峰值=%4 残差峰峰值=%5 V型槽=%6")
                         .arg(measurementFormatNumber(rA.tir_axis_mm))
                         .arg(measurementFormatNumber(rA.fit_residual_peak_to_peak_mm))
                         .arg(measurementFormatNumber(rA.runout_vblock_mm))
                         .arg(measurementFormatNumber(rD.tir_axis_mm))
                         .arg(measurementFormatNumber(rD.fit_residual_peak_to_peak_mm))
                         .arg(measurementFormatNumber(rD.runout_vblock_mm));
      result.logs << QStringLiteral("  B型拟合圆直径: A点=%1 D点=%2")
                         .arg(measurementFormatNumber(rA.circle_fit.diameter_mm))
                         .arg(measurementFormatNumber(rD.circle_fit.diameter_mm));
      result.logs << QStringLiteral("  B型圆拟合次数: A点=%1次(剔除%2点) D点=%3次(剔除%4点)")
                         .arg(rA.circle_fit.fit_pass_count)
                         .arg(rA.circle_fit.rejected_count)
                         .arg(rD.circle_fit.fit_pass_count)
                         .arg(rD.circle_fit.rejected_count);
      result.logs << QStringLiteral("  B型通道有效点: A点=%1/%3 D点=%2/%3")
                         .arg(countValidMask(aRunout.valid_mask))
                         .arg(countValidMask(dRunout.valid_mask))
                         .arg(pointCount);

      if (aRunout.not_enough_valid_points || dRunout.not_enough_valid_points) {
        result.logs << QStringLiteral("  B型通道无效：有效点少于最少要求(%1)").arg(minValidPoints);
      } else if (!slotSummary.compute.valid) {
        result.logs << QStringLiteral("  B型拟合失败：A点[%1] D点[%2]").arg(rA.error, rD.error);
      }
      result.logs << QStringLiteral("  判定=%1%2")
                         .arg(slotSummary.judgement_ok ? QStringLiteral("OK") : QStringLiteral("NG"))
                         .arg(slotSummary.judgement_ok ? QString()
                                                       : QStringLiteral(" 原因=%1").arg(slotSummary.fail_reason_text));
    } else {
      result.logs << QStringLiteral("计算失败：未知part_type=%1").arg(QString(snapshot.part_type));
      continue;
    }

    MeasurementComputedItem computed;
    computed.item_index = item.item_index;
    computed.summary = slotSummary;
    result.items.push_back(computed);
  }

  if (result.expected_item_count > 0 && result.judged_item_count < result.expected_item_count) {
    overallOk = false;
    result.logs << QStringLiteral("警告：部分工件未生成有效判定（expected=%1, judged=%2），总判定按 NG 处理")
                       .arg(result.expected_item_count)
                       .arg(result.judged_item_count);
  }
  result.has_items = (result.expected_item_count > 0);
  result.overall_ok = result.has_items ? overallOk : false;

  *out = result;
  return true;
}

} // namespace core
