#pragma once
#include <QString>
#include <QDateTime>
#include <QVector>

namespace core
{

    struct MeasurementSnapshot
    {
        QString measurement_uuid; // 36 chars
        QChar part_type;          // 'A' or 'B'
        QDateTime measured_at_utc;

        // 原始点阵（单位：μm，允许 NaN）
        QVector<float> confocal4; // A型：4*16*72，可选
        QVector<float> runout2;   // B型：2*16*72，可选

        // PLC给出的长度结果（单位：mm）
        QVector<float> gt2r_mm3; // size=3（见语义说明）

        // META JSON（条码、seq、槽位、任务号、采样参数、软件版本等）
        QString meta_json;
    };

} // namespace core
