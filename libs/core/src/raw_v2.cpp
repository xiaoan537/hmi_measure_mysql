#include "core/raw_v2.hpp"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDataStream>
#include <QBuffer>
#include <QByteArray>
#include <QtGlobal>

#include <limits>
#include <unistd.h>
#include <fcntl.h>

namespace core
{

    /*
    算法步骤详解：
        初始化CRC值：crc = ~crc - 对输入CRC值取反，这是CRC32算法的标准初始化步骤

        逐字节处理数据：遍历数据缓冲区中的每个字节
            crc ^= data[i] - 将当前字节与CRC值进行异或操作

        逐位处理（8次循环）：对每个字节的8个位进行处理
            crc & 1u - 获取CRC的最低位
            -(int)(crc & 1u) - 将最低位转换为-1（如果为1）或0（如果为0）
            0xEDB88320u & (-(int)(crc & 1u)) - 使用CRC32多项式与条件值进行与操作
            crc = (crc >> 1) ^ ... - 将CRC右移1位并与多项式进行异或

        最终处理：return ~crc - 对最终结果取反，返回标准的CRC32校验和
    */
    // ---------- CRC32 ----------
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

    /*
    其中::open()是一个系统调用（仅适用于类Unix系统），用于打开目录并返回文件描述符。
    ::fsync()是另一个系统调用，用于将文件描述符关联的文件数据强制写入物理存储设备，确保数据持久化。
    ::close（）用于关闭文件描述符，释放系统资源。
    */
    // 用于确保目录数据真正写入物理存储设备的函数，防止数据丢失
    static bool fsync_dir(const QString &dirPath)
    {
        int fd = ::open(dirPath.toUtf8().constData(), O_DIRECTORY | O_RDONLY);
        if (fd < 0)
            return false;
        ::fsync(fd);
        ::close(fd);
        return true;
    }

    /*
    具体作用：
        参数接收：接收一个QString类型的JSON字符串jsonUtf8
        编码转换：调用jsonUtf8.toUtf8()将QString转换为UTF-8编码的QByteArray
        返回结果：返回转换后的字节数组
    */
    // 元数据块载荷生成函数，用于将JSON格式的元数据字符串转换为UTF-8编码的字节数组，以便写入原始数据文件。
    // 将元数据（meta_json）转换为UTF-8编码的字节数组，用于写入原始数据文件。
    static QByteArray makeChunkPayload_META(const QString &jsonUtf8)
    {
        return jsonUtf8.toUtf8();
    }

    /*
    1. 内存预留
        预留16字节头部空间 + 数据数组大小（每个浮点数4字节）
        避免多次内存分配，提高性能
    2. 创建缓冲区
        使用QBuffer作为内存缓冲区，避免文件I/O
        以只写模式打开
    3. 数据流配置
        设置为小端序（兼容大多数现代处理器）
        设置浮点精度为单精度（32位）
    4. 写入头部信息
        按顺序写入各个头部字段
        包括通道数、单位代码、环数、点数、角度步长和保留字段
    5. 写入测量数据
        遍历所有测量数据点，逐个写入缓冲区
        每个浮点数占4字节
    */
    // 矩阵数据块载荷生成函数，用于将矩阵数据转换为原始数据文件的字节数组。
    // 一个测量点阵数据打包函数，用于将测量数据转换为二进制格式以便存储
    static QByteArray makeChunkPayload_MATRIX(
        quint8 channels, quint16 rings, quint16 points, float angle_step_deg,
        quint8 unit_code, // 0=um
        const QVector<float> &data)
    {
        // payload = [channels u8][unit u8][rings u16][points u16][angle_step f32][reserved u16] + float32[]
        QByteArray payload;
        payload.reserve(16 + data.size() * 4); // 预留空间：16字节头部 + 数据数组大小，每个数据点4字节
        QBuffer buf(&payload);
        buf.open(QIODevice::WriteOnly); // 打开缓冲区以进行写操作

        QDataStream ds(&buf);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setFloatingPointPrecision(QDataStream::SinglePrecision);

        ds << quint8(channels);
        ds << quint8(unit_code);
        ds << quint16(rings);
        ds << quint16(points);
        ds << float(angle_step_deg);
        ds << quint16(0); // reserved

        for (float v : data)
            ds << float(v);

        buf.close();
        return payload;
    }

    /*
    这里要确认一下，是如何确定v3中到底有几个元素的：在main函数中，对v3进行赋值，
    通过赋了几个值，再通过v3.size()从而确定v3的大小。
    */
    // 总长度数据块载荷生成函数，用于将长度数据转换为原始数据文件的字节数组。
    static QByteArray makeChunkPayload_GT2R(quint8 unit_code, const QVector<float> &v3)
    {
        // payload = [unit u8][count u8][reserved u16] + float32[count]
        QByteArray payload;
        QBuffer buf(&payload);
        buf.open(QIODevice::WriteOnly);

        QDataStream ds(&buf);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setFloatingPointPrecision(QDataStream::SinglePrecision);

        ds << quint8(unit_code); // 1=mm
        ds << quint8(v3.size()); // should be 3
        ds << quint16(0);        // reserved
        for (float v : v3)
            ds << float(v);

        buf.close();
        return payload;
    }

    // 写入数据块函数，用于将数据块写入原始数据文件。
    static bool writeChunk(QDataStream &out, const char type4[4], quint16 chunk_version,
                           quint16 flags, const QByteArray &payload, quint32 *file_crc)
    {
        // chunk header: type[4], ver u16, flags u16, len u32, crc32 u32
        const quint32 len = (quint32)payload.size();
        const quint32 crc = crc32_update(0u,
                                         reinterpret_cast<const unsigned char *>(payload.constData()),
                                         payload.size());

        out.writeRawData(type4, 4);    // 字符格式化写入
        out << quint16(chunk_version); // 小端序写入
        out << quint16(flags);
        out << quint32(len);
        out << quint32(crc);
        out.writeRawData(payload.constData(), payload.size());

        if (file_crc)
        {
            // 把chunk header + payload都纳入文件crc
            // 注意：这里为了简单，复算一次字节串
            QByteArray hdr;
            hdr.resize(16);
            memcpy(hdr.data(), type4, 4); // 将4字符的类型标识(如"META"、"CONF")复制到头部数组
            {
                QBuffer b(&hdr);
                b.open(QIODevice::WriteOnly);
                b.seek(4); // 跳过type部分（前4字节），直接从第5字节开始写
                QDataStream ds(&b);
                ds.setByteOrder(QDataStream::LittleEndian);
                ds << quint16(chunk_version) << quint16(flags) << quint32(len) << quint32(crc);
                b.close();
            }
            /*
            不是覆盖：每次调用crc32_update都是在现有CRC基础上继续计算
            累积过程：文件级CRC是所有数据块按顺序累积计算的结果
            数学特性：CRC算法支持这种增量计算方式
            最终结果：*file_crc最终包含整个文件（除文件头外）的CRC值
            这种设计使得：

            可以边写入边计算CRC，无需等待整个文件完成
            支持大文件处理，不需要将整个文件加载到内存
            提供了完整的数据完整性验证机制
            */
            //
            *file_crc = crc32_update(*file_crc, (const unsigned char *)hdr.constData(), hdr.size());
            *file_crc = crc32_update(*file_crc, (const unsigned char *)payload.constData(), payload.size());
        }
        return true;
    }

    bool writeRawV2(const QString &raw_dir, const MeasurementSnapshot &s, RawWriteInfoV2 *out, QString *err)
    {
        if (s.measurement_uuid.size() != 36)
        {
            if (err)
                *err = "measurement_uuid must be 36 chars.";
            return false;
        }
        if (s.part_type != 'A' && s.part_type != 'B')
        {
            if (err)
                *err = "part_type must be 'A' or 'B'.";
            return false;
        }
        if (s.gt2r_mm3.size() != 3)
        {
            if (err)
                *err = "gt2r_mm3 must have size=3 (total, bc, reserved).";
            return false;
        }

        // A: CONF 4*16*72（可选但通常有）
        // B: RUNO 2*16*72（可选但通常有）
        // lambda表达式，[]表示不获取外部任何变量
        auto expectSize = [](int ch)
        { return ch * 16 * 72; };
        /*
        条件判断设计：!s.confocal4.isEmpty()确保只有当数据存在时才检查大小
        灵活性：支持A型、B型或A+B型工件
        严格性：只要数据存在，就必须符合预期格式
        一致性：验证逻辑与写入逻辑保持一致
        这种设计模式在处理可选数据时非常常见，既保证了数据完整性，又提供了必要的灵活性。
        这种条件判断确实容易让人误解为"必须同时满足"，但实际上是"分别检查各自的存在性和有效性"。
        */
        // 条件判断设计：!s.confocal4.isEmpty()确保只有当数据存在时才检查大小,否则直接跳出if判断
        if (!s.confocal4.isEmpty() && s.confocal4.size() != expectSize(4))
        {
            if (err)
                *err = "confocal4 size mismatch: expect 4*16*72.";
            return false;
        }
        if (!s.runout2.isEmpty() && s.runout2.size() != expectSize(2))
        {
            if (err)
                *err = "runout2 size mismatch: expect 2*16*72.";
            return false;
        }

        // Qt框架中用于创建目录路径的常用方法。它的核心作用是一次性创建指定的完整目录结构，包括所有必要的父级目录。
        QDir().mkpath(raw_dir);
        const QString tmpPath = raw_dir + "/" + s.measurement_uuid + ".tmp";
        const QString finalPath = raw_dir + "/" + s.measurement_uuid + ".rawbin";

        /*
        WriteOnly | Truncate是一个“强硬”的组合，它会无条件清空目标文件的现有内容。
        如果你希望保留原内容并在末尾添加新内容，应使用 QIODevice::WriteOnly | QIODevice::Append模式。
        析构函数会自动调用 close()关闭文件
        */
        // QFile类提供了一组用于文件操作的函数，包括打开、关闭、读取和写入。
        QFile f(tmpPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            if (err)
                *err = "open tmp failed: " + f.errorString();
            return false;
        }

        QDataStream ds(&f);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setFloatingPointPrecision(QDataStream::SinglePrecision);

        quint32 file_crc = 0u;

        // ---- file header (64 bytes) ----
        const QByteArray magic("HMIRAW02", 8);
        ds.writeRawData(magic.constData(), 8);
        ds << quint16(64); // header_size
        ds << quint16(2);  // version
        ds << quint32(1);  // endian=LE
        ds.writeRawData(s.measurement_uuid.toUtf8().constData(), 36);
        ds << quint64(s.measured_at_utc.toMSecsSinceEpoch()); // 获取毫秒级时间戳
        ds << quint8(s.part_type.toLatin1());                 // 用于将Unicode字符串转换为Latin-1编码（ISO-8859-1）的字节数组（QByteArray）
        // pad to 64
        // QFile::pos() 方法用于获取文件的当前读写位置（即文件指针的位置）。
        // 它返回一个 qint64 类型的值，表示从文件开头到当前位置的字节数。
        const qint64 pos = f.pos();
        if (pos < 64)
            f.write(QByteArray(int(64 - pos), '\0'));

        // 文件crc把header也算进去（可选）
        {
            // 强制写入磁盘：确保所有数据（包括刚填充的空字节）从内存缓冲区写入物理存储，防止后续操作读取到旧数据
            f.flush();
            QFile fh(tmpPath);
            // 不重复打开读，直接用已有内容：简单起见，先不把header算进crc也行
        }

        // ---- chunks ----
        RawWriteInfoV2 info;
        info.final_path = finalPath;
        info.format_version = 2;
        info.meta_json = s.meta_json;

        // META
        if (!s.meta_json.trimmed().isEmpty())
        {
            const QByteArray payload = makeChunkPayload_META(s.meta_json);
            const char t[4] = {'M', 'E', 'T', 'A'};
            writeChunk(ds, t, 1, 0, payload, &file_crc);
            info.chunk_mask |= CHUNK_META;
        }

        // CONF（A型直径点阵）
        if (!s.confocal4.isEmpty())
        {
            const QByteArray payload = makeChunkPayload_MATRIX(
                4, 16, 72, 5.0f, 0, s.confocal4);
            const char t[4] = {'C', 'O', 'N', 'F'};
            writeChunk(ds, t, 1, 0, payload, &file_crc);
            info.chunk_mask |= CHUNK_CONF;

            // 主扫描优先用 CONF
            if (info.scan_kind.isEmpty())
            {
                info.scan_kind = "CONF";
                info.main_channels = 4;
                info.rings = 16;
                info.points_per_ring = 72;
                info.angle_step_deg = 5.0f;
            }
        }

        // RUNO（B型跳动点阵）
        if (!s.runout2.isEmpty())
        {
            const QByteArray payload = makeChunkPayload_MATRIX(
                2, 16, 72, 5.0f, 0, s.runout2);
            const char t[4] = {'R', 'U', 'N', 'O'};
            writeChunk(ds, t, 1, 0, payload, &file_crc);
            info.chunk_mask |= CHUNK_RUNO;

            // 如果没有 CONF，则 RUNO 为主扫描
            if (info.scan_kind.isEmpty())
            {
                info.scan_kind = "RUNO";
                info.main_channels = 2;
                info.rings = 16;
                info.points_per_ring = 72;
                info.angle_step_deg = 5.0f;
            }
        }

        // GT2R（PLC给的长度结果，mm）
        {
            const QByteArray payload = makeChunkPayload_GT2R(1, s.gt2r_mm3); // 1=mm
            const char t[4] = {'G', 'T', '2', 'R'};
            writeChunk(ds, t, 1, 0, payload, &file_crc);
            info.chunk_mask |= CHUNK_GT2R;
        }

        /*
        种阻塞可能会对系统性能产生以下影响：

        UI响应延迟：如果这是在主线程中执行，可能会导致UI短暂冻结
        吞吐量降低：频繁的fsync会降低整体写入性能
        实时性影响：对于需要高实时性的应用，这种延迟可能是不可接受的
        */
        f.flush();
        ::fsync(f.handle()); // 一个系统调用，它要求内核将指定文件的所有已修改数据（以及文件的元数据，如最后修改时间）立即、强制地写入物理磁盘，并且该调用会阻塞直到磁盘控制器确认写入操作已完成
        f.close();           // 这里在写入数据时，如果时间比较长的话，可以加入一个进度条，提示用户等待

        QFile::remove(finalPath);               // 如果已存在同名finalPath，先删除它
        if (!QFile::rename(tmpPath, finalPath)) // 尝试将临时文件重命名为最终文件名
        {
            if (err)
                *err = "rename tmp->final failed (must be same filesystem).";
            QFile::remove(tmpPath);
            return false;
        }
        fsync_dir(raw_dir); // 确保目录项变更写入磁盘

        info.file_size_bytes = QFileInfo(finalPath).size();
        info.file_crc32 = file_crc;

        if (out)
            *out = info;
        return true;
    }

    /*
    循环读取：只要已读取的字节数 got小于目标 n，就继续尝试读取。
    指针偏移：dev.read(buf + got, n - got)确保每次读取的数据都紧接着存放在缓冲区 buf中已读数据之后，不会覆盖。
            buf：缓冲区的起点
            定义：buf是一个 char*类型的指针。在函数声明 char *buf中，它表示一个指向字符（即字节）的指针。
            作用：它的作用是记录一段内存空间的起始地址。
            指针运算：在 C/C++ 中，对指针进行加/减操作（如 ptr + 1）并不是简单地将地址值加 1。它的实际偏移量是 1 * sizeof(指针所指向的类型)​ 个字节。
            因为 buf是 char*类型，而 sizeof(char)恒为 1 字节，所以 buf + got就相当于从 buf的地址向后移动 got个字节 。
    错误处理：当 read()返回值 r <= 0时，表示读取出错或已无更多数据，函数立即返回 false。
    可靠性：只有当 got最终等于 n时，函数才返回 true，保证了数据的完整性。
    */
    // 辅助函数，确保从设备中精确读取指定字节数的数据。
    static bool readExactly(QIODevice &dev, char *buf, int n)
    {
        int got = 0;
        while (got < n)
        {
            const int r = dev.read(buf + got, n - got);
            if (r <= 0)
                return false;
            got += r;
        }
        return true;
    }

    bool readRawV2(const QString &file_path, MeasurementSnapshot *out, QString *err)
    {
        QFile f(file_path);
        if (!f.open(QIODevice::ReadOnly))
        {
            if (err)
                *err = "open failed: " + f.errorString();
            return false;
        }

        QDataStream ds(&f);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setFloatingPointPrecision(QDataStream::SinglePrecision);

        char magic[8];
        if (!readExactly(f, magic, 8))
        {
            if (err)
                *err = "read magic failed";
            return false;
        }
        if (memcmp(magic, "HMIRAW02", 8) != 0)
        {
            if (err)
                *err = "not HMIRAW02";
            return false;
        }

        quint16 header_size = 0, version = 0;
        quint32 endian = 0;
        ds >> header_size >> version >> endian;
        if (version != 2 || header_size != 64 || endian != 1)
        {
            if (err)
                *err = "unsupported header fields";
            return false;
        }

        char uuid36[36];
        if (!readExactly(f, uuid36, 36))
        {
            if (err)
                *err = "read uuid failed";
            return false;
        }

        quint64 ms = 0;
        quint8 part = 0;
        ds >> ms >> part;

        // skip padding to 64
        const qint64 needSkip = 64 - f.pos();
        if (needSkip > 0)
            f.read(needSkip);

        MeasurementSnapshot s;
        s.measurement_uuid = QString::fromUtf8(QByteArray(uuid36, 36));
        s.measured_at_utc = QDateTime::fromMSecsSinceEpoch((qint64)ms, Qt::UTC);
        s.part_type = QChar(char(part));

        // read chunks until EOF
        while (!f.atEnd())
        {
            char type4[4];
            if (!readExactly(f, type4, 4))
                break;

            quint16 cver = 0, flags = 0;
            quint32 len = 0, crc = 0;
            ds >> cver >> flags >> len >> crc;

            QByteArray payload;
            payload.resize((int)len);
            if (!readExactly(f, payload.data(), (int)len))
            {
                if (err)
                    *err = "read chunk payload failed";
                return false;
            }

            const quint32 gotCrc = crc32_update(0u,
                                                reinterpret_cast<const unsigned char *>(payload.constData()),
                                                payload.size());
            if (gotCrc != crc)
            {
                if (err)
                    *err = "chunk crc mismatch";
                return false;
            }

            const QByteArray t(type4, 4);

            if (t == "META")
            {
                s.meta_json = QString::fromUtf8(payload);
                continue;
            }

            if (t == "GT2R")
            {
                QBuffer buf(&payload);
                buf.open(QIODevice::ReadOnly);
                QDataStream pd(&buf);
                pd.setByteOrder(QDataStream::LittleEndian);
                pd.setFloatingPointPrecision(QDataStream::SinglePrecision);

                quint8 unit = 0, count = 0;
                quint16 rsv = 0;
                pd >> unit >> count >> rsv;
                if (unit != 1 || count != 3)
                {
                    if (err)
                        *err = "GT2R payload unexpected (unit/count)";
                    return false;
                }
                s.gt2r_mm3.clear();
                s.gt2r_mm3.reserve(3);
                for (int i = 0; i < 3; i++)
                {
                    float v;
                    pd >> v;
                    s.gt2r_mm3.push_back(v);
                }
                continue;
            }

            if (t == "CONF" || t == "RUNO")
            {
                QBuffer buf(&payload);
                buf.open(QIODevice::ReadOnly);
                QDataStream pd(&buf);
                pd.setByteOrder(QDataStream::LittleEndian);
                pd.setFloatingPointPrecision(QDataStream::SinglePrecision);

                quint8 channels = 0, unit = 0;
                quint16 rings = 0, points = 0;
                float angle = 0.0f;
                quint16 rsv = 0;

                pd >> channels >> unit >> rings >> points >> angle >> rsv;
                const int n = int(channels) * int(rings) * int(points);

                QVector<float> arr;
                arr.reserve(n);
                for (int i = 0; i < n; i++)
                {
                    float v;
                    pd >> v;
                    arr.push_back(v);
                }

                if (t == "CONF")
                    s.confocal4 = arr;
                else
                    s.runout2 = arr;

                continue;
            }

            // unknown chunk: skip (already read)
        }

        if (s.gt2r_mm3.size() != 3)
        {
            // 允许缺失，但你项目里建议必须有
            s.gt2r_mm3 = {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::quiet_NaN()};
        }

        if (out)
            *out = s;
        return true;
    }

} // namespace core
