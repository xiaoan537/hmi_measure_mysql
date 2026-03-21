#include "core/config.hpp"  // 包含配置头文件，配置数据结构定义
#include <QSettings>        // 包含 QSettings 类，QT 的配置文件读取类，用于读取 INI 配置文件

namespace
{
    QString trimUrl(const QString &v)
    {
        return v.trimmed();
    }

    QString trimRightSlash(const QString &v)
    {
        QString s = trimUrl(v);
        while (s.endsWith('/'))
            s.chop(1);
        return s;
    }

    QString joinUrl(const QString &base, const QString &leaf)
    {
        const QString b = trimRightSlash(base);
        const QString l = leaf.trimmed();
        if (b.isEmpty())
            return QString();
        if (l.isEmpty())
            return b;
        return l.startsWith('/') ? (b + l) : (b + "/" + l);
    }

    QString normalizedInterfaceCode(const QString &v)
    {
        return v.trimmed().toUpper();
    }
}

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
        c.mes.url = trimUrl(s.value("url", "").toString());

        c.mes.sysid = trimUrl(s.value("sysid", "").toString());
        c.mes.sys_base_url = trimRightSlash(s.value("sys_base_url", "").toString());
        c.mes.sys_heartbeat_url = trimUrl(s.value("sys_heartbeat_url", "").toString());
        c.mes.sys_op_check_user_url = trimUrl(s.value("sys_op_check_user_url", "").toString());
        c.mes.sys_op_check_tech_state_url = trimUrl(s.value("sys_op_check_tech_state_url", "").toString());

        if (c.mes.sys_heartbeat_url.isEmpty())
            c.mes.sys_heartbeat_url = joinUrl(c.mes.sys_base_url, "Heartbeat");
        if (c.mes.sys_op_check_user_url.isEmpty())
            c.mes.sys_op_check_user_url = joinUrl(c.mes.sys_base_url, "OpCheckUser");
        if (c.mes.sys_op_check_tech_state_url.isEmpty())
            c.mes.sys_op_check_tech_state_url = joinUrl(c.mes.sys_base_url, "OpCheckTechState");

        c.mes.prod_normal_url = trimUrl(s.value("prod_normal_url", c.mes.url).toString());
        c.mes.prod_second_url = trimUrl(s.value("prod_second_url", "").toString());
        c.mes.prod_third_url = trimUrl(s.value("prod_third_url", "").toString());
        c.mes.prod_mil_url = trimUrl(s.value("prod_mil_url", "").toString());

        c.mes.auth_token = s.value("auth_token", "").toString();
        c.mes.heartbeat_enabled = (s.value("heartbeat_enabled", 1).toInt() != 0);
        c.mes.heartbeat_interval_ms = s.value("heartbeat_interval_ms", 10000).toInt();
        c.mes.max_time_diff_seconds = s.value("max_time_diff_seconds", 60).toInt();
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

        // PLC 通讯配置解析
        s.beginGroup("plc");
        c.plc.enabled = (s.value("enabled", 0).toInt() != 0);
        c.plc.host = s.value("host", "127.0.0.1").toString().trimmed();
        c.plc.port = s.value("port", 502).toInt();
        c.plc.server_address = s.value("server_address", 1).toInt();
        c.plc.connect_timeout_ms = s.value("connect_timeout_ms", 3000).toInt();
        c.plc.response_timeout_ms = s.value("response_timeout_ms", 1000).toInt();
        c.plc.number_of_retries = s.value("number_of_retries", 1).toInt();
        c.plc.poll_interval_ms = s.value("poll_interval_ms", 100).toInt();
        c.plc.auto_reconnect = (s.value("auto_reconnect", 1).toInt() != 0);
        c.plc.reconnect_interval_ms = s.value("reconnect_interval_ms", 2000).toInt();
        c.plc.use_fake_client = (s.value("use_fake_client", 0).toInt() != 0);
        c.plc.status_start_address = s.value("status_start_address", 0).toULongLong();
        c.plc.tray_start_address = s.value("tray_start_address", 0).toULongLong();
        c.plc.command_start_address = s.value("command_start_address", 0).toULongLong();
        c.plc.mailbox_start_address = s.value("mailbox_start_address", 0).toULongLong();
        c.plc.pc_ack_start_address = s.value("pc_ack_start_address", 0).toULongLong();
        s.endGroup();

        return c;
    }

    QString resolveMesInterfaceUrl(const MesConfig &cfg, const QString &interfaceCode)
    {
        const QString code = normalizedInterfaceCode(interfaceCode);
        if (code == QStringLiteral("MES_PROD_NORMAL_RESULT"))
            return trimUrl(cfg.prod_normal_url.isEmpty() ? cfg.url : cfg.prod_normal_url);
        if (code == QStringLiteral("MES_PROD_SECOND_RESULT"))
            return trimUrl(cfg.prod_second_url);
        if (code == QStringLiteral("MES_PROD_THIRD_RESULT"))
            return trimUrl(cfg.prod_third_url);
        if (code == QStringLiteral("MES_PROD_MIL_RESULT"))
            return trimUrl(cfg.prod_mil_url);

        if (code == QStringLiteral("MES_SYS_HEARTBEAT") || code == QStringLiteral("SYS_HEARTBEAT"))
            return trimUrl(cfg.sys_heartbeat_url);
        if (code == QStringLiteral("MES_SYS_OP_CHECK_USER") || code == QStringLiteral("SYS_OP_CHECK_USER"))
            return trimUrl(cfg.sys_op_check_user_url);
        if (code == QStringLiteral("MES_SYS_OP_CHECK_TECH_STATE") || code == QStringLiteral("SYS_OP_CHECK_TECH_STATE"))
            return trimUrl(cfg.sys_op_check_tech_state_url);

        return QString();
    }

    bool hasMesInterfaceUrl(const MesConfig &cfg, const QString &interfaceCode)
    {
        return !resolveMesInterfaceUrl(cfg, interfaceCode).trimmed().isEmpty();
    }

} // namespace core
