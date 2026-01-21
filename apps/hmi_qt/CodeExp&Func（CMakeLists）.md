这是 **HMI Qt 应用的构建配置文件**，定义了应用程序的编译规则和依赖关系。

### 代码结构分析

#### 项目定义

```cmake
project(hmi_qt LANGUAGES CXX)
```

定义名为 `hmi_qt` 的子项目，使用 C++ 语言编译

#### 依赖包查找

```cmake
find_package(Qt5 REQUIRED COMPONENTS Widgets Sql Core)
```

| 组件        | 说明          | 用途                               |
| ----------- | ------------- | ---------------------------------- |
| **Widgets** | Qt 图形界面库 | 创建窗口、按钮等 UI 控件           |
| **Sql**     | Qt 数据库库   | 数据库操作（虽然由 core 库使用）   |
| **Core**    | Qt 核心库     | 事件循环、信号槽、字符串等基础功能 |

#### 可执行文件构建

```cmake
add_executable(hmi_qt
  main.cpp
)
```

- 编译 [main.cpp](vscode-file://vscode-app/usr/share/code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 为名为 `hmi_qt` 的可执行程序
- 源文件列表最少化（仅包含主程序入口）

#### 链接库依赖

```cmake
target_link_libraries(hmi_qt PRIVATE Qt5::Widgets Qt5::Sql Qt5::Core core)
```

| 库           | 访问级别 | 说明                                   |
| ------------ | -------- | -------------------------------------- |
| Qt5::Widgets | PRIVATE  | UI 库，仅应用使用                      |
| Qt5::Sql     | PRIVATE  | SQL 库，仅应用使用                     |
| Qt5::Core    | PRIVATE  | 核心库，仅应用使用                     |
| **core**     | PRIVATE  | 自定义的 core 库（配置、数据库、模型） |

## 项目中的作用

| 角色             | 说明                                                         |
| ---------------- | ------------------------------------------------------------ |
| **应用入口配置** | 定义如何编译 HMI 应用程序                                    |
| **依赖管理**     | 明确应用所需的 Qt 组件和 core 库                             |
| **链接规则**     | 确保编译时正确链接所有依赖库                                 |
| **最小化构建**   | 只编译 main.cpp，其他代码通过 core 库链接                    |
| **构建目标生成** | 最终生成 [hmi_qt](vscode-file://vscode-app/usr/share/code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 可执行文件 |

## 依赖关系图

```
hmi_qt 应用
    ├─ Qt5::Widgets ─── UI 界面
    ├─ Qt5::Sql ──────── 数据库操作
    ├─ Qt5::Core ─────── 核心功能
    └─ core 库 ───────── 自定义库
        ├─ config.cpp ─── 配置读取
        ├─ db.cpp ─────── 数据库操作
        └─ model.cpp ──── 数据模型
            └─ Qt5::Core, Qt5::Sql
```

## 编译流程

```
cmake (根 CMakeLists.txt)
    ├─ add_subdirectory(libs/core)
    │   └─ 编译 core 静态库
    └─ add_subdirectory(apps/hmi_qt)
        ├─ find_package(Qt5) 查找 Qt5
        └─ add_executable(hmi_qt main.cpp)
            └─ target_link_libraries(... core)
                └─ 链接 core 库 + Qt 库
                    └─ 生成 hmi_qt 可执行文件
```

## 典型的构建命令

**简述**：这个 [CMakeLists.txt](vscode-file://vscode-app/usr/share/code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 将 [main.cpp](vscode-file://vscode-app/usr/share/code/resources/app/out/vs/code/electron-browser/workbench/workbench.html) 编译为 HMI 应用，并链接 Qt 框架和 core 库，形成完整的可执行程序。