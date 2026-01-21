#pragma once
#include <QString>
#include <QVector>
#include "core/snapshot.hpp"

namespace core
{

    // chunk bit mask 位掩码模式
    /*
    使用二进制位表示块的存在/不存在
    多个块组合可用 | 运算符（如 CHUNK_CONF | CHUNK_RUNO）
    检查块存在用 & 运算符（如 mask & CHUNK_GT2R）
    */
    enum : quint32
    {
        CHUNK_CONF = 1u << 0,       // 1u 表示 1 的无符号整数，左移 0 位即 000...0001
        CHUNK_RUNO = 1u << 1,       // 左移 1 位即 000...0010
        CHUNK_GT2R = 1u << 2,       // 左移 2 位即 000...0100
        CHUNK_META = 1u << 3,       // 左移 3 位即 000...1000
    };

    // 描述 RAW 文件的关键属性，便于检索、验证和重建数据
    /*
    HMI 应用 (main.cpp)
        ↓
    db.insertResultWithRawIndexV2()
    ├─ 调用 writeRawV2() 保存原始文件到 ./data/raw/
    ├─ 获得 RawWriteInfoV2（路径、大小、CRC等）
    └─ 将 RawWriteInfoV2 信息写入数据库表 raw_file_index
         ↓
    MES 系统可通过数据库查询 raw_file_index 表
        ↓
    根据需要调用 readRawV2() 读取完整的点云数据
    */
    struct RawWriteInfoV2
    {
        QString final_path;
        quint64 file_size_bytes = 0;
        quint32 file_crc32 = 0; // 对整个文件做crc（可选，这里实现）
        int format_version = 2;

        quint32 chunk_mask = 0; // CONF/RUNO/GT2R/META
        QString scan_kind;      // "CONF" or "RUNO"（主扫描）
        int main_channels = 0;
        int rings = 0;
        int points_per_ring = 0;
        float angle_step_deg = 0.0f;

        QString meta_json; // 便于写入 DB 索引（可选）
    };

    // 写入原始文件
    bool writeRawV2(
        const QString &raw_dir,
        const MeasurementSnapshot &s,
        RawWriteInfoV2 *out,
        QString *err);

    // 读取原始文件
    bool readRawV2(
        const QString &file_path,
        MeasurementSnapshot *out,
        QString *err);

} // namespace core
