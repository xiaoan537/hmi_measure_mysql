#include "core/config.hpp"  // 包含配置头文件，配置数据结构定义
#include <QSettings>        // 包含 QSettings 类，QT 的配置文件读取类，用于读取 INI 配置文件

namespace core
{

    // 函数实现，用于从 INI 文件加载应用配置
    AppConfig loadConfigIni(const QString &iniPath)
    {
        QSettings s(iniPath, QSettings::IniFormat);     // 创建 QSetting 对象读取 INI 文件。QSettings 说明：Qt 的官方配置文件管理类，支持 INI 格式的键值对存储
        AppConfig c;                                    // 声明返回的配置对象，创建 AppConfig 对象以存储加载的配置

        // 数据库配置解析
        // 进入 "db" 配置组，开始读取数据库相关配置
        s.beginGroup("db");                                     
        c.db.driver = s.value("driver", "QMYSQL").toString();   // 读取数据库驱动，默认值为 "QMYSQL"
        c.db.host = s.value("host", "localhost").toString();
        c.db.port = s.value("port", 3306).toInt();
        c.db.name = s.value("name").toString();                 // 数据库名称，必须配置
        c.db.user = s.value("user").toString();
        c.db.pass = s.value("pass").toString();
        c.db.options = s.value("options", "MYSQL_OPT_RECONNECT=1").toString();  
        s.endGroup();                                           // 结束 "db" 配置组

        // 路径配置解析
        // 进入 "paths" 配置组，开始读取路径相关配置
        s.beginGroup("paths");
        c.paths.data_root = s.value("data_root", "./data").toString();
        c.paths.raw_dir = s.value("raw_dir", "./data/raw").toString();
        c.paths.log_dir = s.value("log_dir", "./data/logs").toString();
        s.endGroup();                                           // 结束 "paths" 配置组

        // MES 配置解析
        s.beginGroup("mes");
        c.mes.enabled = (s.value("enabled", 0).toInt() != 0);
        c.mes.manual_enabled = (s.value("manual_enabled", 1).toInt() != 0);
        c.mes.auto_enabled = (s.value("auto_enabled", 1).toInt() != 0);
        c.mes.auto_interval_ms = s.value("auto_interval_ms", 1000).toInt();
        c.mes.url = s.value("url", "").toString();
        c.mes.auth_token = s.value("auth_token", "").toString();
        c.mes.timeout_ms = s.value("timeout_ms", 5000).toInt();
        c.mes.retry_base_seconds = s.value("retry_base_seconds", 30).toInt();
        c.mes.retry_max_seconds = s.value("retry_max_seconds", 21600).toInt();
        s.endGroup();

        // 点阵扫描配置解析（由上位机侧配置，与 PLC 约定一致）
        // A 型：CONF 4ch
        s.beginGroup("scan_a");
        c.scan_a.rings = s.value("rings", 1).toInt();
        c.scan_a.points_per_ring = s.value("points_per_ring", 72).toInt();
        c.scan_a.angle_step_deg = s.value("angle_step_deg", 5.0).toDouble();
        c.scan_a.order_code = (quint16)s.value("order_code", 1).toInt();
        s.endGroup();

        // B 型：RUNO 2ch
        s.beginGroup("scan_b");
        c.scan_b.rings = s.value("rings", 1).toInt();
        c.scan_b.points_per_ring = s.value("points_per_ring", 72).toInt();
        c.scan_b.angle_step_deg = s.value("angle_step_deg", 5.0).toDouble();
        c.scan_b.order_code = (quint16)s.value("order_code", 1).toInt();
        s.endGroup();

        return c;
    }

} // namespace core
