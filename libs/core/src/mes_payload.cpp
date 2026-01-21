#include "core/mes_payload.hpp"
#include <QJsonObject>
#include <QJsonDocument>
#include <limits>

namespace core
{

    static QJsonValue maybeNullDouble(double v, bool isNull)
    {
        return isNull ? QJsonValue(QJsonValue::Null) : QJsonValue(v);
    }

    QString buildMesPayloadV1(const MeasureResult &r, const RawWriteInfoV2 &raw)
    {
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
        o["raw"] = rawObj;

        if (!raw.meta_json.trimmed().isEmpty())
        {
            o["meta_json"] = raw.meta_json; // 字符串透传（你也可以后续改成 JSON 对象）
        }

        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }

} // namespace core
