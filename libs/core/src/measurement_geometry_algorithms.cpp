#include "core/measurement_geometry_algorithms.hpp"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace core {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

bool isFinite(double v)
{
    return std::isfinite(v) != 0;
}

double degToRad(double deg)
{
    return deg * kPi / 180.0;
}

int countValid(const QVector<bool> &mask)
{
    int count = 0;
    for (bool v : mask) {
        if (v) ++count;
    }
    return count;
}

bool solveLinear3x3(double a[3][4])
{
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        double best = std::fabs(a[col][col]);
        for (int row = col + 1; row < 3; ++row) {
            const double cand = std::fabs(a[row][col]);
            if (cand > best) {
                best = cand;
                pivot = row;
            }
        }
        if (best < 1e-12) {
            return false;
        }
        if (pivot != col) {
            for (int k = col; k < 4; ++k) {
                std::swap(a[col][k], a[pivot][k]);
            }
        }
        const double div = a[col][col];
        for (int k = col; k < 4; ++k) {
            a[col][k] /= div;
        }
        for (int row = 0; row < 3; ++row) {
            if (row == col) continue;
            const double factor = a[row][col];
            for (int k = col; k < 4; ++k) {
                a[row][k] -= factor * a[col][k];
            }
        }
    }
    return true;
}

bool solveLinear3(const double normal[3][3],
                  const double rhs[3],
                  double out[3])
{
    double aug[3][4] = {
        {normal[0][0], normal[0][1], normal[0][2], rhs[0]},
        {normal[1][0], normal[1][1], normal[1][2], rhs[1]},
        {normal[2][0], normal[2][1], normal[2][2], rhs[2]},
    };
    if (!solveLinear3x3(aug)) {
        return false;
    }
    out[0] = aug[0][3];
    out[1] = aug[1][3];
    out[2] = aug[2][3];
    return true;
}

bool algebraicCircleFit(const PointSet2D &pointSet,
                        const QVector<bool> &mask,
                        double *cx,
                        double *cy,
                        double *radius,
                        QString *err)
{
    if (!cx || !cy || !radius) {
        if (err) *err = QStringLiteral("algebraicCircleFit 输出指针不能为空");
        return false;
    }

    double normal[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    double rhs[3] = {0.0, 0.0, 0.0};
    int used = 0;

    for (int i = 0; i < pointSet.points.size(); ++i) {
        if (i >= mask.size() || !mask.at(i)) continue;
        const auto &p = pointSet.points.at(i);
        if (!p.valid || !isFinite(p.x) || !isFinite(p.y)) continue;

        const double row[3] = {p.x, p.y, 1.0};
        const double bi = -(p.x * p.x + p.y * p.y);

        for (int r = 0; r < 3; ++r) {
            rhs[r] += row[r] * bi;
            for (int c = 0; c < 3; ++c) {
                normal[r][c] += row[r] * row[c];
            }
        }
        ++used;
    }

    if (used < 3) {
        if (err) *err = QStringLiteral("代数圆拟合有效点不足");
        return false;
    }

    double abc[3] = {0.0, 0.0, 0.0};
    if (!solveLinear3(normal, rhs, abc)) {
        if (err) *err = QStringLiteral("代数圆拟合求解失败");
        return false;
    }

    const double A = abc[0];
    const double B = abc[1];
    const double C = abc[2];
    const double a = -A / 2.0;
    const double b = -B / 2.0;
    const double rad2 = a * a + b * b - C;
    if (!(rad2 > 0.0) || !isFinite(rad2)) {
        if (err) *err = QStringLiteral("代数圆拟合得到非法半径");
        return false;
    }

    *cx = a;
    *cy = b;
    *radius = std::sqrt(rad2);
    return true;
}

bool solveSymmetric3(double m[3][3], double rhs[3], double delta[3])
{
    return solveLinear3(m, rhs, delta);
}

bool geometricCircleRefine(const PointSet2D &pointSet,
                           const QVector<bool> &mask,
                           int maxIterations,
                           double tolerance,
                           double *cx,
                           double *cy,
                           double *radius,
                           QString *err)
{
    if (!cx || !cy || !radius) {
        if (err) *err = QStringLiteral("geometricCircleRefine 输出指针不能为空");
        return false;
    }

    double a = *cx;
    double b = *cy;
    double R = *radius;

    for (int iter = 0; iter < maxIterations; ++iter) {
        double jtj[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
        double jte[3] = {0.0, 0.0, 0.0};
        int used = 0;

        for (int i = 0; i < pointSet.points.size(); ++i) {
            if (i >= mask.size() || !mask.at(i)) continue;
            const auto &p = pointSet.points.at(i);
            if (!p.valid || !isFinite(p.x) || !isFinite(p.y)) continue;

            const double dx = a - p.x;
            const double dy = b - p.y;
            const double di = std::sqrt(dx * dx + dy * dy);
            if (!(di > 1e-12)) continue;

            const double ei = di - R;
            const double jac[3] = {dx / di, dy / di, -1.0};
            for (int r = 0; r < 3; ++r) {
                jte[r] += jac[r] * ei;
                for (int c = 0; c < 3; ++c) {
                    jtj[r][c] += jac[r] * jac[c];
                }
            }
            ++used;
        }

        if (used < 3) {
            if (err) *err = QStringLiteral("几何圆拟合有效点不足");
            return false;
        }

        double delta[3] = {0.0, 0.0, 0.0};
        double negJte[3] = {-jte[0], -jte[1], -jte[2]};
        if (!solveSymmetric3(jtj, negJte, delta)) {
            if (err) *err = QStringLiteral("几何圆拟合线性求解失败");
            return false;
        }

        a += delta[0];
        b += delta[1];
        R += delta[2];

        const double stepNorm = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
        if (stepNorm < tolerance) {
            break;
        }
    }

    if (!(R > 0.0) || !isFinite(R)) {
        if (err) *err = QStringLiteral("几何圆拟合得到非法半径");
        return false;
    }

    *cx = a;
    *cy = b;
    *radius = R;
    return true;
}

QVector<double> computeResiduals(const PointSet2D &pointSet,
                                 double cx,
                                 double cy,
                                 double radius)
{
    QVector<double> residuals(pointSet.points.size(), std::numeric_limits<double>::quiet_NaN());
    for (int i = 0; i < pointSet.points.size(); ++i) {
        const auto &p = pointSet.points.at(i);
        if (!p.valid || !isFinite(p.x) || !isFinite(p.y)) continue;
        const double d = std::hypot(p.x - cx, p.y - cy);
        residuals[i] = d - radius;
    }
    return residuals;
}

void fillResidualStats(const QVector<double> &residuals,
                       const QVector<bool> &mask,
                       double *rms,
                       double *maxAbs,
                       int *count)
{
    double sum2 = 0.0;
    double maxV = 0.0;
    int used = 0;
    for (int i = 0; i < residuals.size(); ++i) {
        if (i >= mask.size() || !mask.at(i)) continue;
        const double e = residuals.at(i);
        if (!isFinite(e)) continue;
        sum2 += e * e;
        maxV = std::max(maxV, std::fabs(e));
        ++used;
    }
    if (rms) *rms = (used > 0) ? std::sqrt(sum2 / static_cast<double>(used)) : 0.0;
    if (maxAbs) *maxAbs = maxV;
    if (count) *count = used;
}

AngularSeries makeRawSeries(const QVector<double> &rawValues,
                            const QVector<bool> &rawMask,
                            double rawMin,
                            double rawMax)
{
    AngularSeries series = makeAngularSeries(rawValues.size(), rawValues.isEmpty() ? 0.0 : 360.0 / rawValues.size());
    series.values = rawValues;
    series.valid_mask.resize(rawValues.size());
    for (int i = 0; i < rawValues.size(); ++i) {
        const bool userValid = (i < rawMask.size()) ? rawMask.at(i) : true;
        const double v = rawValues.at(i);
        const bool finite = isFinite(v);
        const bool inRange = (v >= rawMin && v <= rawMax);
        series.valid_mask[i] = userValid && finite && inRange;
    }
    return series;
}

double effectiveOuterOffset(const DiameterAlgoParams &params)
{
    if (params.use_explicit_k_out && isFinite(params.k_out_mm) && params.k_out_mm > 0.0) {
        return params.k_out_mm;
    }
    if (isFinite(params.k_out_mm) && params.k_out_mm > 0.0) {
        return params.k_out_mm;
    }
    return params.k_in_mm + params.probe_base_mm;
}

double effectiveProbeBase(const DiameterAlgoParams &params)
{
    return effectiveOuterOffset(params) - params.k_in_mm;
}

} // namespace

AngularSeries makeAngularSeries(int pointCount, double stepDeg)
{
    AngularSeries series;
    if (pointCount <= 0) return series;

    series.angles_deg.reserve(pointCount);
    series.values.fill(0.0, pointCount);
    series.valid_mask.fill(true, pointCount);
    for (int i = 0; i < pointCount; ++i) {
        series.angles_deg.push_back(stepDeg * static_cast<double>(i));
    }
    return series;
}

AngularSeries buildRadiusProfile(const QVector<double> &angles_deg,
                                 const QVector<double> &raw_values,
                                 const QVector<bool> &raw_valid_mask,
                                 double offset_mm,
                                 double sign,
                                 double raw_min_mm,
                                 double raw_max_mm)
{
    AngularSeries series;
    series.angles_deg = angles_deg;
    series.values.resize(raw_values.size());
    series.valid_mask.resize(raw_values.size());

    for (int i = 0; i < raw_values.size(); ++i) {
        const bool userValid = (i < raw_valid_mask.size()) ? raw_valid_mask.at(i) : true;
        const double raw = raw_values.at(i);
        const bool finite = isFinite(raw);
        const bool inRange = (raw >= raw_min_mm && raw <= raw_max_mm);
        const double radius = offset_mm + sign * raw;
        const bool radiusOk = isFinite(radius) && radius > 0.0;
        series.values[i] = radius;
        series.valid_mask[i] = userValid && finite && inRange && radiusOk;
    }
    return series;
}

PointSet2D polarToPointSet(const AngularSeries &radiusProfile,
                           double angleOffsetDeg)
{
    PointSet2D pointSet;
    pointSet.angles_deg = radiusProfile.angles_deg;
    pointSet.points.resize(radiusProfile.values.size());

    for (int i = 0; i < radiusProfile.values.size(); ++i) {
        const bool valid = (i < radiusProfile.valid_mask.size()) ? radiusProfile.valid_mask.at(i) : true;
        const double r = radiusProfile.values.at(i);
        const double angleDeg = ((i < radiusProfile.angles_deg.size()) ? radiusProfile.angles_deg.at(i) : 0.0) + angleOffsetDeg;
        const double rad = degToRad(angleDeg);

        Point2D p;
        p.valid = valid && isFinite(r) && r > 0.0;
        if (p.valid) {
            p.x = r * std::cos(rad);
            p.y = r * std::sin(rad);
        }
        pointSet.points[i] = p;
    }

    return pointSet;
}

CircleFitResult fitCircleRobust(const PointSet2D &pointSet,
                                const CircleFitOptions &options)
{
    CircleFitResult result;
    // result.final_mask.resize(pointSet.points.size(), false);
    result.final_mask.resize(pointSet.points.size());
    result.final_mask.fill(false);
    // result.residuals_mm.resize(pointSet.points.size(), std::numeric_limits<double>::quiet_NaN());
    result.residuals_mm.resize(pointSet.points.size());
    result.residuals_mm.fill(std::numeric_limits<double>::quiet_NaN());

    QVector<bool> rawMask(pointSet.points.size(), false);
    for (int i = 0; i < pointSet.points.size(); ++i) {
        const auto &p = pointSet.points.at(i);
        rawMask[i] = p.valid && isFinite(p.x) && isFinite(p.y);
    }
    result.valid_count_raw = countValid(rawMask);
    if (result.valid_count_raw < options.min_valid_points) {
        result.error = QStringLiteral("圆拟合原始有效点不足：%1 < %2")
                           .arg(result.valid_count_raw)
                           .arg(options.min_valid_points);
        return result;
    }

    double cx = 0.0, cy = 0.0, radius = 0.0;
    QString err;
    if (!algebraicCircleFit(pointSet, rawMask, &cx, &cy, &radius, &err)) {
        result.error = err;
        return result;
    }
    if (!geometricCircleRefine(pointSet, rawMask,
                               options.max_geometric_iterations,
                               options.geometric_tolerance,
                               &cx, &cy, &radius, &err)) {
        result.error = err;
        return result;
    }

    QVector<double> residuals = computeResiduals(pointSet, cx, cy, radius);
    QVector<bool> finalMask = rawMask;
    if (options.enable_second_pass) {
        for (int i = 0; i < residuals.size(); ++i) {
            if (!finalMask.at(i)) continue;
            const double e = residuals.at(i);
            if (!isFinite(e) || std::fabs(e) > options.residual_threshold_mm) {
                finalMask[i] = false;
            }
        }
        if (countValid(finalMask) >= options.min_valid_points) {
            if (!algebraicCircleFit(pointSet, finalMask, &cx, &cy, &radius, &err)) {
                result.error = err;
                return result;
            }
            if (!geometricCircleRefine(pointSet, finalMask,
                                       options.max_geometric_iterations,
                                       options.geometric_tolerance,
                                       &cx, &cy, &radius, &err)) {
                result.error = err;
                return result;
            }
            residuals = computeResiduals(pointSet, cx, cy, radius);
        } else {
            finalMask = rawMask;
        }
    }

    result.center_x_mm = cx;
    result.center_y_mm = cy;
    result.radius_mm = radius;
    result.diameter_mm = radius * 2.0;
    result.final_mask = finalMask;
    result.residuals_mm = residuals;
    result.valid_count_final = countValid(finalMask);
    result.rejected_count = result.valid_count_raw - result.valid_count_final;
    fillResidualStats(residuals, finalMask,
                      &result.residual_rms_mm,
                      &result.residual_max_abs_mm,
                      nullptr);
    result.success = result.valid_count_final >= options.min_valid_points && isFinite(result.radius_mm) && result.radius_mm > 0.0;
    if (!result.success && result.error.isEmpty()) {
        result.error = QStringLiteral("圆拟合失败");
    }
    return result;
}

HarmonicAnalysisResult analyzeHarmonics(const AngularSeries &series,
                                        const HarmonicAnalysisOptions &options)
{
    HarmonicAnalysisResult result;
    const int N = series.values.size();
    if (N <= 0 || series.valid_mask.size() != N) {
        result.error = QStringLiteral("谐波分析输入为空或 mask 长度不匹配");
        return result;
    }

    QVector<int> validIndices;
    validIndices.reserve(N);
    double mean = 0.0;
    for (int i = 0; i < N; ++i) {
        if (!series.valid_mask.at(i)) continue;
        const double v = series.values.at(i);
        if (!isFinite(v)) continue;
        validIndices.push_back(i);
        mean += v;
    }
    if (validIndices.size() < 3) {
        result.error = QStringLiteral("谐波分析有效点不足");
        return result;
    }
    mean /= static_cast<double>(validIndices.size());
    result.dc_mean_mm = mean;

    const int maxOrder = std::max(0, options.max_order);
    result.components.reserve(maxOrder);
    for (int k = 1; k <= maxOrder; ++k) {
        double ak = 0.0;
        double bk = 0.0;
        for (int idx : validIndices) {
            const double angleRad = degToRad(series.angles_deg.value(idx));
            double value = series.values.at(idx);
            if (options.remove_mean) value -= mean;
            ak += value * std::cos(static_cast<double>(k) * angleRad);
            bk += value * std::sin(static_cast<double>(k) * angleRad);
        }
        const double scale = 2.0 / static_cast<double>(validIndices.size());
        HarmonicComponent comp;
        comp.order = k;
        comp.amplitude_mm = std::hypot(scale * ak, scale * bk);
        comp.phase_rad = std::atan2(scale * bk, scale * ak);
        result.components.push_back(comp);
    }

    result.success = true;
    return result;
}

DiameterChannelResult computeInnerDiameter(const QVector<double> &raw_inner_values_mm,
                                           const QVector<bool> &raw_inner_valid_mask,
                                           const DiameterAlgoParams &params)
{
    DiameterChannelResult result;
    result.raw_series = makeRawSeries(raw_inner_values_mm,
                                      raw_inner_valid_mask,
                                      params.raw_min_in_mm,
                                      params.raw_max_in_mm);
    result.radius_profile = buildRadiusProfile(result.raw_series.angles_deg,
                                               raw_inner_values_mm,
                                               result.raw_series.valid_mask,
                                               params.k_in_mm,
                                               +1.0,
                                               params.raw_min_in_mm,
                                               params.raw_max_in_mm);
    result.point_set = polarToPointSet(result.radius_profile, params.angle_offset_deg);
    result.circle_fit = fitCircleRobust(result.point_set, params.inner_fit);
    result.harmonics = analyzeHarmonics(result.radius_profile, params.harmonic);
    result.success = result.circle_fit.success;
    if (!result.success) {
        result.error = result.circle_fit.error;
    }
    return result;
}

DiameterChannelResult computeOuterDiameter(const QVector<double> &raw_outer_values_mm,
                                           const QVector<bool> &raw_outer_valid_mask,
                                           const DiameterAlgoParams &params)
{
    DiameterChannelResult result;
    result.raw_series = makeRawSeries(raw_outer_values_mm,
                                      raw_outer_valid_mask,
                                      params.raw_min_out_mm,
                                      params.raw_max_out_mm);
    const double kOut = effectiveOuterOffset(params);
    result.radius_profile = buildRadiusProfile(result.raw_series.angles_deg,
                                               raw_outer_values_mm,
                                               result.raw_series.valid_mask,
                                               kOut,
                                               -1.0,
                                               params.raw_min_out_mm,
                                               params.raw_max_out_mm);
    result.point_set = polarToPointSet(result.radius_profile, params.angle_offset_deg);
    result.circle_fit = fitCircleRobust(result.point_set, params.outer_fit);
    result.harmonics = analyzeHarmonics(result.radius_profile, params.harmonic);
    result.success = result.circle_fit.success;
    if (!result.success) {
        result.error = result.circle_fit.error;
    }
    return result;
}

ThicknessResult computeThickness(const QVector<double> &raw_inner_values_mm,
                                 const QVector<bool> &raw_inner_valid_mask,
                                 const QVector<double> &raw_outer_values_mm,
                                 const QVector<bool> &raw_outer_valid_mask,
                                 double probe_base_mm)
{
    ThicknessResult result;
    const int N = std::min(raw_inner_values_mm.size(), raw_outer_values_mm.size());
    result.thickness_profile = makeAngularSeries(N, N > 0 ? 360.0 / static_cast<double>(N) : 0.0);
    result.thickness_profile.values.resize(N);
    result.thickness_profile.valid_mask.resize(N);

    QVector<double> vals;
    vals.reserve(N);
    for (int i = 0; i < N; ++i) {
        const bool vin = (i < raw_inner_valid_mask.size()) ? raw_inner_valid_mask.at(i) : true;
        const bool vout = (i < raw_outer_valid_mask.size()) ? raw_outer_valid_mask.at(i) : true;
        const double in = raw_inner_values_mm.at(i);
        const double out = raw_outer_values_mm.at(i);
        const bool valid = vin && vout && isFinite(in) && isFinite(out);
        result.thickness_profile.valid_mask[i] = valid;
        const double t = probe_base_mm - in - out;
        result.thickness_profile.values[i] = t;
        if (valid) vals.push_back(t);
    }

    result.valid_count = vals.size();
    if (vals.isEmpty()) {
        result.error = QStringLiteral("壁厚计算有效点不足");
        return result;
    }

    double sum = 0.0;
    result.min_mm = vals.front();
    result.max_mm = vals.front();
    for (double v : vals) {
        sum += v;
        result.min_mm = std::min(result.min_mm, v);
        result.max_mm = std::max(result.max_mm, v);
    }
    result.mean_mm = sum / static_cast<double>(vals.size());
    double var = 0.0;
    for (double v : vals) {
        const double dv = v - result.mean_mm;
        var += dv * dv;
    }
    result.stddev_mm = std::sqrt(var / static_cast<double>(vals.size()));
    result.success = true;
    return result;
}

ThicknessResult computeThickness(const QVector<double> &raw_inner_values_mm,
                                 const QVector<bool> &raw_inner_valid_mask,
                                 const QVector<double> &raw_outer_values_mm,
                                 const QVector<bool> &raw_outer_valid_mask,
                                 const DiameterAlgoParams &params)
{
    return computeThickness(raw_inner_values_mm,
                            raw_inner_valid_mask,
                            raw_outer_values_mm,
                            raw_outer_valid_mask,
                            effectiveProbeBase(params));
}

EndSectionResult computeEndSectionGeometry(const QVector<double> &raw_inner_values_mm,
                                           const QVector<bool> &raw_inner_valid_mask,
                                           const QVector<double> &raw_outer_values_mm,
                                           const QVector<bool> &raw_outer_valid_mask,
                                           const DiameterAlgoParams &params)
{
    EndSectionResult result;
    result.inner_diameter = computeInnerDiameter(raw_inner_values_mm, raw_inner_valid_mask, params);
    result.outer_diameter = computeOuterDiameter(raw_outer_values_mm, raw_outer_valid_mask, params);
    result.thickness = computeThickness(raw_inner_values_mm,
                                        raw_inner_valid_mask,
                                        raw_outer_values_mm,
                                        raw_outer_valid_mask,
                                        params);
    return result;
}

ATypeGeometryResult computeATypeGeometry(const QVector<double> &left_inner_raw_mm,
                                         const QVector<bool> &left_inner_valid_mask,
                                         const QVector<double> &left_outer_raw_mm,
                                         const QVector<bool> &left_outer_valid_mask,
                                         const QVector<double> &right_inner_raw_mm,
                                         const QVector<bool> &right_inner_valid_mask,
                                         const QVector<double> &right_outer_raw_mm,
                                         const QVector<bool> &right_outer_valid_mask,
                                         const DiameterAlgoParams &left_params,
                                         const DiameterAlgoParams &right_params)
{
    ATypeGeometryResult result;
    result.left_end = computeEndSectionGeometry(left_inner_raw_mm,
                                                left_inner_valid_mask,
                                                left_outer_raw_mm,
                                                left_outer_valid_mask,
                                                left_params);
    result.right_end = computeEndSectionGeometry(right_inner_raw_mm,
                                                 right_inner_valid_mask,
                                                 right_outer_raw_mm,
                                                 right_outer_valid_mask,
                                                 right_params);
    return result;
}

} // namespace core
