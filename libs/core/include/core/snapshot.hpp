#pragma once
#include <QString>
#include <QDateTime>
#include <QVector>

namespace core
{

    // 点阵扫描参数（由上位机配置，与 PLC 约定一致）
    // 说明：RAW 文件的 MATRIX chunk payload 头里也会存 rings/points/angle。
    // order_code 用于消除历史版本“线性展开顺序”的歧义：
    //   0 = legacy: ch -> ring -> pt（旧测试代码曾用）
    //   1 = ring -> ch -> pt（当前与 PLC 约定：先第1圈4通道，再第2圈4通道）
    struct ScanSpec
    {
        int rings = 1;               // 圈数（例如 1，后续可能 2）
        int points_per_ring = 72;    // 每圈点数（例如 72，后续可能变）
        float angle_step_deg = 5.0f; // 角度步进（例如 5.0）

        // 线性展开顺序：默认 ring->ch->pt（与 PLC 约定一致）
        quint16 order_code = 1;
    };

    struct MeasurementSnapshot
    {
        QString measurement_uuid; // 36 chars
        QChar part_type;          // 'A' or 'B'
        QDateTime measured_at_utc;

        // 原始点阵（单位：μm，允许 NaN）
        // 线性展开顺序由对应的 spec.order_code 定义。
        QVector<float> confocal4; // A型：4 * spec.rings * spec.points_per_ring（可选）
        QVector<float> runout2;   // B型：2 * spec.rings * spec.points_per_ring（可选）

        // 点阵参数（由上位机配置；PLC 侧 Arrays 写入顺序需一致）
        ScanSpec conf_spec; // 对应 confocal4
        ScanSpec run_spec;  // 对应 runout2

        // PLC给出的长度结果（单位：mm）
        QVector<float> gt2r_mm3; // size=3（见语义说明）

        // META JSON（条码、seq、槽位、任务号、采样参数、软件版本等）
        QString meta_json;
    };

} // namespace core
