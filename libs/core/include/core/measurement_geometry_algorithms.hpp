#pragma once

#include <QVector>
#include <QString>

namespace core {

struct AngularSeries
{
    QVector<double> angles_deg;
    QVector<double> values;
    QVector<bool> valid_mask;
};

struct Point2D
{
    double x = 0.0;
    double y = 0.0;
    bool valid = false;
};

struct PointSet2D
{
    QVector<Point2D> points;
    QVector<double> angles_deg;
};

struct CircleFitOptions
{
    double residual_threshold_mm = 0.03;
    // 最小有效点默认给低门槛（仅保证可拟合），业务门槛由上层流程控制
    int min_valid_points = 3;
    bool enable_second_pass = true;
    int max_geometric_iterations = 20;
    double geometric_tolerance = 1e-9;
};

struct CircleFitResult
{
    bool success = false;

    double center_x_mm = 0.0;
    double center_y_mm = 0.0;
    double radius_mm = 0.0;
    double diameter_mm = 0.0;

    double residual_rms_mm = 0.0;
    double residual_max_abs_mm = 0.0;

    int valid_count_raw = 0;
    int valid_count_final = 0;
    int rejected_count = 0;

    QVector<double> residuals_mm;
    QVector<bool> final_mask;
    QString error;
};

struct HarmonicAnalysisOptions
{
    int max_order = 8;
    bool remove_mean = false;
};

struct HarmonicComponent
{
    int order = 0;
    double amplitude_mm = 0.0;
    double phase_rad = 0.0;
};

struct HarmonicAnalysisResult
{
    bool success = false;
    double dc_mean_mm = 0.0;
    QVector<HarmonicComponent> components;
    QString error;
};

struct DiameterAlgoParams
{
    double angle_offset_deg = 0.0;

    double k_in_mm = 0.0;
    double k_out_mm = 0.0;
    bool use_explicit_k_out = true;
    double probe_base_mm = 15.0;  // 辅助/校验参数：名义探头基距L

    double raw_min_in_mm = -1e9;
    double raw_max_in_mm =  1e9;
    double raw_min_out_mm = -1e9;
    double raw_max_out_mm =  1e9;

    CircleFitOptions inner_fit;
    CircleFitOptions outer_fit;
    HarmonicAnalysisOptions harmonic;
};

struct DiameterChannelResult
{
    bool success = false;

    AngularSeries raw_series;
    AngularSeries radius_profile;
    PointSet2D point_set;
    CircleFitResult circle_fit;
    HarmonicAnalysisResult harmonics;

    QString error;
};


struct RunoutAlgoParams
{
    double angle_offset_deg = 0.0;
    double k_runout_mm = 0.0;
    double raw_min_mm = -1e9;
    double raw_max_mm = 1e9;
    int interpolation_factor = 5;
    double v_block_angle_deg = 90.0;

    CircleFitOptions fit_options;
    HarmonicAnalysisOptions harmonic;
};

struct RunoutResult
{
    bool success = false;

    AngularSeries raw_series;
    AngularSeries radius_profile;
    PointSet2D point_set;
    PointSet2D dense_point_set;

    double tir_axis_mm = 0.0;
    double max_radius_mm = 0.0;
    double min_radius_mm = 0.0;
    double max_angle_deg = 0.0;
    double min_angle_deg = 0.0;

    double runout_vblock_mm = 0.0;
    double vblock_max_reading_mm = 0.0;
    double vblock_min_reading_mm = 0.0;

    double fit_residual_peak_to_peak_mm = 0.0;
    double fit_residual_rms_mm = 0.0;

    CircleFitResult circle_fit;
    HarmonicAnalysisResult harmonics;
    QString error;
};

struct ThicknessResult
{
    bool success = false;

    AngularSeries thickness_profile;
    double mean_mm = 0.0;
    double min_mm = 0.0;
    double max_mm = 0.0;
    double stddev_mm = 0.0;

    int valid_count = 0;
    QString error;
};

struct EndSectionResult
{
    DiameterChannelResult inner_diameter;
    DiameterChannelResult outer_diameter;
    ThicknessResult thickness;

    QString error;
};

struct ATypeGeometryResult
{
    EndSectionResult left_end;
    EndSectionResult right_end;
    QString error;
};

AngularSeries makeAngularSeries(int pointCount, double stepDeg);

AngularSeries buildRadiusProfile(const QVector<double> &angles_deg,
                                 const QVector<double> &raw_values,
                                 const QVector<bool> &raw_valid_mask,
                                 double offset_mm,
                                 double sign,
                                 double raw_min_mm,
                                 double raw_max_mm);

PointSet2D polarToPointSet(const AngularSeries &radiusProfile,
                           double angleOffsetDeg = 0.0);

CircleFitResult fitCircleRobust(const PointSet2D &pointSet,
                                const CircleFitOptions &options);

HarmonicAnalysisResult analyzeHarmonics(const AngularSeries &series,
                                        const HarmonicAnalysisOptions &options);

DiameterChannelResult computeInnerDiameter(const QVector<double> &raw_inner_values_mm,
                                           const QVector<bool> &raw_inner_valid_mask,
                                           const DiameterAlgoParams &params);

DiameterChannelResult computeOuterDiameter(const QVector<double> &raw_outer_values_mm,
                                           const QVector<bool> &raw_outer_valid_mask,
                                           const DiameterAlgoParams &params);


RunoutResult computeRunoutAnalysis(const QVector<double> &raw_runout_values_mm,
                                   const QVector<bool> &raw_runout_valid_mask,
                                   const RunoutAlgoParams &params);

ThicknessResult computeThickness(const QVector<double> &raw_inner_values_mm,
                                 const QVector<bool> &raw_inner_valid_mask,
                                 const QVector<double> &raw_outer_values_mm,
                                 const QVector<bool> &raw_outer_valid_mask,
                                 double probe_base_mm);

ThicknessResult computeThickness(const QVector<double> &raw_inner_values_mm,
                                 const QVector<bool> &raw_inner_valid_mask,
                                 const QVector<double> &raw_outer_values_mm,
                                 const QVector<bool> &raw_outer_valid_mask,
                                 const DiameterAlgoParams &params);

EndSectionResult computeEndSectionGeometry(const QVector<double> &raw_inner_values_mm,
                                           const QVector<bool> &raw_inner_valid_mask,
                                           const QVector<double> &raw_outer_values_mm,
                                           const QVector<bool> &raw_outer_valid_mask,
                                           const DiameterAlgoParams &params);

ATypeGeometryResult computeATypeGeometry(const QVector<double> &left_inner_raw_mm,
                                         const QVector<bool> &left_inner_valid_mask,
                                         const QVector<double> &left_outer_raw_mm,
                                         const QVector<bool> &left_outer_valid_mask,
                                         const QVector<double> &right_inner_raw_mm,
                                         const QVector<bool> &right_inner_valid_mask,
                                         const QVector<double> &right_outer_raw_mm,
                                         const QVector<bool> &right_outer_valid_mask,
                                         const DiameterAlgoParams &left_params,
                                         const DiameterAlgoParams &right_params);

} // namespace core
