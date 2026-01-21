#include "core/db.hpp"

#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QVariant>

#include <QtSql/QSqlRecord>
#include "core/mes_payload.hpp"

namespace core
{

    // 实现 Db 类的方法
    // 数据库连接实现
    bool Db::open(const DbConfig &cfg, QString *err)
    {
        /*
        检查数据库驱动是否可用，如果不可用则返回错误信息，提示用户安装所需的数据库驱动包。
        QSqlDatabase::isDriverAvailable() 方法用于检查数据库驱动是否可用。
        如果数据库驱动不可用，则返回 false，否则返回 true。
        */
        if (!QSqlDatabase::isDriverAvailable(cfg.driver))
        {

            /*
            err是一个指向QString的指针参数。空指针检查：if (err)用于验证调用者是否传递了一个有效的指针。
            如果err是nullptr（或NULL），这个条件就会失败，不会尝试设置错误信息。
            这种设计允许调用者灵活选择是否需要接收错误信息：
                如果需要错误信息，调用者可以传递一个QString变量的地址：
                    QString errorMsg;
                    db.open(config, &errorMsg);
                如果不需要错误信息，调用这可以传递nullptr:
                    db.open(config, nullptr);
            通过先检查指针是否为空，再使用*err = ...赋值，可以避免空指针解引用导致的程序崩溃。
            */

            if (err)
                *err = QString("DB driver not available: %1 (install libqt5sql5-mysql)").arg(cfg.driver);
            return false;

            /*
            错误信息设置：
                当检测到所需的数据库驱动不可用时，它会设置一个错误信息字符串。
            字符串格式化：
                使用 QString::arg() 方法进行格式化。%1 是一个占位符，会被 cfg.driver 的值替换
            */
        }

        /*
        连接池模式：
            连接池模式是一种数据库连接管理技术，用于在应用程序中重复使用数据库连接，而不是每次都创建新的连接。
            这可以提高应用程序的性能和响应时间，因为创建和关闭数据库连接是一个相对耗时的操作。
            连接池模式的工作原理是维护一个连接池，其中包含多个数据库连接。
            当应用程序需要与数据库交互时，它从连接池获取一个连接，执行操作后，将连接返回给连接池而不是关闭它。
            这样，下一次需要与数据库交互时，就可以直接使用连接池中的连接，而无需重新创建。
        */

        // 使用连接池模式，避免重复创建连接对象（连接池模式：QT框架管理数据库连接的一种常见且推荐的做法）
        // 调用此方法后，一个名为 "hmi_conn"的新连接就被添加到Qt的管理列表中，并赋值给 db_。
        const QString connName = "hmi_conn";
        if (QSqlDatabase::contains(connName)) // 静态函数 QSqlDatabase::contains()会检查当前应用程序中是否已经存在一个名为 "hmi_conn"的连接
        {
            db_ = QSqlDatabase::database(connName); // 从连接池中获取已存在的连接
        }
        else
        {
            db_ = QSqlDatabase::addDatabase(cfg.driver, connName); // 向连接池添加新连接
        }

        // 连接参数设置
        db_.setHostName(cfg.host);
        db_.setPort(cfg.port);
        db_.setDatabaseName(cfg.name);
        db_.setUserName(cfg.user);
        db_.setPassword(cfg.pass);
        if (!cfg.options.trimmed().isEmpty())
        {
            db_.setConnectOptions(cfg.options);
        }

        /*
        .trimmed() 去除字符串两端的空白字符
        .isEmpty() 检查字符串是否为空
        整个条件判断：如果去除空白后的选项字符串不为空，则执行设置
        调用Qt的QSqlDatabase对象的setConnectOptions方法
        将配置中的选项字符串应用到数据库连接上
        */
        // !db_.open() 检查连接是否失败
        if (!db_.open())
        {
            if (err)
                *err = db_.lastError().text();
            return false;
        }
        return true;
        /*
        if (err) 检查调用者是否提供了错误信息存储位置（非空指针）
        db_.lastError() 获取数据库操作产生的最后一个错误对象
        .text() 将错误对象转换为可读的错误信息字符串
        *err = ... 将错误信息存储到调用者提供的字符串变量中
        */
    }

    // ensureSchema 方法的主要作用是确保数据库中存在所需的数据表结构，如果不存在则创建它们。这是一种"幂等"操作，即无论执行多少次，结果都是一致的。
    bool Db::ensureSchema(QString *err)
    {

        // 创建一个与当前数据库连接 db_ 关联的SQL查询对象QSqlQuery q(db_);该对象用于执行SQL语句（如创建表、插入数据等）。
        QSqlQuery q(db_);

        // Lambda表达式在Db::ensureSchema方法中定义，作为一个错误处理的辅助函数
        auto fail = [&](const QString &e)
        {
            if (err)
                *err = e;
            return false;
        };

        /*
        这段代码实现了一个简单的数据库模式迁移（Schema Migration）机制，用于确保数据库结构在应用启动时处于最新版本。
        它通过一个名为schema_migrations的表来记录当前的数据库版本，并依次应用缺失的迁移脚本。
        创建表schema_migrations（如果不存在），用于记录已应用的迁移版本号（version）和应用时间（applied_at）。
        DATETIME(3)表示时间精确到毫秒，ENGINE=InnoDB指定存储引擎，utf8mb4支持完整的Unicode字符集。
        */
        // 0) 创建迁移记录表
        if (!q.exec(
                "CREATE TABLE IF NOT EXISTS schema_migrations ("
                "  version INT NOT NULL PRIMARY KEY,"
                "  applied_at DATETIME(3) NOT NULL"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"))
        {
            return fail(q.lastError().text());
        }

        /*
        定义了一个 lambda 函数，用于查询数据库表 schema_migrations 中的最大版本号。
        这是典型的数据库迁移系统中的版本追踪机制。
        IFNULL 函数：如果 MAX(version) 返回 NULL（表为空），则返回 0
        错误处理：查询失败返回 -1
        */
        // 用于查询数据库表 schema_migrations 中的最大版本号
        auto currentVersion = [&]() -> int
        {
            QSqlQuery t(db_);
            if (!t.exec("SELECT IFNULL(MAX(version), 0) FROM schema_migrations;"))
                return -1;
            if (!t.next()) // 将查询结果指针移动到第一条记录
                return -1;
            return t.value(0).toInt(); // 获取第一列的值并转换为整数返回
        };

        /*
        定义了一个 lambda 函数，用于检查指定表中的列是否存在。
        通过查询 MySQL 的系统元数据表 INFORMATION_SCHEMA.COLUMNS 来实现，这是数据库 schema 检查的标准方法。
        1、准备查询：创建一个 QSqlQuery对象 t并关联到成员变量 db_所代表的数据库连接。
            使用 prepare()函数准备一条参数化 SQL 语句。这条语句查询的是数据库的 INFORMATION_SCHEMA.COLUMNS系统视图，这个视图包含了数据库中所有表的列信息。WHERE子句使用 DATABASE()函数限定当前数据库，并通过占位符 :t和 :c来指定具体的表名和列名。
        2、绑定参数：通过 bindValue()函数将Lambda表达式的输入参数 table和 column分别绑定到SQL语句中的 :t和 :c占位符上。这是推荐的做法，可以防止SQL注入攻击，并避免手动拼接字符串时可能出现的格式错误。
        3、执行与判断：
        t.exec()执行SQL查询。如果执行失败，直接返回 false。
        t.next()将结果集指针移动到第一条记录。因为 COUNT(*)查询总是返回一行数据，如果这一步失败，说明结果异常，返回 false。
        t.value(0)获取查询结果第一列（索引为0）的值，也就是 COUNT(*)的结果。如果这个值大于0，表示至少存在一行符合条件的记录，即列存在，函数返回 true；否则返回 false。
        */
        // 用于检查指定表中的列是否存在
        auto columnExists = [&](const QString &table, const QString &column) -> bool
        {
            QSqlQuery t(db_);
            t.prepare(
                "SELECT COUNT(*) "
                "FROM INFORMATION_SCHEMA.COLUMNS "
                "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :t AND COLUMN_NAME = :c;");
            t.bindValue(":t", table);
            t.bindValue(":c", column);
            if (!t.exec())
                return false;
            if (!t.next())
                return false;
            return t.value(0).toInt() > 0;
        };

        // 用于将迁移版本号写入 schema_migrations 表
        auto applyVersion = [&](int v) -> bool
        {
            QSqlQuery t(db_);
            t.prepare("INSERT INTO schema_migrations(version, applied_at) VALUES(:v, NOW(3));");
            t.bindValue(":v", v);
            if (!t.exec())
                return false;
            return true;
        };

        int cur = currentVersion();
        if (cur < 0)
            return fail("Failed to read schema_migrations");

        // v1: measure_result + MES view
        if (cur < 1)
        {
            /*
            如果当前版本cur < 1，则执行以下事务性操作：
            开启事务：
            创建数据表：
            创建视图：
            记录迁移版本：
            */
            // 事务：开始一个新的数据库事务，用于将多个操作封装在一起执行，确保要么全部成功，要么全部失败事务回滚
            if (!db_.transaction())
                return fail(db_.lastError().text());

            /*
            这是一个SQL数据定义语言(DDL)语句，用于创建 measure_result 表。

            创建 measure_result 表
            if not exists
            即使 measure_result表已经存在，语句也会安静地跳过创建步骤，并返回一个提示而非错误，不会影响后续任何操作。
            这使得它在初始化脚本或应用程序启动脚本中特别有用，可以放心地重复执行。

            表字段说明：
            id bigint unsigned not null auto_increment primary key:
                定义一个名为 id 的列，类型为无符号大整数(bigint unsigned)，
                该列不能为空(not null)，并且是自动递增(auto_increment)的主键(primary key)。
                确保每条记录有唯一标识(id)，防止重复插入。
            measurement_uuid char(36) not null:
                定义一个名为 measurement_uuid 的列，类型为字符型(char)，长度为36个字符，存储UUID值。
                该列不能为空(not null)。用于唯一标识每次测量。
            part_id varchar(64) not null:
                定义一个名为 part_id 的列，类型为字符串型(varchar,可变长度字符串)，长度为64个字符，存储零件ID。
                该列不能为空(not null)。用于标识测量的具体零件。
            part_type char(1) not null:
                定义一个名为 part_type 的列，类型为字符型(char)，长度为1个字符，存储零件类型。
                该列不能为空(not null)。用于标识测量的零件类型（如"A"、"B"等）。
            ok tinyint(1) not null:
                定义一个名为 ok 的列，类型为小整数(tinyint)，长度为1个字节，存储布尔值。
                该列不能为空(not null)。用于标识测量结果是否通过（1表示通过，0表示未通过）。
                确保测量结果的有效性（通过或未通过）。
            measured_at_utc datetime(3) not null:
                定义一个名为 measured_at_utc 的列，类型为日期时间型(datetime)，精度为3毫秒(3)，存储测量时间。
                该列不能为空(not null)。用于记录测量的具体时间点。
            total_len_mm double null:
                定义一个名为 total_len_mm 的列，类型为双精度浮点数(double)，可以为空(null)，存储总长度（单位：毫米）。
            status varchar(16) not null:
                定义一个名为 status 的列，类型为字符串型(varchar)，长度为16个字符，存储测量状态。
                该列不能为空(not null)。用于标识测量的当前状态（如"READY"、"WRITING"等）。
            unique key uk_measurement_uuid (measurement_uuid):
                定义一个唯一键(unique key)，名称为 uk_measurement_uuid，基于 measurement_uuid 列。
                确保每次测量的 UUID 都是唯一的，防止重复测量。

            索引说明：
            key idx_measured_at_utc (measured_at_utc):
                定义一个普通键(key)，名称为 idx_measured_at_utc，基于 measured_at_utc 列。
                用于加速按测量时间范围查询测量结果。
            key idx_part_id (part_id):
                定义一个普通键(key)，名称为 idx_part_id，基于 part_id 列。
                用于加速按零件ID查询测量结果。

            表选项：
            engine=InnoDB default charset=utf8mb4;
                指定表使用 InnoDB 存储引擎，支持事务和外键约束，并设置默认字符集为 utf8mb4。支持完整的 Unicode 字符。
            */
            // 目标：MySQL 5.7 子集（OceanBase MySQL 模式更稳）
            // 定义数据表结构（DDL），创建测量结果表的 SQL 语句
            const char *ddl =
                "CREATE TABLE IF NOT EXISTS measure_result ("
                "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
                "  measurement_uuid CHAR(36) NOT NULL,"
                "  part_id VARCHAR(64) NOT NULL,"
                "  part_type CHAR(1) NOT NULL,"
                "  ok TINYINT(1) NOT NULL,"
                "  measured_at_utc DATETIME(3) NOT NULL,"
                "  total_len_mm DOUBLE NULL,"
                "  bc_len_mm DOUBLE NULL,"
                "  status VARCHAR(16) NOT NULL,"
                "  UNIQUE KEY uk_measurement_uuid (measurement_uuid),"
                "  KEY idx_measured_at_utc (measured_at_utc),"
                "  KEY idx_part_id (part_id)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

            if (!q.exec(ddl))
            {
                db_.rollback();
                return fail(q.lastError().text());
            }

            // MES view: only READY
            const char *view_ddl =
                "CREATE OR REPLACE VIEW mes_v_measure_result AS "
                "SELECT "
                "  measurement_uuid, part_id, part_type, ok, measured_at_utc, total_len_mm, bc_len_mm, status "
                "FROM measure_result "
                "WHERE status='READY';";

            if (!q.exec(view_ddl))
            {
                db_.rollback();
                return fail(q.lastError().text());
            }

            if (!applyVersion(1))
            {
                db_.rollback();
                return fail("Failed to write schema_migrations v1");
            }

            if (!db_.commit())
                return fail(db_.lastError().text());
            cur = 1;
        }

        // v2: raw_file_index (HMIRAW02)
        if (cur < 2)
        {
            if (!db_.transaction())
                return fail(db_.lastError().text());

            const char *ddl2 =
                "CREATE TABLE IF NOT EXISTS raw_file_index ("
                "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
                "  measurement_uuid CHAR(36) NOT NULL,"
                "  file_path VARCHAR(512) NOT NULL,"
                "  file_size_bytes BIGINT UNSIGNED NOT NULL,"
                "  format_version INT NOT NULL,"
                "  file_crc32 BIGINT UNSIGNED NOT NULL,"
                "  chunk_mask BIGINT UNSIGNED NOT NULL,"
                "  scan_kind CHAR(4) NOT NULL,"
                "  main_channels INT NOT NULL,"
                "  rings SMALLINT UNSIGNED NOT NULL,"
                "  points_per_ring SMALLINT UNSIGNED NOT NULL,"
                "  angle_step_deg FLOAT NOT NULL,"
                "  meta_json LONGTEXT NULL,"
                "  created_at_utc DATETIME(3) NOT NULL,"
                "  UNIQUE KEY uk_raw_measurement_uuid (measurement_uuid),"
                "  KEY idx_raw_created_at (created_at_utc)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

            if (!q.exec(ddl2))
            {
                db_.rollback();
                return fail(q.lastError().text());
            }

            if (!applyVersion(2))
            {
                db_.rollback();
                return fail("Failed to write schema_migrations v2");
            }

            if (!db_.commit())
                return fail(db_.lastError().text());
            cur = 2;
        }

        // v3: ensure bc_len_mm exists + recreate view (for older DBs that created v1 without bc_len_mm)
        if (cur < 3)
        {
            if (!db_.transaction())
                return fail(db_.lastError().text());

            if (!columnExists("measure_result", "bc_len_mm"))
            {
                if (!q.exec("ALTER TABLE measure_result ADD COLUMN bc_len_mm DOUBLE NULL AFTER total_len_mm;"))
                {
                    db_.rollback();
                    return fail(q.lastError().text());
                }
            }

            // recreate view to include bc_len_mm
            if (!q.exec("DROP VIEW IF EXISTS mes_v_measure_result;"))
            {
                db_.rollback();
                return fail(q.lastError().text());
            }
            if (!q.exec(
                    "CREATE VIEW mes_v_measure_result AS "
                    "SELECT "
                    "  measurement_uuid, part_id, part_type, ok, measured_at_utc, total_len_mm, bc_len_mm, status "
                    "FROM measure_result "
                    "WHERE status='READY';"))
            {
                db_.rollback();
                return fail(q.lastError().text());
            }

            if (!applyVersion(3))
            {
                db_.rollback();
                return fail("Failed to write schema_migrations v3");
            }

            if (!db_.commit())
                return fail(db_.lastError().text());
            cur = 3;
        }

        // ---- v4: mes_outbox (manual queue + reliable delivery)
        if (cur < 4)
        {
            if (!db_.transaction())
                return fail(db_.lastError().text());

            if (!q.exec(
                    "CREATE TABLE IF NOT EXISTS mes_outbox ("
                    "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
                    "  measurement_uuid CHAR(36) NOT NULL,"
                    "  event_type VARCHAR(32) NOT NULL,"
                    "  payload_json LONGTEXT NOT NULL,"
                    "  status VARCHAR(16) NOT NULL," // PENDING/SENDING/SENT/FAILED
                    "  attempt_count INT NOT NULL DEFAULT 0,"
                    "  next_retry_at_utc DATETIME(3) NOT NULL,"
                    "  last_error TEXT NULL,"
                    "  last_http_code INT NULL,"
                    "  last_response_body TEXT NULL,"
                    "  created_at_utc DATETIME(3) NOT NULL,"
                    "  updated_at_utc DATETIME(3) NOT NULL,"
                    "  UNIQUE KEY uk_outbox_uuid_type (measurement_uuid, event_type),"
                    "  KEY idx_outbox_status_next (status, next_retry_at_utc),"
                    "  KEY idx_outbox_created (created_at_utc)"
                    ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"))
            {
                db_.rollback();
                return fail(q.lastError().text());
            }

            if (!q.exec("INSERT INTO schema_migrations(version, applied_at) VALUES(4, NOW(3));"))
            {
                db_.rollback();
                return fail(q.lastError().text());
            }

            if (!db_.commit())
                return fail(db_.lastError().text());
            cur = 4;
        }

        return true;
    }

    /*
    这段代码是 Db::insertResult 方法的完整实现，用于将测量结果数据插入到数据库中。
    这个方法的主要作用是将一个测量结果记录安全地插入到数据库中，使用了事务机制确保数据一致性。
    */
    bool Db::insertResult(const MeasureResult &r, QString *err)
    {
        /*
        启动数据库事务
        如果启动失败，记录错误并返回false
        事务确保操作的原子性：要么全部成功，要么全部失败
        */
        if (!db_.transaction())
        {
            if (err)
                *err = db_.lastError().text();
            return false;
        }

        /*
        准备SQL插入语句
        创建与当前数据库连接关联的查询对象
        使用参数化查询（Prepared Statement）防止SQL注入
        冒号前缀的名称（如:measurement_uuid）是占位符，稍后会被实际值替换
        */
        QSqlQuery q(db_);
        q.prepare(
            "INSERT INTO measure_result("
            " measurement_uuid, part_id, part_type, ok, measured_at_utc, total_len_mm, status"
            ") VALUES ("
            " :measurement_uuid, :part_id, :part_type, :ok, :measured_at_utc, :total_len_mm, :status"
            ");");

        /*
        1. **准备阶段**：`q.prepare(...)`发送带占位符的SQL模板给数据库驱动。
        2. **标签生成**：`QStringLiteral(":measurement_uuid")`在编译时高效生成占位符名称对应的 `QString`。
        3. **数据打包**：`QVariant(r.measurement_uuid)`将变量 `r.measurement_uuid`的值打包成通用容器。
        4. **执行绑定**：`bindValue`将第2步的“标签”和第3步的“数据包”关联起来。
        5. **最终执行**：调用 `q.exec()`，数据库驱动将绑定的所有值填入SQL模板的对应位置，并执行完整的语句。
        */
        q.bindValue(QStringLiteral(":measurement_uuid"), QVariant(r.measurement_uuid));
        q.bindValue(QStringLiteral(":part_id"), QVariant(r.part_id));
        q.bindValue(QStringLiteral(":part_type"), QVariant(r.part_type));
        q.bindValue(QStringLiteral(":ok"), QVariant(r.ok ? 1 : 0));
        q.bindValue(QStringLiteral(":measured_at_utc"), QVariant(r.measured_at_utc));
        q.bindValue(QStringLiteral(":total_len_mm"), QVariant(r.total_len_mm));
        q.bindValue(QStringLiteral(":status"), QVariant(r.status));

        // 执行插入操作，将绑定的参数值插入到数据库
        if (!q.exec())
        {
            db_.rollback(); // 如果执行失败，回滚事务以保持数据一致性
            if (err)
                *err = q.lastError().text();
            return false;
        }

        // 提交事务，使更改永久生效
        // 如果提交失败，记录错误并返回false
        if (!db_.commit())
        {
            if (err)
                *err = db_.lastError().text();
            return false;
        }

        return true;
    }

    // 新版 raw index 插入接口（HMIRAW02），在main中调用时使用，上边的insertResult函数基本可以遗弃了。
    bool Db::insertResultWithRawIndexV2(const MeasureResult &r, const RawWriteInfoV2 &raw, QString *err)
    {
        if (!db_.transaction())
        {
            if (err)
                *err = db_.lastError().text();
            return false;
        }

        // 1) measure_result
        {
            QSqlQuery q(db_);
            q.prepare(
                "INSERT INTO measure_result("
                " measurement_uuid, part_id, part_type, ok, measured_at_utc, total_len_mm, bc_len_mm, status"
                ") VALUES ("
                " :measurement_uuid, :part_id, :part_type, :ok, :measured_at_utc, :total_len_mm, :bc_len_mm, :status"
                ");");
            q.bindValue(":measurement_uuid", r.measurement_uuid);
            q.bindValue(":part_id", r.part_id);
            q.bindValue(":part_type", r.part_type);
            q.bindValue(":ok", r.ok ? 1 : 0);
            q.bindValue(":measured_at_utc", r.measured_at_utc);
            q.bindValue(":total_len_mm", r.total_len_mm);

            // A型 bc_len_mm 建议写NULL
            if (r.part_type == "A")
                q.bindValue(":bc_len_mm", QVariant());
            else
                q.bindValue(":bc_len_mm", r.bc_len_mm);

            q.bindValue(":status", r.status);

            if (!q.exec())
            {
                db_.rollback();
                if (err)
                    *err = q.lastError().text();
                return false;
            }
        }

        // 2) raw_file_index
        {
            QSqlQuery q(db_);
            q.prepare(
                "INSERT INTO raw_file_index("
                " measurement_uuid, file_path, file_size_bytes, format_version, file_crc32, "
                " chunk_mask, scan_kind, main_channels, rings, points_per_ring, angle_step_deg, meta_json, created_at_utc"
                ") VALUES ("
                " :measurement_uuid, :file_path, :file_size_bytes, :format_version, :file_crc32, "
                " :chunk_mask, :scan_kind, :main_channels, :rings, :points_per_ring, :angle_step_deg, :meta_json, :created_at_utc"
                ");");

            q.bindValue(":measurement_uuid", r.measurement_uuid);
            q.bindValue(":file_path", raw.final_path);
            q.bindValue(":file_size_bytes", QVariant::fromValue<qulonglong>(raw.file_size_bytes));
            q.bindValue(":format_version", raw.format_version);
            q.bindValue(":file_crc32", raw.file_crc32);

            q.bindValue(":chunk_mask", QVariant::fromValue<qulonglong>(raw.chunk_mask));
            q.bindValue(":scan_kind", raw.scan_kind);
            q.bindValue(":main_channels", raw.main_channels);
            q.bindValue(":rings", raw.rings);
            q.bindValue(":points_per_ring", raw.points_per_ring);
            q.bindValue(":angle_step_deg", raw.angle_step_deg);
            q.bindValue(":meta_json", raw.meta_json);
            q.bindValue(":created_at_utc", r.measured_at_utc);

            if (!q.exec())
            {
                db_.rollback();
                if (err)
                    *err = q.lastError().text();
                return false;
            }
        }

        if (!db_.commit())
        {
            if (err)
                *err = db_.lastError().text();
            return false;
        }
        return true;
    }

    // 查询：measure_result LEFT JOIN mes_outbox
    QVector<core::MesUploadRow> core::Db::queryMesUploadRows(const MesUploadFilter &f, int limit, QString *err)
    {
        QVector<MesUploadRow> out;
        QSqlQuery q(db_);

        QString sql =
            "SELECT "
            " r.measurement_uuid, r.part_id, r.part_type, r.ok, r.measured_at_utc, r.total_len_mm, r.bc_len_mm, "
            " o.status AS mes_status, o.attempt_count, o.last_error, o.updated_at_utc "
            "FROM measure_result r "
            "LEFT JOIN mes_outbox o "
            "  ON o.measurement_uuid = r.measurement_uuid AND o.event_type='MEASURE_RESULT_READY' "
            "WHERE r.status='READY' "
            "  AND r.measured_at_utc >= :from_utc AND r.measured_at_utc <= :to_utc ";

        if (!f.part_id_like.trimmed().isEmpty())
            sql += " AND r.part_id LIKE :part_id_like ";
        if (!f.part_type.trimmed().isEmpty())
            sql += " AND r.part_type = :part_type ";
        if (f.ok_filter == 0 || f.ok_filter == 1)
            sql += " AND r.ok = :ok ";
        if (!f.mes_status.trimmed().isEmpty())
        {
            if (f.mes_status == "NOT_QUEUED")
                sql += " AND o.id IS NULL ";
            else
                sql += " AND o.status = :mes_status ";
        }

        sql += " ORDER BY r.measured_at_utc DESC LIMIT :limit;";

        q.prepare(sql);
        q.bindValue(":from_utc", f.from_utc);
        q.bindValue(":to_utc", f.to_utc);
        if (!f.part_id_like.trimmed().isEmpty())
            q.bindValue(":part_id_like", "%" + f.part_id_like + "%");
        if (!f.part_type.trimmed().isEmpty())
            q.bindValue(":part_type", f.part_type);
        if (f.ok_filter == 0 || f.ok_filter == 1)
            q.bindValue(":ok", f.ok_filter);
        if (!f.mes_status.trimmed().isEmpty() && f.mes_status != "NOT_QUEUED")
            q.bindValue(":mes_status", f.mes_status);
        q.bindValue(":limit", limit);

        if (!q.exec())
        {
            if (err)
                *err = q.lastError().text();
            return out;
        }

        while (q.next())
        {
            MesUploadRow r;
            r.measurement_uuid = q.value(0).toString();
            r.part_id = q.value(1).toString();
            r.part_type = q.value(2).toString();
            r.ok = q.value(3).toInt() != 0;
            r.measured_at_utc = q.value(4).toDateTime();
            r.total_len_mm = q.value(5).isNull() ? 0.0 : q.value(5).toDouble();
            r.bc_len_mm = q.value(6).isNull() ? 0.0 : q.value(6).toDouble();

            // outbox may be NULL
            if (q.value(7).isNull())
            {
                r.mes_status = "NOT_QUEUED";
                r.attempt_count = 0;
            }
            else
            {
                r.mes_status = q.value(7).toString();
                r.attempt_count = q.value(8).toInt();
                r.last_error = q.value(9).toString();
                r.mes_updated_at_utc = q.value(10).toDateTime();
            }

            out.push_back(r);
        }

        return out;
    }

    // 入队：若已存在 outbox：SENT -> 拒绝；其他 -> 重置为 PENDING；不存在 -> 生成 payload + INSERT
    bool core::Db::queueMesUploadByUuid(const QString &uuid, QString *err)
    {
        // 1) 检查 outbox 是否存在
        QSqlQuery q(db_);
        q.prepare("SELECT id, status FROM mes_outbox WHERE measurement_uuid=:u AND event_type='MEASURE_RESULT_READY' LIMIT 1;");
        q.bindValue(":u", uuid);
        if (!q.exec())
        {
            if (err)
                *err = q.lastError().text();
            return false;
        }

        if (q.next())
        {
            const auto status = q.value(1).toString();
            if (status == "SENT")
            {
                if (err)
                    *err = "Already SENT; skip.";
                return false;
            }
            QSqlQuery u(db_);
            u.prepare(
                "UPDATE mes_outbox "
                "SET status='PENDING', next_retry_at_utc=UTC_TIMESTAMP(3), updated_at_utc=UTC_TIMESTAMP(3) "
                "WHERE measurement_uuid=:u AND event_type='MEASURE_RESULT_READY';");
            u.bindValue(":u", uuid);
            if (!u.exec())
            {
                if (err)
                    *err = u.lastError().text();
                return false;
            }
            return true;
        }

        // 2) 不存在：从 DB 读 measure_result + raw_file_index，构建 payload_json
        core::MeasureResult mr;
        {
            QSqlQuery r(db_);
            r.prepare(
                "SELECT measurement_uuid, part_id, part_type, ok, measured_at_utc, total_len_mm, bc_len_mm, status "
                "FROM measure_result WHERE measurement_uuid=:u LIMIT 1;");
            r.bindValue(":u", uuid);
            if (!r.exec())
            {
                if (err)
                    *err = r.lastError().text();
                return false;
            }
            if (!r.next())
            {
                if (err)
                    *err = "measure_result not found for uuid";
                return false;
            }

            mr.measurement_uuid = r.value(0).toString();
            mr.part_id = r.value(1).toString();
            mr.part_type = r.value(2).toString();
            mr.ok = r.value(3).toInt() != 0;
            mr.measured_at_utc = r.value(4).toDateTime();
            mr.total_len_mm = r.value(5).isNull() ? 0.0 : r.value(5).toDouble();
            mr.bc_len_mm = r.value(6).isNull() ? 0.0 : r.value(6).toDouble();
            mr.status = r.value(7).toString();
        }

        core::RawWriteInfoV2 raw;
        {
            QSqlQuery r(db_);
            r.prepare(
                "SELECT file_path, file_size_bytes, format_version, file_crc32, chunk_mask, scan_kind, main_channels,"
                " rings, points_per_ring, angle_step_deg, meta_json "
                "FROM raw_file_index WHERE measurement_uuid=:u LIMIT 1;");
            r.bindValue(":u", uuid);
            if (!r.exec())
            {
                if (err)
                    *err = r.lastError().text();
                return false;
            }
            if (!r.next())
            {
                if (err)
                    *err = "raw_file_index not found for uuid";
                return false;
            }

            raw.final_path = r.value(0).toString();
            raw.file_size_bytes = r.value(1).toULongLong();
            raw.format_version = r.value(2).toInt();
            raw.file_crc32 = r.value(3).toULongLong();
            raw.chunk_mask = r.value(4).toULongLong();
            raw.scan_kind = r.value(5).toString();
            raw.main_channels = r.value(6).toInt();
            raw.rings = r.value(7).toInt();
            raw.points_per_ring = r.value(8).toInt();
            raw.angle_step_deg = r.value(9).toFloat();
            raw.meta_json = r.value(10).toString();
        }

        const QString payload = core::buildMesPayloadV1(mr, raw);

        QSqlQuery ins(db_);
        ins.prepare(
            "INSERT INTO mes_outbox("
            " measurement_uuid, event_type, payload_json,"
            " status, attempt_count, next_retry_at_utc,"
            " created_at_utc, updated_at_utc"
            ") VALUES ("
            " :u, 'MEASURE_RESULT_READY', :p,"
            " 'PENDING', 0, UTC_TIMESTAMP(3), UTC_TIMESTAMP(3), UTC_TIMESTAMP(3)"
            ");");
        ins.bindValue(":u", uuid);
        ins.bindValue(":p", payload);
        if (!ins.exec())
        {
            if (err)
                *err = ins.lastError().text();
            return false;
        }

        return true;
    }

    int core::Db::retryFailed(const QVector<QString> &uuids, QString *err)
    {
        if (uuids.isEmpty())
            return 0;

        // 简单实现：逐条 update（MVP够用）
        int cnt = 0;
        for (const auto &u : uuids)
        {
            QSqlQuery q(db_);
            q.prepare(
                "UPDATE mes_outbox SET status='PENDING', next_retry_at_utc=UTC_TIMESTAMP(3), updated_at_utc=UTC_TIMESTAMP(3) "
                "WHERE measurement_uuid=:u AND event_type='MEASURE_RESULT_READY' AND status='FAILED';");
            q.bindValue(":u", u);
            if (!q.exec())
            {
                if (err)
                    *err = q.lastError().text();
                return cnt;
            }
            cnt += q.numRowsAffected();
        }
        return cnt;
    }

    bool core::Db::resetStaleSending(int stale_seconds, QString *err)
    {
        QSqlQuery q(db_);
        q.prepare(
            "UPDATE mes_outbox SET status='FAILED', updated_at_utc=UTC_TIMESTAMP(3), last_error=IFNULL(last_error,'') "
            "WHERE status='SENDING' AND updated_at_utc < (UTC_TIMESTAMP(3) - INTERVAL :s SECOND);");
        q.bindValue(":s", stale_seconds);
        if (!q.exec())
        {
            if (err)
                *err = q.lastError().text();
            return false;
        }
        return true;
    }

    bool core::Db::fetchNextDueOutbox(MesOutboxTask *task, QString *err)
    {
        if (!task)
            return false;
        QSqlQuery q(db_);
        q.prepare(
            "SELECT id, measurement_uuid, payload_json, attempt_count "
            "FROM mes_outbox "
            "WHERE event_type='MEASURE_RESULT_READY' "
            "  AND status IN ('PENDING','FAILED') "
            "  AND next_retry_at_utc <= UTC_TIMESTAMP(3) "
            "ORDER BY id ASC LIMIT 1;");
        if (!q.exec())
        {
            if (err)
                *err = q.lastError().text();
            return false;
        }
        if (!q.next())
            return false;

        task->id = q.value(0).toULongLong();
        task->measurement_uuid = q.value(1).toString();
        task->payload_json = q.value(2).toString();
        task->attempt_count = q.value(3).toInt();
        return true;
    }

    bool core::Db::markOutboxSending(quint64 id, QString *err)
    {
        QSqlQuery q(db_);
        q.prepare("UPDATE mes_outbox SET status='SENDING', updated_at_utc=UTC_TIMESTAMP(3) WHERE id=:id;");
        q.bindValue(":id", QVariant::fromValue<qulonglong>(id));
        if (!q.exec())
        {
            if (err)
                *err = q.lastError().text();
            return false;
        }
        return true;
    }

    bool core::Db::markOutboxSent(quint64 id, int http_code, const QString &resp, QString *err)
    {
        QSqlQuery q(db_);
        q.prepare(
            "UPDATE mes_outbox SET "
            " status='SENT', last_http_code=:c, last_response_body=:r, last_error=NULL,"
            " updated_at_utc=UTC_TIMESTAMP(3) "
            "WHERE id=:id;");
        q.bindValue(":c", http_code);
        q.bindValue(":r", resp.left(4000)); // 防止过大
        q.bindValue(":id", QVariant::fromValue<qulonglong>(id));
        if (!q.exec())
        {
            if (err)
                *err = q.lastError().text();
            return false;
        }
        return true;
    }

    bool core::Db::markOutboxFailed(quint64 id, int http_code, const QString &resp,
                                    const QString &error, int next_retry_seconds, QString *err)
    {
        QSqlQuery q(db_);
        q.prepare(
            "UPDATE mes_outbox SET "
            " status='FAILED', attempt_count=attempt_count+1, "
            " last_http_code=:c, last_response_body=:r, last_error=:e, "
            " next_retry_at_utc=(UTC_TIMESTAMP(3) + INTERVAL :s SECOND), "
            " updated_at_utc=UTC_TIMESTAMP(3) "
            "WHERE id=:id;");
        q.bindValue(":c", http_code);
        q.bindValue(":r", resp.left(4000));
        q.bindValue(":e", error.left(2000));
        q.bindValue(":s", next_retry_seconds);
        q.bindValue(":id", QVariant::fromValue<qulonglong>(id));
        if (!q.exec())
        {
            if (err)
                *err = q.lastError().text();
            return false;
        }
        return true;
    }

} // namespace core
