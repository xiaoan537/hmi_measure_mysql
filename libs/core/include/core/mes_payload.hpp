#pragma once
#include <QString>
#include "core/model.hpp"
#include "core/raw_v2.hpp"

namespace core
{
    // 单条上传 payload（JSON, compact）
    QString buildMesPayloadV1(const MeasureResult &r, const RawWriteInfoV2 &raw);
}
