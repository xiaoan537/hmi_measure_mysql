#pragma once // 现代编译器支持的宏，确保头文件只被包含一次，避免重复定义
#include <QString>
#include <QDateTime>

namespace core
{

    struct MeasureResult
    {
        QString measurement_uuid; // 建议：UUID字符串（方便幂等/追溯）
        QString part_id;          // 条码/编号
        QString part_type;        // "A" / "B"
        bool ok = true;           // OK/NG

        QDateTime measured_at_utc; // 历史字段名；当前按设备/MES本地时间保存
        double total_len_mm = 0.0; // 先占位：后面扩展更多字段
        double bc_len_mm = 0.0;    // A型可不使用（后面写库时写NULL更合适）

        QString status; // "READY" / "WRITING" ...（先最小化）

        // 根据项目需求，添加更多测量相关字段，待扩展
    };

} // namespace core

/*
这个文件定义了一个简单的数据结构 MeasureResult ，用于封装测量结果的相关信息。它包含以下特点：

使用 Qt 的数据类型（ QString 和 QDateTime ）确保跨平台兼容性。
结构清晰，字段命名直观，便于扩展。
适用于 HMI（人机界面）或测量系统中存储和传递测量结果。
如果需要进一步扩展或修改字段，可以基于此结构体进行调整。
*/
