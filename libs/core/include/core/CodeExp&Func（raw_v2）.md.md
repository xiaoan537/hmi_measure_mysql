## 代码解释

这是 **RAW 文件格式 V2 的数据结构定义头文件**，定义了测量原始数据的存储和访问接口。

### 文件组成

#### 1. 块类型枚举（Chunk Bit Mask）

```cpp
enum : quint32
{
    CHUNK_CONF = 1u << 0,   // 位0：配置信息块
    CHUNK_RUNO = 1u << 1,   // 位1：运行信息块
    CHUNK_GT2R = 1u << 2,   // 位2：GT2R 数据块（激光扫描数据）
    CHUNK_META = 1u << 3,   // 位3：元数据块
};
```

**设计模式**：位掩码模式
- 使用二进制位表示块的存在/不存在
- 多个块组合可用 `|` 运算符（如 `CHUNK_CONF | CHUNK_RUNO`）
- 检查块存在用 `&` 运算符（如 `mask & CHUNK_GT2R`）

| 块类型     | 含义      | 内容                   |
| ---------- | --------- | ---------------------- |
| CHUNK_CONF | 配置信息  | 扫描仪参数配置         |
| CHUNK_RUNO | 运行信息  | 运行时参数、状态       |
| CHUNK_GT2R | GT2R 数据 | 激光扫描的原始点云数据 |
| CHUNK_META | 元数据    | JSON 格式的补充信息    |

#### 2. RawWriteInfoV2 结构体

| 字段                | 类型    | 说明                                            |
| ------------------- | ------- | ----------------------------------------------- |
| **final_path**      | QString | 最终文件路径（绝对路径或相对路径）              |
| **file_size_bytes** | quint64 | 文件大小（字节）                                |
| **file_crc32**      | quint32 | 整个文件的 CRC32 校验值（可选，用于完整性验证） |
| **format_version**  | int     | 格式版本号（当前为 2）                          |
| **chunk_mask**      | quint32 | 块类型掩码（哪些块被包含）                      |
| **scan_kind**       | QString | 主扫描类型（"CONF" 或 "RUNO"）                  |
| **main_channels**   | int     | 主扫描通道数                                    |
| **rings**           | int     | 扫描环数（垂直分辨率）                          |
| **points_per_ring** | int     | 每环点数（水平分辨率）                          |
| **angle_step_deg**  | float   | 角度步长（度）                                  |
| **meta_json**       | QString | JSON 格式元数据（便于数据库索引）               |

**用途**：描述 RAW 文件的关键属性，便于检索、验证和重建数据

#### 3. 核心函数声明

##### writeRawV2() - 写入原始文件
```cpp
bool writeRawV2(
    const QString &raw_dir,              // 原始文件存放目录
    const MeasurementSnapshot &s,        // 测量快照对象（包含所有扫描数据）
    RawWriteInfoV2 *out,                 // 输出：文件信息（路径、大小、CRC等）
    QString *err);                       // 输出：错误信息
```

**流程**：
```
MeasurementSnapshot（内存中的测量数据）
    ↓
writeRawV2()
    ├─ 序列化各块（CONF/RUNO/GT2R/META）
    ├─ 写入到磁盘文件
    ├─ 计算 CRC32 校验值
    └─ 填充 RawWriteInfoV2 结构
    ↓
RAW 文件（磁盘） + RawWriteInfoV2（内存中的文件元信息）
```

##### readRawV2() - 读取原始文件
```cpp
bool readRawV2(
    const QString &file_path,            // 原始文件路径
    MeasurementSnapshot *out,            // 输出：反序列化后的测量快照
    QString *err);                       // 输出：错误信息
```

**流程**：
```
RAW 文件（磁盘）
    ↓
readRawV2()
    ├─ 读取文件内容
    ├─ 验证 CRC32（可选）
    └─ 反序列化各块
    ↓
MeasurementSnapshot（内存中的完整测量数据）
```

## 项目中的作用

| 角色             | 说明                                                   |
| ---------------- | ------------------------------------------------------ |
| **原始数据存储** | 定义激光测量仪的高精度点云数据如何保存到文件           |
| **数据完整性**   | 使用 CRC32 校验保证文件不损坏                          |
| **多块结构**     | 支持配置、运行参数、点云数据、元数据的分离存储         |
| **数据库索引**   | RawWriteInfoV2 中的 meta_json 便于存入数据库，加快查询 |
| **版本管理**     | format_version = 2 支持未来升级到 V3、V4 等            |
| **数据恢复**     | readRawV2() 支持从磁盘重新加载完整的测量数据           |

## 与项目其他模块的关系

```
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
```

## 典型应用场景

```cpp
// 场景1：保存测量结果
MeasurementSnapshot snapshot = ... // 包含扫描配置、点云数据
RawWriteInfoV2 raw_info;
QString err;

if (writeRawV2("./data/raw", snapshot, &raw_info, &err))
{
    // raw_info.final_path = "./data/raw/2026-01-07-12-34-56-abc123.raw"
    // raw_info.file_size_bytes = 5242880
    // raw_info.file_crc32 = 0x12345678
    
    // 将 raw_info 中的数据插入数据库
    db.insertResultWithRawIndexV2(measure_result, raw_info, &err);
}

// 场景2：重新加载数据进行分析
MeasurementSnapshot loaded_snapshot;
if (readRawV2("./data/raw/2026-01-07-12-34-56-abc123.raw", &loaded_snapshot, &err))
{
    // loaded_snapshot 现在包含完整的点云数据和扫描参数
    // 可用于数据分析、可视化等
}
```

## 设计优点

| 优点       | 说明                             |
| ---------- | -------------------------------- |
| **模块化** | 块结构允许独立读写不同类型的数据 |
| **可扩展** | 新增块类型时无需修改已有代码     |
| **高效**   | 二进制格式比 JSON/XML 更紧凑     |
| **可靠**   | CRC32 校验确保数据完整性         |
| **可追溯** | 完整的元数据便于数据查询和审计   |

**总结**：这个文件定义了 HMI 系统的**核心数据持久化格式**，是激光测量仪原始数据从内存到磁盘再到数据库索引的完整链路。