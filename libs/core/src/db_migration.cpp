#include "core/db.hpp"

#include <QVariant>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>

namespace core {

bool Db::ensureSchema(QString *err) {

  // 创建一个与当前数据库连接 db_ 关联的SQL查询对象QSqlQuery
  // q(db_);该对象用于执行SQL语句（如创建表、插入数据等）。
  QSqlQuery q(db_);

  // Lambda表达式在Db::ensureSchema方法中定义，作为一个错误处理的辅助函数
  auto fail = [&](const QString &e) {
    if (err)
      *err = e;
    return false;
  };

  /*
  这段代码实现了一个简单的数据库模式迁移（Schema
  Migration）机制，用于确保数据库结构在应用启动时处于最新版本。
  它通过一个名为schema_migrations的表来记录当前的数据库版本，并依次应用缺失的迁移脚本。
  创建表schema_migrations（如果不存在），用于记录已应用的迁移版本号（version）和应用时间（applied_at）。
  DATETIME(3)表示时间精确到毫秒，ENGINE=InnoDB指定存储引擎，utf8mb4支持完整的Unicode字符集。
  */
  // 0) 创建迁移记录表
  if (!q.exec("CREATE TABLE IF NOT EXISTS schema_migrations ("
              "  version INT NOT NULL PRIMARY KEY,"
              "  applied_at DATETIME(3) NOT NULL"
              ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;")) {
    return fail(q.lastError().text());
  }

  /*
  定义了一个 lambda 函数，用于查询数据库表 schema_migrations 中的最大版本号。
  这是典型的数据库迁移系统中的版本追踪机制。
  IFNULL 函数：如果 MAX(version) 返回 NULL（表为空），则返回 0
  错误处理：查询失败返回 -1
  */
  // 用于查询数据库表 schema_migrations 中的最大版本号
  auto currentVersion = [&]() -> int {
    QSqlQuery t(db_);
    if (!t.exec("SELECT IFNULL(MAX(version), 0) FROM schema_migrations;"))
      return -1;
    if (!t.next()) // 将查询结果指针移动到第一条记录
      return -1;
    return t.value(0).toInt(); // 获取第一列的值并转换为整数返回
  };

  /*
  定义了一个 lambda 函数，用于检查指定表中的列是否存在。
  通过查询 MySQL 的系统元数据表 INFORMATION_SCHEMA.COLUMNS 来实现，这是数据库
  schema 检查的标准方法。 1、准备查询：创建一个 QSqlQuery对象 t并关联到成员变量
  db_所代表的数据库连接。 使用 prepare()函数准备一条参数化 SQL
  语句。这条语句查询的是数据库的
  INFORMATION_SCHEMA.COLUMNS系统视图，这个视图包含了数据库中所有表的列信息。WHERE子句使用
  DATABASE()函数限定当前数据库，并通过占位符 :t和 :c来指定具体的表名和列名。
  2、绑定参数：通过 bindValue()函数将Lambda表达式的输入参数 table和
  column分别绑定到SQL语句中的 :t和
  :c占位符上。这是推荐的做法，可以防止SQL注入攻击，并避免手动拼接字符串时可能出现的格式错误。
  3、执行与判断：
  t.exec()执行SQL查询。如果执行失败，直接返回 false。
  t.next()将结果集指针移动到第一条记录。因为
  COUNT(*)查询总是返回一行数据，如果这一步失败，说明结果异常，返回 false。
  t.value(0)获取查询结果第一列（索引为0）的值，也就是
  COUNT(*)的结果。如果这个值大于0，表示至少存在一行符合条件的记录，即列存在，函数返回
  true；否则返回 false。
  */
  // 用于检查指定表中的列是否存在
  auto columnExists = [&](const QString &table, const QString &column) -> bool {
    QSqlQuery t(db_);
    t.prepare("SELECT COUNT(*) "
              "FROM INFORMATION_SCHEMA.COLUMNS "
              "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :t AND "
              "COLUMN_NAME = :c;");
    t.bindValue(":t", table);
    t.bindValue(":c", column);
    if (!t.exec())
      return false;
    if (!t.next())
      return false;
    return t.value(0).toInt() > 0;
  };

  // 用于将迁移版本号写入 schema_migrations 表
  auto applyVersion = [&](int v) -> bool {
    QSqlQuery t(db_);
    t.prepare("INSERT INTO schema_migrations(version, applied_at) VALUES(:v, "
              "NOW(3));");
    t.bindValue(":v", v);
    if (!t.exec())
      return false;
    return true;
  };

  // 用于执行 SQL 语句并在失败时记录错误信息
  auto execOrFail = [&](const QString &sql) -> bool {
    QSqlQuery t(db_);
    if (!t.exec(sql)) {
      if (err)
        *err = t.lastError().text();
      return false;
    }
    return true;
  };

  // 用于检查指定表中的索引是否存在
  auto indexExists = [&](const QString &table,
                         const QString &indexName) -> bool {
    QSqlQuery t(db_);
    t.prepare("SELECT COUNT(*) "
              "FROM INFORMATION_SCHEMA.STATISTICS "
              "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = :t AND "
              "INDEX_NAME = :i;");
    t.bindValue(":t", table);
    t.bindValue(":i", indexName);
    if (!t.exec())
      return false;
    if (!t.next())
      return false;
    return t.value(0).toInt() > 0;
  };

  int cur = currentVersion();
  if (cur < 0)
    return fail("Failed to read schema_migrations");

  // v1: measure_result + MES view
  if (cur < 1) {
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
    即使
    measure_result表已经存在，语句也会安静地跳过创建步骤，并返回一个提示而非错误，不会影响后续任何操作。
    这使得它在初始化脚本或应用程序启动脚本中特别有用，可以放心地重复执行。

    表字段说明：
    id bigint unsigned not null auto_increment primary key:
        定义一个名为 id 的列，类型为无符号大整数(bigint unsigned)，
        该列不能为空(not null)，并且是自动递增(auto_increment)的主键(primary
    key)。 确保每条记录有唯一标识(id)，防止重复插入。 measurement_uuid char(36)
    not null: 定义一个名为 measurement_uuid
    的列，类型为字符型(char)，长度为36个字符，存储UUID值。 该列不能为空(not
    null)。用于唯一标识每次测量。 part_id varchar(64) not null: 定义一个名为
    part_id
    的列，类型为字符串型(varchar,可变长度字符串)，长度为64个字符，存储零件ID。
        该列不能为空(not null)。用于标识测量的具体零件。
    part_type char(1) not null:
        定义一个名为 part_type
    的列，类型为字符型(char)，长度为1个字符，存储零件类型。 该列不能为空(not
    null)。用于标识测量的零件类型（如"A"、"B"等）。 ok tinyint(1) not null:
        定义一个名为 ok 的列，类型为小整数(tinyint)，长度为1个字节，存储布尔值。
        该列不能为空(not
    null)。用于标识测量结果是否通过（1表示通过，0表示未通过）。
        确保测量结果的有效性（通过或未通过）。
    measured_at_utc datetime(3) not null:
        定义一个名为 measured_at_utc
    的列，类型为日期时间型(datetime)，精度为3毫秒(3)，存储测量时间。
        该列不能为空(not null)。用于记录测量的具体时间点。
    total_len_mm double null:
        定义一个名为 total_len_mm
    的列，类型为双精度浮点数(double)，可以为空(null)，存储总长度（单位：毫米）。
    status varchar(16) not null:
        定义一个名为 status
    的列，类型为字符串型(varchar)，长度为16个字符，存储测量状态。
        该列不能为空(not
    null)。用于标识测量的当前状态（如"READY"、"WRITING"等）。 unique key
    uk_measurement_uuid (measurement_uuid): 定义一个唯一键(unique key)，名称为
    uk_measurement_uuid，基于 measurement_uuid 列。 确保每次测量的 UUID
    都是唯一的，防止重复测量。

    索引说明：
    key idx_measured_at_utc (measured_at_utc):
        定义一个普通键(key)，名称为 idx_measured_at_utc，基于 measured_at_utc
    列。 用于加速按测量时间范围查询测量结果。 key idx_part_id (part_id):
        定义一个普通键(key)，名称为 idx_part_id，基于 part_id 列。
        用于加速按零件ID查询测量结果。

    表选项：
    engine=InnoDB default charset=utf8mb4;
        指定表使用 InnoDB 存储引擎，支持事务和外键约束，并设置默认字符集为
    utf8mb4。支持完整的 Unicode 字符。
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

    if (!q.exec(ddl)) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    // MES view: only READY
    const char *view_ddl = "CREATE OR REPLACE VIEW mes_v_measure_result AS "
                           "SELECT "
                           "  measurement_uuid, part_id, part_type, ok, "
                           "measured_at_utc, total_len_mm, bc_len_mm, status "
                           "FROM measure_result "
                           "WHERE status='READY';";

    if (!q.exec(view_ddl)) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!applyVersion(1)) {
      db_.rollback();
      return fail("Failed to write schema_migrations v1");
    }

    if (!db_.commit())
      return fail(db_.lastError().text());
    cur = 1;
  }

  // v2: raw_file_index (HMIRAW02)
  if (cur < 2) {
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

    if (!q.exec(ddl2)) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!applyVersion(2)) {
      db_.rollback();
      return fail("Failed to write schema_migrations v2");
    }

    if (!db_.commit())
      return fail(db_.lastError().text());
    cur = 2;
  }

  // v3: ensure bc_len_mm exists + recreate view (for older DBs that created v1
  // without bc_len_mm)
  if (cur < 3) {
    if (!db_.transaction())
      return fail(db_.lastError().text());

    if (!columnExists("measure_result", "bc_len_mm")) {
      if (!q.exec("ALTER TABLE measure_result ADD COLUMN bc_len_mm DOUBLE NULL "
                  "AFTER total_len_mm;")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }

    // recreate view to include bc_len_mm
    if (!q.exec("DROP VIEW IF EXISTS mes_v_measure_result;")) {
      db_.rollback();
      return fail(q.lastError().text());
    }
    if (!q.exec("CREATE VIEW mes_v_measure_result AS "
                "SELECT "
                "  measurement_uuid, part_id, part_type, ok, measured_at_utc, "
                "total_len_mm, bc_len_mm, status "
                "FROM measure_result "
                "WHERE status='READY';")) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!applyVersion(3)) {
      db_.rollback();
      return fail("Failed to write schema_migrations v3");
    }

    if (!db_.commit())
      return fail(db_.lastError().text());
    cur = 3;
  }

  // ---- v4: mes_outbox (manual queue + reliable delivery)
  if (cur < 4) {
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
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;")) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!q.exec("INSERT INTO schema_migrations(version, applied_at) VALUES(4, "
                "NOW(3));")) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!db_.commit())
      return fail(db_.lastError().text());
    cur = 4;
  }

  // v5: new MES/task/cycle/measurement schema
  if (cur < 5) {
    if (!db_.transaction())
      return fail(db_.lastError().text());

    const char *ddl_mes_task =
        "CREATE TABLE IF NOT EXISTS mes_task ("
        "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "  task_uuid CHAR(36) NOT NULL,"
        "  task_card_no VARCHAR(64) NOT NULL,"
        "  task_type VARCHAR(16) NOT NULL,"
        "  mes_task_code VARCHAR(64) NULL,"
        "  op_user_id VARCHAR(64) NULL,"
        "  tech_id VARCHAR(64) NULL,"
        "  status VARCHAR(16) NOT NULL,"
        "  item_total INT NOT NULL DEFAULT 0,"
        "  item_finished INT NOT NULL DEFAULT 0,"
        "  note TEXT NULL,"
        "  mes_payload_json LONGTEXT NULL,"
        "  created_at_utc DATETIME(3) NOT NULL,"
        "  updated_at_utc DATETIME(3) NOT NULL,"
        "  UNIQUE KEY uk_mes_task_uuid (task_uuid),"
        "  UNIQUE KEY uk_mes_task_card_no (task_card_no),"
        "  KEY idx_mes_task_status (status, updated_at_utc)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    const char *ddl_mes_task_item =
        "CREATE TABLE IF NOT EXISTS mes_task_item ("
        "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "  task_id BIGINT UNSIGNED NOT NULL,"
        "  item_uuid CHAR(36) NOT NULL,"
        "  part_id VARCHAR(64) NOT NULL,"
        "  item_seq INT NULL,"
        "  slot_hint SMALLINT NULL,"
        "  required_rounds TINYINT NOT NULL DEFAULT 1,"
        "  current_round TINYINT NOT NULL DEFAULT 0,"
        "  final_disposition VARCHAR(16) NOT NULL DEFAULT 'PENDING',"
        "  last_measurement_id BIGINT UNSIGNED NULL,"
        "  remark TEXT NULL,"
        "  created_at_utc DATETIME(3) NOT NULL,"
        "  updated_at_utc DATETIME(3) NOT NULL,"
        "  UNIQUE KEY uk_mes_task_item_uuid (item_uuid),"
        "  UNIQUE KEY uk_mes_task_item_task_part (task_id, part_id),"
        "  KEY idx_mes_task_item_task (task_id, item_seq),"
        "  KEY idx_mes_task_item_part (part_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    const char *ddl_plc_cycle =
        "CREATE TABLE IF NOT EXISTS plc_cycle ("
        "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "  cycle_uuid CHAR(36) NOT NULL,"
        "  meas_seq BIGINT UNSIGNED NOT NULL,"
        "  part_type CHAR(1) NOT NULL,"
        "  item_count TINYINT NOT NULL,"
        "  machine_state SMALLINT NULL,"
        "  step_state SMALLINT NULL,"
        "  source_mode VARCHAR(16) NOT NULL,"
        "  mailbox_header_json LONGTEXT NULL,"
        "  mailbox_meta_json LONGTEXT NULL,"
        "  measured_at_utc DATETIME(3) NOT NULL,"
        "  acked_at_utc DATETIME(3) NULL,"
        "  created_at_utc DATETIME(3) NOT NULL,"
        "  UNIQUE KEY uk_plc_cycle_uuid (cycle_uuid),"
        "  UNIQUE KEY uk_plc_cycle_meas_seq (meas_seq),"
        "  KEY idx_plc_cycle_time (measured_at_utc)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    const char *ddl_plc_cycle_item =
        "CREATE TABLE IF NOT EXISTS plc_cycle_item ("
        "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "  plc_cycle_id BIGINT UNSIGNED NOT NULL,"
        "  item_index TINYINT NOT NULL,"
        "  slot_index SMALLINT NULL,"
        "  part_id VARCHAR(64) NULL,"
        "  result_ok TINYINT(1) NULL,"
        "  fail_reason_code VARCHAR(32) NULL,"
        "  fail_reason_text VARCHAR(255) NULL,"
        "  is_valid TINYINT(1) NOT NULL DEFAULT 1,"
        "  measurement_id BIGINT UNSIGNED NULL,"
        "  created_at_utc DATETIME(3) NOT NULL,"
        "  UNIQUE KEY uk_plc_cycle_item (plc_cycle_id, item_index),"
        "  KEY idx_plc_cycle_item_part (part_id),"
        "  KEY idx_plc_cycle_item_slot (slot_index)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    const char *ddl_measurement =
        "CREATE TABLE IF NOT EXISTS measurement ("
        "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "  measurement_uuid CHAR(36) NOT NULL,"
        "  plc_cycle_id BIGINT UNSIGNED NULL,"
        "  plc_cycle_item_id BIGINT UNSIGNED NULL,"
        "  task_id BIGINT UNSIGNED NULL,"
        "  task_item_id BIGINT UNSIGNED NULL,"
        "  part_id VARCHAR(64) NOT NULL,"
        "  part_type CHAR(1) NOT NULL,"
        "  slot_id VARCHAR(32) NULL,"
        "  slot_index SMALLINT NULL,"
        "  item_index TINYINT NULL,"
        "  measure_mode VARCHAR(16) NOT NULL,"
        "  measure_round TINYINT NOT NULL,"
        "  result_judgement VARCHAR(16) NOT NULL,"
        "  upload_kind VARCHAR(32) NULL,"
        "  measured_at_utc DATETIME(3) NOT NULL,"
        "  operator_id VARCHAR(64) NULL,"
        "  review_status VARCHAR(16) NOT NULL DEFAULT 'PENDING',"
        "  reviewer_id VARCHAR(64) NULL,"
        "  reviewed_at_utc DATETIME(3) NULL,"
        "  review_note TEXT NULL,"
        "  fail_reason_code VARCHAR(32) NULL,"
        "  fail_reason_text VARCHAR(255) NULL,"
        "  status VARCHAR(16) NOT NULL,"
        "  created_at_utc DATETIME(3) NOT NULL,"
        "  updated_at_utc DATETIME(3) NOT NULL,"
        "  UNIQUE KEY uk_measurement_uuid (measurement_uuid),"
        "  KEY idx_measurement_part (part_id, measure_mode, measure_round),"
        "  KEY idx_measurement_task (task_id, task_item_id),"
        "  KEY idx_measurement_cycle (plc_cycle_id, plc_cycle_item_id),"
        "  KEY idx_measurement_status (status, review_status),"
        "  KEY idx_measurement_time (measured_at_utc)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    const char *ddl_measurement_result =
        "CREATE TABLE IF NOT EXISTS measurement_result ("
        "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "  measurement_id BIGINT UNSIGNED NOT NULL,"
        "  total_len_mm DOUBLE NULL,"
        "  ad_len_mm DOUBLE NULL,"
        "  bc_len_mm DOUBLE NULL,"
        "  id_left_mm DOUBLE NULL,"
        "  id_right_mm DOUBLE NULL,"
        "  od_left_mm DOUBLE NULL,"
        "  od_right_mm DOUBLE NULL,"
        "  runout_left_mm DOUBLE NULL,"
        "  runout_right_mm DOUBLE NULL,"
        "  tolerance_json LONGTEXT NULL,"
        "  extra_json LONGTEXT NULL,"
        "  created_at_utc DATETIME(3) NOT NULL,"
        "  UNIQUE KEY uk_measurement_result_mid (measurement_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    const char *ddl_mes_report =
        "CREATE TABLE IF NOT EXISTS mes_report ("
        "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "  report_uuid CHAR(36) NOT NULL,"
        "  measurement_id BIGINT UNSIGNED NOT NULL,"
        "  task_id BIGINT UNSIGNED NULL,"
        "  task_item_id BIGINT UNSIGNED NULL,"
        "  report_type VARCHAR(32) NOT NULL,"
        "  interface_code VARCHAR(64) NULL,"
        "  business_key VARCHAR(128) NULL,"
        "  need_upload TINYINT(1) NOT NULL DEFAULT 1,"
        "  report_status VARCHAR(16) NOT NULL,"
        "  approved_by VARCHAR(64) NULL,"
        "  approved_at_utc DATETIME(3) NULL,"
        "  payload_json LONGTEXT NULL,"
        "  response_json LONGTEXT NULL,"
        "  created_at_utc DATETIME(3) NOT NULL,"
        "  updated_at_utc DATETIME(3) NOT NULL,"
        "  UNIQUE KEY uk_mes_report_uuid (report_uuid),"
        "  UNIQUE KEY uk_mes_report_measure_type (measurement_id, report_type),"
        "  KEY idx_mes_report_status (report_status, updated_at_utc)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    const char *ddl_mes_api_log =
        "CREATE TABLE IF NOT EXISTS mes_api_log ("
        "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "  request_id CHAR(36) NOT NULL,"
        "  interface_code VARCHAR(64) NOT NULL,"
        "  business_type VARCHAR(32) NOT NULL,"
        "  ref_report_id BIGINT UNSIGNED NULL,"
        "  ref_task_id BIGINT UNSIGNED NULL,"
        "  http_method VARCHAR(8) NOT NULL,"
        "  request_url VARCHAR(512) NOT NULL,"
        "  request_body LONGTEXT NULL,"
        "  response_code INT NULL,"
        "  response_body LONGTEXT NULL,"
        "  success TINYINT(1) NOT NULL DEFAULT 0,"
        "  error_text TEXT NULL,"
        "  created_at_utc DATETIME(3) NOT NULL,"
        "  KEY idx_mes_api_log_time (created_at_utc),"
        "  KEY idx_mes_api_log_iface (interface_code, created_at_utc)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    if (!q.exec(ddl_mes_task) || !q.exec(ddl_mes_task_item) ||
        !q.exec(ddl_plc_cycle) || !q.exec(ddl_plc_cycle_item) ||
        !q.exec(ddl_measurement) || !q.exec(ddl_measurement_result) ||
        !q.exec(ddl_mes_report) || !q.exec(ddl_mes_api_log)) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!applyVersion(5)) {
      db_.rollback();
      return fail("Failed to write schema_migrations v5");
    }

    if (!db_.commit())
      return fail(db_.lastError().text());

    cur = 5;
  }

  // v6: extend old tables for new flow
  if (cur < 6) {
    if (!db_.transaction())
      return fail(db_.lastError().text());

    if (!columnExists("raw_file_index", "measurement_id")) {
      if (!q.exec("ALTER TABLE raw_file_index ADD COLUMN measurement_id BIGINT "
                  "UNSIGNED NULL;")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }
    if (!columnExists("raw_file_index", "plc_cycle_id")) {
      if (!q.exec("ALTER TABLE raw_file_index ADD COLUMN plc_cycle_id BIGINT "
                  "UNSIGNED NULL;")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }
    if (!columnExists("raw_file_index", "raw_kind")) {
      if (!q.exec("ALTER TABLE raw_file_index ADD COLUMN raw_kind VARCHAR(16) "
                  "NULL;")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }

    // 注意：raw_file_index 现在本来就有 meta_json，不要重复加
    if (!columnExists("mes_outbox", "mes_report_id")) {
      if (!q.exec("ALTER TABLE mes_outbox ADD COLUMN mes_report_id BIGINT "
                  "UNSIGNED NULL;")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }

    if (!applyVersion(6)) {
      db_.rollback();
      return fail("Failed to write schema_migrations v6");
    }

    if (!db_.commit())
      return fail(db_.lastError().text());

    cur = 6;
  }

  // v7: compatibility views for new schema,补兼容视图，方便你过渡查询
  if (cur < 7) {
    if (!db_.transaction())
      return fail(db_.lastError().text());

    if (!q.exec("DROP VIEW IF EXISTS mes_v_measurement_ready;")) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!q.exec("CREATE VIEW mes_v_measurement_ready AS "
                "SELECT "
                "  m.measurement_uuid, m.part_id, m.part_type, "
                "  CASE WHEN m.result_judgement='OK' THEN 1 ELSE 0 END AS ok, "
                "  m.measured_at_utc, "
                "  r.total_len_mm, r.bc_len_mm, "
                "  m.status, m.review_status, m.measure_mode, m.measure_round "
                "FROM measurement m "
                "LEFT JOIN measurement_result r ON r.measurement_id = m.id "
                "WHERE m.status='READY';")) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!applyVersion(7)) {
      db_.rollback();
      return fail("Failed to write schema_migrations v7");
    }

    if (!db_.commit())
      return fail(db_.lastError().text());

    cur = 7;
  }


  // v8: measurement business-model fields + card query indexes
  if (cur < 8) {
    if (!db_.transaction())
      return fail(db_.lastError().text());

    if (!columnExists("measurement", "run_kind")) {
      if (!q.exec("ALTER TABLE measurement ADD COLUMN run_kind VARCHAR(16) NOT NULL DEFAULT 'PRODUCTION' AFTER task_item_id;")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }
    if (!columnExists("measurement", "attempt_kind")) {
      if (!q.exec("ALTER TABLE measurement ADD COLUMN attempt_kind VARCHAR(16) NOT NULL DEFAULT 'PRIMARY' AFTER measure_mode;")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }
    if (!columnExists("measurement", "fail_class")) {
      if (!q.exec("ALTER TABLE measurement ADD COLUMN fail_class VARCHAR(16) NULL AFTER result_judgement;")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }
    if (!columnExists("measurement", "is_effective")) {
      if (!q.exec("ALTER TABLE measurement ADD COLUMN is_effective TINYINT(1) NOT NULL DEFAULT 1 AFTER fail_reason_text;")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }
    if (!columnExists("measurement", "superseded_by")) {
      if (!q.exec("ALTER TABLE measurement ADD COLUMN superseded_by BIGINT UNSIGNED NULL AFTER is_effective;")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }

    if (!q.exec("ALTER TABLE measurement MODIFY COLUMN measure_mode VARCHAR(16) NULL;")) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!q.exec("UPDATE measurement SET run_kind='PRODUCTION' WHERE run_kind IS NULL OR run_kind='';")) {
      db_.rollback();
      return fail(q.lastError().text());
    }
    if (!q.exec("UPDATE measurement "
                "SET attempt_kind = CASE "
                "  WHEN UPPER(measure_mode)='RETEST' THEN 'RETEST' "
                "  ELSE 'PRIMARY' "
                "END "
                "WHERE attempt_kind IS NULL OR attempt_kind='';")) {
      db_.rollback();
      return fail(q.lastError().text());
    }
    if (!q.exec("UPDATE measurement SET is_effective=1 WHERE is_effective IS NULL;")) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!indexExists("measurement", "idx_measurement_task_time")) {
      if (!q.exec("CREATE INDEX idx_measurement_task_time ON measurement(task_id, measured_at_utc, id);")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }
    if (!indexExists("measurement", "idx_measurement_chain")) {
      if (!q.exec("CREATE INDEX idx_measurement_chain ON measurement(part_id, run_kind, measure_mode, is_effective, measured_at_utc);")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }
    if (!indexExists("measurement", "idx_measurement_superseded_by")) {
      if (!q.exec("CREATE INDEX idx_measurement_superseded_by ON measurement(superseded_by);")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }

    if (!applyVersion(8)) {
      db_.rollback();
      return fail("Failed to write schema_migrations v8");
    }

    if (!db_.commit())
      return fail(db_.lastError().text());

    cur = 8;
  }

  // v9: MES route backfill and report/outbox indexes
  if (cur < 9) {
    if (!db_.transaction())
      return fail(db_.lastError().text());

    if (!indexExists("mes_outbox", "idx_mes_outbox_report")) {
      if (!q.exec("CREATE INDEX idx_mes_outbox_report ON mes_outbox(mes_report_id, status, next_retry_at_utc);")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }
    if (!indexExists("mes_report", "idx_mes_report_measure_iface")) {
      if (!q.exec("CREATE INDEX idx_mes_report_measure_iface ON mes_report(measurement_id, interface_code);")) {
        db_.rollback();
        return fail(q.lastError().text());
      }
    }

    if (!q.exec(
            "UPDATE mes_report mr "
            "JOIN measurement m ON m.id = mr.measurement_id "
            "SET mr.interface_code = CASE UPPER(IFNULL(m.measure_mode, '')) "
            "  WHEN 'NORMAL' THEN 'MES_PROD_NORMAL_RESULT' "
            "  WHEN 'SECOND' THEN 'MES_PROD_SECOND_RESULT' "
            "  WHEN 'THIRD' THEN 'MES_PROD_THIRD_RESULT' "
            "  WHEN 'MIL' THEN 'MES_PROD_MIL_RESULT' "
            "  ELSE mr.interface_code END, "
            "mr.updated_at_utc = NOW(3) "
            "WHERE IFNULL(m.run_kind, 'PRODUCTION')='PRODUCTION' "
            "  AND (mr.interface_code IS NULL OR mr.interface_code='');")) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!q.exec(
            "UPDATE mes_outbox o "
            "JOIN measurement m ON m.measurement_uuid = o.measurement_uuid "
            "JOIN mes_report mr ON mr.id = ("
            "  SELECT mr2.id FROM mes_report mr2 WHERE mr2.measurement_id = m.id ORDER BY mr2.id DESC LIMIT 1"
            ") "
            "SET o.mes_report_id = mr.id "
            "WHERE o.mes_report_id IS NULL;")) {
      db_.rollback();
      return fail(q.lastError().text());
    }

    if (!applyVersion(9)) {
      db_.rollback();
      return fail("Failed to write schema_migrations v9");
    }

    if (!db_.commit())
      return fail(db_.lastError().text());

    cur = 9;
  }


  return true;
}


} // namespace core

// db_migration.cpp 实现了数据库迁移相关的功能，包括创建表、添加列、更新索引等。
