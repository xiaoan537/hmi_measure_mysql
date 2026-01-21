#pragma once
#include <QString>
#include <QDateTime>
#include <QVector>

namespace core
{

    struct RawWriteInfo
    {
        QString final_path;
        quint64 file_size_bytes = 0;
        quint64 crc32_payload = 0;

        int format_version = 1;
        int confocal_channels = 4;
        int rings = 16;
        int points_per_ring = 72;
        float angle_step_deg = 5.0f;
    };

    bool writeRawV1_ConfocalOnly(
        const QString &raw_dir,
        const QString &measurement_uuid,
        const QDateTime &measured_at_utc,
        QChar part_type,
        const QVector<float> &confocal, // size = 4*16*72
        RawWriteInfo *out,
        QString *err);

} // namespace core
