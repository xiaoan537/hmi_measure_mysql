#include "core/raw_writer.hpp"

#include <QFile>
#include <QDir>
#include <QDataStream>
#include <QByteArray>
#include <QtGlobal>

#include <limits>

#include <unistd.h> // fsync
#include <fcntl.h>  // open
#include <sys/stat.h>
#include <sys/types.h>

namespace core
{

    // 简单 CRC32（payload用）
    static quint32 crc32_update(quint32 crc, const unsigned char *data, size_t len)
    {
        crc = ~crc;
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= data[i];
            for (int k = 0; k < 8; ++k)
            {
                crc = (crc >> 1) ^ (0xEDB88320u & (-(int)(crc & 1u)));
            }
        }
        return ~crc;
    }

    static bool fsync_dir(const QString &dirPath)
    {
        // 确保 rename 的元数据也尽量落盘（提高断电可靠性）
        int fd = ::open(dirPath.toUtf8().constData(), O_DIRECTORY | O_RDONLY);
        if (fd < 0)
            return false;
        ::fsync(fd);
        ::close(fd);
        return true;
    }

    bool writeRawV1_ConfocalOnly(
        const QString &raw_dir,
        const QString &measurement_uuid,
        const QDateTime &measured_at_utc,
        QChar part_type,
        const QVector<float> &confocal,
        RawWriteInfo *out,
        QString *err)
    {
        if (measurement_uuid.size() != 36)
        {
            if (err)
                *err = "measurement_uuid must be 36 chars (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).";
            return false;
        }
        constexpr int kCh = 4, kRings = 16, kPts = 72;
        if (confocal.size() != kCh * kRings * kPts)
        {
            if (err)
                *err = QString("confocal size mismatch: expect %1, got %2")
                           .arg(kCh * kRings * kPts)
                           .arg(confocal.size());
            return false;
        }

        QDir().mkpath(raw_dir);

        const QString tmpPath = raw_dir + "/" + measurement_uuid + ".tmp";
        const QString finalPath = raw_dir + "/" + measurement_uuid + ".rawbin";

        // 计算 payload crc32（float 原始字节）
        const unsigned char *payloadBytes = reinterpret_cast<const unsigned char *>(confocal.constData());
        const size_t payloadLen = static_cast<size_t>(confocal.size()) * sizeof(float);
        const quint32 crc = crc32_update(0u, payloadBytes, payloadLen);

        QFile f(tmpPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            if (err)
                *err = "open tmp file failed: " + f.errorString();
            return false;
        }

        QDataStream ds(&f);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setFloatingPointPrecision(QDataStream::SinglePrecision);

        // --- header (256 bytes) ---
        const QByteArray magic("HMIRAW01", 8);
        ds.writeRawData(magic.constData(), 8);

        ds << quint16(1);   // version
        ds << quint16(256); // header_size
        ds << quint32(1);   // endian: 1=LE

        // uuid[36] ASCII
        ds.writeRawData(measurement_uuid.toUtf8().constData(), 36);

        // measured_at_utc_ms
        const quint64 ms = static_cast<quint64>(measured_at_utc.toMSecsSinceEpoch());
        ds << ms;

        ds << quint8(part_type.toLatin1()); // 'A'/'B'
        ds << quint8(kCh);
        ds << quint16(kRings);
        ds << quint16(kPts);
        ds << float(5.0f); // angle_step_deg
        ds << quint32(0);  // payload_layout: 0 = ch-major

        // padding to 256
        const qint64 headerWritten = f.pos();
        if (headerWritten > 256)
        {
            f.close();
            if (err)
                *err = "header overflow (>256), bug in writer.";
            return false;
        }
        if (headerWritten < 256)
        {
            f.write(QByteArray(static_cast<int>(256 - headerWritten), '\0'));
        }

        // --- payload ---
        const qint64 payloadWritten = f.write(reinterpret_cast<const char *>(confocal.constData()),
                                              static_cast<qint64>(payloadLen));
        if (payloadWritten != (qint64)payloadLen)
        {
            f.close();
            if (err)
                *err = "write payload failed: " + f.errorString();
            return false;
        }

        f.flush();
        ::fsync(f.handle());
        f.close();

        // rename 原子提交
        QFile::remove(finalPath); // 开发阶段允许覆盖；正式版会更严谨
        if (!QFile::rename(tmpPath, finalPath))
        {
            if (err)
                *err = "rename tmp->final failed (same filesystem required).";
            QFile::remove(tmpPath);
            return false;
        }
        fsync_dir(raw_dir);

        if (out)
        {
            out->final_path = finalPath;
            out->file_size_bytes = QFileInfo(finalPath).size();
            out->crc32_payload = crc;
            out->format_version = 1;
            out->confocal_channels = kCh;
            out->rings = kRings;
            out->points_per_ring = kPts;
            out->angle_step_deg = 5.0f;
        }
        return true;
    }

} // namespace core
