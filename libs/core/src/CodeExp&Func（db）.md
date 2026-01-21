这是**数据库操作的具体实现文件**，包含数据库连接、表初始化、数据插入三大核心功能。

### 一、open() - 数据库连接实现

#### 驱动检验

```cpp
if (!QSqlDatabase::isDriverAvailable(cfg.driver))
{
    *err = QString("DB driver not available: %1 (install libqt5sql5-mysql)").arg(cfg.driver);
    return false;
}
```

检查 MySQL 驱动是否可用，若缺失提示安装 `libqt5sql5-mysql` 包

#### 连接创建

```cpp
const QString connName = "hmi_conn";
if (QSqlDatabase::contains(connName))
    db_ = QSqlDatabase::database(connName);  // 复用已有连接
else
    db_ = QSqlDatabase::addDatabase(cfg.driver, connName);  // 创建新连接
```

使用连接池模式，避免重复创建连接

#### 连接参数设置

```cpp
db_.setHostName(cfg.host);        // localhost
db_.setPort(cfg.port);            // 3306
db_.setDatabaseName(cfg.name);    // hmi_dev
db_.setUserName(cfg.user);        // hmi
db_.setPassword(cfg.pass);        // hmi_pass
db_.setConnectOptions(cfg.options);  // MYSQL_OPT_RECONNECT=1
```

### 二、ensureSchema() - 表结构初始化

#### 主表创建 (measure_result)

| 字段             | 类型            | 约束       | 说明                         |
| ---------------- | --------------- | ---------- | ---------------------------- |
| id               | BIGINT UNSIGNED | 主键，自增 | 表行唯一标识                 |
| measurement_uuid | CHAR(36)        | 唯一索引   | 测量唯一标识（UUID）         |
| part_id          | VARCHAR(64)     | 索引       | 零件条码                     |
| part_type        | CHAR(1)         | -          | 零件类型（A/B）              |
| ok               | TINYINT(1)      | -          | 测量结果（1=合格，0=不合格） |
| measured_at_utc  | DATETIME(3)     | 索引       | 测量时间（UTC，精度毫秒）    |
| total_len_mm     | DOUBLE          | NULL       | 总长度（毫米）               |
| status           | VARCHAR(16)     | -          | 状态（READY/WRITING）        |

#### 索引策略

```sql
UNIQUE KEY uk_measurement_uuid (measurement_uuid),  // 防重复
KEY idx_measured_at_utc (measured_at_utc),         // 时间范围查询
KEY idx_part_id (part_id)                          // 零件查询
```

#### 视图创建 (mes_v_measure_result)

```sql
CREATE OR REPLACE VIEW mes_v_measure_result AS 
SELECT ... FROM measure_result WHERE status='READY';
```

为 MES（制造执行系统）提供稳定的数据读取入口，仅暴露 READY 状态的记录

### 三、insertResult() - 数据插入实现

#### 事务管理

```cpp
if (!db_.transaction())  // 开启事务
    return false;
```

确保数据一致性，插入失败则回滚

#### 参数化查询（防 SQL 注入）

```cpp
q.prepare(
    "INSERT INTO measure_result(...) VALUES (:measurement_uuid, :part_id, ...)"
);
q.bindValue(":measurement_uuid", r.measurement_uuid);
q.bindValue(":part_id", r.part_id);
// ... 绑定其他字段
```

使用 `bindValue()` 进行参数绑定，防止 SQL 注入攻击

#### 布尔值转换

```cpp
q.bindValue(":ok", QVariant(r.ok ? 1 : 0));  // 布尔转整数
```

MySQL TINYINT(1) 存储 0/1，这里从 bool 转换

#### 事务提交

```cpp
if (!q.exec())
{
    db_.rollback();  // 执行失败，回滚
    return false;
}
if (!db_.commit())  // 执行成功，提交
    return false;
```

## 项目中的作用

| 功能           | 说明                                            |
| -------------- | ----------------------------------------------- |
| **连接管理**   | 负责建立和维护与 MySQL 的连接                   |
| **表初始化**   | 首次运行时自动创建 measure_result 表和 MES 视图 |
| **数据持久化** | 将测量结果可靠地保存到数据库                    |
| **数据一致性** | 使用事务和参数化查询确保数据安全                |
| **集成接口**   | 为 MES 系统提供视图接口，实现系统间数据交互     |
| **错误报告**   | 通过 QString 指针返回详细错误信息               |

## 执行流程示意

```tex
应用启动
    ↓
db.open(cfg) ─── 连接 MySQL ─── 返回 true/false
    ↓
db.ensureSchema() ─── 自动建表 (IF NOT EXISTS) ─── 返回 true/false
    ↓
用户操作
    ↓
db.insertResult(data) ─── 
    ├─ 开启事务
    ├─ 绑定参数
    ├─ 执行 INSERT
    ├─ 提交/回滚
    └─ 返回 true/false
```

这是项目的**数据访问层（DAL）**，隐藏 Qt SQL 的复杂性，提供简洁的业务接口。