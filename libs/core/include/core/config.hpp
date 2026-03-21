#pragma once
#include <QString>
#include <QtGlobal>

namespace core
{

    // 数据库配置结构体，封装 MySQL 连接所需的所有参数
    struct DbConfig
    {
        QString driver;  // 数据库驱动（QMYSQL）
        QString host;    // 数据库服务器主机名或地址
        int port = 3306; // 端口号，默认 MySQL 端口
        QString name;    // 数据库名称
        QString user;    // 数据库用户名
        QString pass;    // 数据库密码
        QString options; // Qt sql connectOptions（连接选项）
    };

    // 路径配置结构体，封装应用所使用的各类文件目录路径
    struct PathConfig
    {
        QString data_root; // 数据根目录
        QString raw_dir;   // 原始数据目录
        QString log_dir;   // 日志目录
    };

    // MES 推送配置结构体
struct MesConfig
{
    bool enabled = false;          // 是否启用 MES 集成（全局）
    bool manual_enabled = true;    // 是否允许手动上传（UI 按钮）
    bool auto_enabled = true;      // 是否允许自动上传（后台定时器）
    int auto_interval_ms = 1000;   // 自动上传定时器周期（ms）

    QString url;                   // 兼容旧配置：默认/普通报工地址

    // SYS 类接口配置
    QString sysid;                 // SYSID（系统编号）
    QString sys_base_url;          // 例如 http://ip:8080/msService/public/DeviceIf/SYS
    QString sys_heartbeat_url;     // /Heartbeat
    QString sys_op_check_user_url; // /OpCheckUser
    QString sys_op_check_tech_state_url; // /OpCheckTechState

    // 生产测量报工接口地址（不同模式走不同地址）
    QString prod_normal_url;       // 普通测量
    QString prod_second_url;       // 第二次测量
    QString prod_third_url;        // 第三次测量
    QString prod_mil_url;          // 军检

    QString auth_token;            // Bearer token（可选）

    bool heartbeat_enabled = true; // 是否启用 SYS Heartbeat
    int heartbeat_interval_ms = 10000; // Heartbeat 周期（ms）
    int max_time_diff_seconds = 60; // 本机与 MES 允许的最大时间差（秒）

    int timeout_ms = 5000;         // HTTP 超时（ms）
    int retry_base_seconds = 30;   // 重试基准间隔（秒）
    int retry_max_seconds = 21600; // 最大重试间隔（秒）
};


    // 点阵扫描配置（由上位机侧配置，与 PLC 约定一致）
    // rings：转几圈（当前=1，后续可能=2）
    // points_per_ring：每圈采样点数（当前=72，后续可能变）
    // angle_step_deg：点间角度步进（例如 5.0）
    // order_code：线性展开顺序（0=legacy ch->ring->pt, 1=ring->ch->pt）
    struct ScanConfig
    {
        int rings = 1;
        int points_per_ring = 72;
        float angle_step_deg = 5.0f;
        quint16 order_code = 1;
    };

    // PLC 运行配置：
    // 1) 连接参数；
    // 2) 轮询节拍；
    // 3) 各寄存器区块的起始地址（reg_count 由协议常量给出，不在 ini 中反复填写）。
    struct PlcConfig
    {
        bool enabled = false;
        QString host = QStringLiteral("127.0.0.1");
        int port = 502;
        int server_address = 1;

        int connect_timeout_ms = 3000;
        int response_timeout_ms = 1000;
        int number_of_retries = 1;
        int poll_interval_ms = 100;
        bool auto_reconnect = true;
        int reconnect_interval_ms = 2000;

        bool use_fake_client = false; // 1=使用本地 Fake PLC，而非真实 Modbus/TCP

        quint32 status_start_address = 0;
        quint32 tray_start_address = 0;
        quint32 command_start_address = 0;
        quint32 mailbox_start_address = 0;
        quint32 pc_ack_start_address = 0;
    };

    // 应用程序配置结构体，整合所有配置为一个单一配置对象，便于统一管理
    struct AppConfig
    {
        DbConfig db;
        PathConfig paths;
        MesConfig mes;

        // A 型：CONF 4ch
        ScanConfig scan_a;
        // B 型：RUNO 2ch
        ScanConfig scan_b;

        // PLC 通讯配置
        PlcConfig plc;
    };

    // 函数声明
    AppConfig loadConfigIni(const QString &iniPath);
    QString resolveMesInterfaceUrl(const MesConfig &cfg, const QString &interfaceCode);
    bool hasMesInterfaceUrl(const MesConfig &cfg, const QString &interfaceCode);

    /*
    函数名：loadConfigIni
    参数：INI 文件路径字符串
    返回值：解析后的完整应用配置对象
    作用：从 app.ini 文件加载配置并返回结构化数据
    */

} // namespace core
