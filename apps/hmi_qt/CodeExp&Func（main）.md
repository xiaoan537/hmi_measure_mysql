## 代码解释

这是 **HMI 应用的主程序入口**，实现了一个简单的 Qt 应用，演示了从配置读取到数据库插入的完整流程。

### 头文件包含

```cpp
#include <QApplication>      // Qt 应用主对象
#include <QPushButton>       // 按钮控件
#include <QMessageBox>       // 消息对话框
#include <QDir>              // 目录操作
#include <QUuid>             // UUID 生成

#include "core/config.hpp"   // 配置管理
#include "core/db.hpp"       // 数据库操作
#include "core/model.hpp"    // 数据模型
```

### 辅助函数：ensureDir()

```cpp
static bool ensureDir(const QString &path, QString *err)
{
  QDir d;
  if (d.exists(path))
    return true;                    // 目录存在，直接返回
  if (!d.mkpath(path))             // 创建目录及其父目录
  {
    if (err)
      *err = QString("Failed to create dir: %1").arg(path);
    return false;
  }
  return true;
}
```

**功能**：确保指定目录存在，不存在则创建

| 步骤     | 说明                           |
| -------- | ------------------------------ |
| 检查存在 | 若目录已存在，直接返回 true    |
| 创建目录 | 使用 `mkpath()` 递归创建父目录 |
| 错误处理 | 失败时将错误信息写入指针       |

### main() 函数核心逻辑

#### 1. 应用初始化

```cpp
QApplication a(argc, argv);
QPushButton btn("Insert one READY record into MySQL");
btn.resize(420, 60);
```

创建 Qt 应用和一个按钮控件，显示提示信息

#### 2. 按钮点击事件（信号槽）

```cpp
QObject::connect(&btn, &QPushButton::clicked, [&]() { ... });
```

使用 lambda 表达式处理按钮点击事件，内部执行以下步骤：

##### 步骤 1：读配置

```cpp
const auto cfg = core::loadConfigIni("configs/app.ini");
if (cfg.db.name.trimmed().isEmpty()) {
  QMessageBox::critical(nullptr, "Config", "db.name is empty in configs/app.ini");
  return;
}
```

加载 INI 配置文件，校验数据库名是否为空

##### 步骤 2：确保数据目录存在

```cpp
if (!ensureDir(cfg.paths.data_root, &err) ||
    !ensureDir(cfg.paths.raw_dir, &err) ||
    !ensureDir(cfg.paths.log_dir, &err)) {
  QMessageBox::critical(nullptr, "Paths", err);
  return;
}
```

创建三个必要的数据目录：

- `data_root`：数据根目录
- `raw_dir`：原始数据目录
- `log_dir`：日志目录

##### 步骤 3：连接数据库 + 初始化表结构

```cpp
core::Db db;
if (!db.open(cfg.db, &err)) {
  QMessageBox::critical(nullptr, "DB open failed", err);
  return;
}
if (!db.ensureSchema(&err)) {
  QMessageBox::critical(nullptr, "Schema failed", err);
  return;
}
```

- 创建数据库对象
- 连接 MySQL
- 自动创建表结构（若表不存在）

##### 步骤 4：构造并插入测量结果

```cpp
core::MeasureResult r;
r.measurement_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
r.part_id = "TEST-0001";
r.part_type = "A";
r.ok = true;
r.measured_at_utc = QDateTime::currentDateTimeUtc();
r.total_len_mm = 123.456;
r.status = "READY";

if (!db.insertResult(r, &err)) {
  QMessageBox::critical(nullptr, "Insert failed", err);
  return;
}
```

| 字段             | 值               | 说明                 |
| ---------------- | ---------------- | -------------------- |
| measurement_uuid | UUID（无连字符） | 测量唯一标识         |
| part_id          | "TEST-0001"      | 零件编号（测试数据） |
| part_type        | "A"              | 零件类型             |
| ok               | true             | 测量合格             |
| measured_at_utc  | 当前 UTC 时间    | 测量时间戳           |
| total_len_mm     | 123.456          | 总长度（毫米）       |
| status           | "READY"          | MES 可读取状态       |

##### 步骤 5：成功提示

```cpp
QMessageBox::information(nullptr, "OK", 
  "Inserted READY record.\nMES can read from view: mes_v_measure_result");
```

提示用户数据已插入，MES 可从视图读取

#### 3. 应用运行

## 项目中的作用

| 角色         | 说明                                                        |
| ------------ | ----------------------------------------------------------- |
| **应用入口** | 整个 HMI 应用的启动点                                       |
| **集成演示** | 展示了配置读取 → 目录管理 → 数据库连接 → 数据插入的完整流程 |
| **原型验证** | 通过简单的按钮测试各组件是否正常工作                        |
| **错误处理** | 每个步骤都有错误检查和用户提示                              |
| **MES 集成** | 将数据标记为 "READY"，供 MES 系统通过视图读取               |

## 应用程序流程图

## 典型用法

```bash
cd /home/xiaoan/QT_Project/work/hmi_measure_mysql
./build/apps/hmi_qt/hmi_qt
# 点击按钮 → 自动插入一条测试记录到数据库
```

**总结**：这是一个最小化但完整的 HMI 应用原型，演示了如何从配置、目录、数据库到数据持久化的完整链路。