## 代码解释

这是**配置管理模块的实现文件**，具体实现 INI 文件的解析和加载。

### 代码结构分析

#### 头文件包含

```cpp
#include "core/config.hpp"  // 配置数据结构定义
#include <QSettings>        // Qt 的配置文件读取类
```

#### 函数实现：loadConfigIni()

```cpp
AppConfig loadConfigIni(const QString &iniPath)
{
    QSettings s(iniPath, QSettings::IniFormat);  // 创建 QSettings 对象读取 INI 文件
    AppConfig c;  // 声明返回的配置对象
```

**QSettings 说明**：Qt 的官方配置文件管理类，支持 INI 格式的键值对存储

### 数据库配置解析

```cpp
s.beginGroup("db");  // 进入 [db] 分组
c.db.driver = s.value("driver", "QMYSQL").toString();
c.db.host = s.value("host", "localhost").toString();
c.db.port = s.value("port", 3306).toInt();
c.db.name = s.value("name").toString();
c.db.user = s.value("user").toString();
c.db.pass = s.value("pass").toString();
c.db.options = s.value("options", "MYSQL_OPT_RECONNECT=1").toString();
s.endGroup();  // 退出 [db] 分组
```

| 字段    | 读取方式   | 默认值                  | 说明         |
| ------- | ---------- | ----------------------- | ------------ |
| driver  | toString() | "QMYSQL"                | 数据库驱动   |
| host    | toString() | "localhost"             | 主机地址     |
| port    | toInt()    | 3306                    | 端口号       |
| name    | toString() | （无）                  | 数据库名     |
| user    | toString() | （无）                  | 用户名       |
| pass    | toString() | （无）                  | 密码         |
| options | toString() | "MYSQL_OPT_RECONNECT=1" | SQL 连接选项 |

### 路径配置解析

```cpp
s.beginGroup("db");  // 进入 [db] 分组
c.db.driver = s.value("driver", "QMYSQL").toString();
c.db.host = s.value("host", "localhost").toString();
c.db.port = s.value("port", 3306).toInt();
c.db.name = s.value("name").toString();
c.db.user = s.value("user").toString();
c.db.pass = s.value("pass").toString();
c.db.options = s.value("options", "MYSQL_OPT_RECONNECT=1").toString();
s.endGroup();  // 退出 [db] 分组
```



| 字段      | 默认值        | 说明         |
| --------- | ------------- | ------------ |
| data_root | "./data"      | 数据根目录   |
| raw_dir   | "./data/raw"  | 原始数据目录 |
| log_dir   | "./data/logs" | 日志目录     |

## 项目中的作用

| 角色               | 说明                                                         |
| ------------------ | ------------------------------------------------------------ |
| **配置文件解析器** | 将 INI 格式文件解析为程序能理解的结构化数据                  |
| **应用初始化**     | 应用启动时首先调用此函数获取所有必要配置                     |
| **数据库连接**     | 提供数据库连接所需的完整参数（主机、端口、用户、密码等）     |
| **目录管理**       | 为应用提供存放数据、日志等文件的目录路径                     |
| **缺省值处理**     | 若 [app.ini](vscode-file://vscode-app/usr/share/code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 中缺少某些配置项，使用默认值，提高容错性 |
| **配置与代码解耦** | 配置改动无需重新编译，便于不同环境部署                       |

## 调用流程

```
应用启动 (main.cpp)
    ↓
main() 函数执行
    ↓
调用 core::loadConfigIni("configs/app.ini")
    ↓
QSettings 打开并解析 app.ini
    ↓
按组读取所有配置项
    ↓
返回完整的 AppConfig 对象
    ↓
将配置传递给数据库模块、路径管理等
```

