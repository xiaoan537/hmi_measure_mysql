#pragma once
#include <QString>

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
        bool enabled = false;          // 是否启用推送
        QString url;                   // MES 接口地址（单条上传）
        QString auth_token;            // Bearer token（可选）
        int timeout_ms = 5000;         // HTTP 超时（ms）
        int retry_base_seconds = 30;   // 重试基准间隔（秒）
        int retry_max_seconds = 21600; // 最大重试间隔（秒）
    };

    // 应用程序配置结构体，整合所有配置为一个单一配置对象，便于统一管理
    struct AppConfig
    {
        DbConfig db;
        PathConfig paths;
        MesConfig mes;
    };

    // 函数声明
    AppConfig loadConfigIni(const QString &iniPath);

    /*
    函数名：loadConfigIni
    参数：INI 文件路径字符串
    返回值：解析后的完整应用配置对象
    作用：从 app.ini 文件加载配置并返回结构化数据
    */

} // namespace core
