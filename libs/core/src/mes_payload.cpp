#include "core/mes_payload.hpp"
#include <QJsonObject>   // Qt JSON处理类
#include <QJsonDocument> // Qt JSON文档类
#include <limits>        // 用于处理浮点数的特殊值（如 NaN, Inf）

/*
文件的主要功能是将测量结果和原始数据信息转换为 JSON 格式的字符串，用于 MES（制造执行系统）系统的数据上传。
这个文件实现了一个名为 buildMesPayloadV1 的函数，它构建符合特定格式的 JSON 负载。
*/
namespace core
{

    /*
    这是一个静态辅助函数，用于处理可能为 NULL 的双精度浮点数
    如果 isNull 为 true，返回 JSON null 值；否则返回实际数值
    主要用于处理 A 型工件的 bc_len_mm 字段，因为 A 型工件可能没有这个值
    */
    static QJsonValue maybeNullDouble(double v, bool isNull)
    {
        return isNull ? QJsonValue(QJsonValue::Null) : QJsonValue(v);
    }

    // 返回一个 JSON 格式的字符串。
    QString buildMesPayloadV1(const MeasureResult &r, const RawWriteInfoV2 &raw)
    {
        // 创建一个主 JSON 对象 o，然后添加测量结果相关的字段
        QJsonObject o;
        o["measurement_uuid"] = r.measurement_uuid;
        o["part_id"] = r.part_id;
        o["part_type"] = r.part_type;
        o["ok"] = r.ok;
        o["measured_at_utc"] = r.measured_at_utc.toUTC().toString(Qt::ISODateWithMs);
        o["total_len_mm"] = r.total_len_mm;

        // A型 bc_len 允许 null；B型通常有值
        const bool bcNull = (r.part_type == "A");
        o["bc_len_mm"] = maybeNullDouble(r.bc_len_mm, bcNull);

        o["status"] = r.status;

        // 创建一个嵌套的 JSON 对象 rawObj，包含原始文件信息
        QJsonObject rawObj;
        rawObj["format"] = "HMIRAW02";
        rawObj["file_path"] = raw.final_path;
        rawObj["format_version"] = raw.format_version;
        rawObj["file_crc32"] = double(raw.file_crc32);
        rawObj["chunk_mask"] = double(raw.chunk_mask);
        rawObj["scan_kind"] = raw.scan_kind;
        rawObj["main_channels"] = raw.main_channels;
        rawObj["rings"] = raw.rings;
        rawObj["points_per_ring"] = raw.points_per_ring;
        rawObj["angle_step_deg"] = raw.angle_step_deg;
        o["raw"] = rawObj;  // 这个对象被嵌套在主对象o的 "raw" 字段中

        // 如果存在元数据 JSON 字符串，则直接添加到主对象中
        if (!raw.meta_json.trimmed().isEmpty())
        {
            o["meta_json"] = raw.meta_json; // 字符串透传（也可以后续改成 JSON 对象）
        }

        // JSON序列化，将 JSON 对象序列化为紧凑格式的 JSON 字符串，使用 UTF-8 编码确保兼容性
        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }

} // namespace core

/*
根据代码，生成的 JSON 结构大致如下：
{
    "measurement_uuid": "uuid-string",
    "part_id": "part-id",
    "part_type": "A",
    "ok": true,
    "measured_at_utc": "2023-01-01T12:00:00.000Z",
    "total_len_mm": 100.5,
    "bc_len_mm": null,
    "status": "READY",
    "raw": {
        "format": "HMIRAW02",
        "file_path": "/path/to/file.raw",
        "format_version": 2,
        "file_crc32": 1234567890,
        "chunk_mask": 15,
        "scan_kind": "CONF",
        "main_channels": 2,
        "rings": 360,
        "points_per_ring": 1000,
        "angle_step_deg": 0.5
    },
    "meta_json": "{\"key\":\"value\"}"
}
*/