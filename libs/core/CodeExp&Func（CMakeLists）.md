## 代码解释

这是 **core 库的构建配置文件**，定义了核心库的编译规则和依赖关系。

### 关键配置详解

#### 1. 项目定义

```cmake
project(core LANGUAGES CXX)
```

定义名为 `core` 的子项目，使用 C++ 语言编译

#### 2. 依赖查找

```cmake
find_package(Qt5 REQUIRED COMPONENTS Core Sql)
```

- 查找 Qt5 框架的两个必需组件
- **Qt5::Core**：Qt 核心库（事件系统、信号槽等）
- **Qt5::Sql**：Qt SQL 模块（数据库操作）
- `REQUIRED` 表示若找不到会构建失败

#### 3. 静态库构建

```cmake
add_library(core STATIC
  src/config.cpp
  src/db.cpp
  src/model.cpp
)
```

编译三个源文件为**静态库**（.a 文件）：

- **config.cpp**：配置管理模块
- **db.cpp**：数据库操作模块
- **model.cpp**：数据模型模块

#### 4. 头文件目录设置

```cmake
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
```

将 `include/` 目录暴露为公开头文件搜索路径，使得其他模块可以包含 `core/config.hpp` 等头文件

#### 5. 链接库依赖

```cmake
target_link_libraries(core PUBLIC Qt5::Core Qt5::Sql)
```

链接 Qt 库，使 core 库及其使用者都能访问 Qt 功能

## 项目中的作用

| 角色             | 说明                                                 |
| ---------------- | ---------------------------------------------------- |
| **核心库提供者** | 为整个应用程序提供基础功能库                         |
| **模块化设计**   | 与应用层（hmi_qt）分离，代码结构清晰                 |
| **中间层**       | config、db、model 三个模块处理配置、数据库、数据操作 |
| **依赖管理**     | 统一管理 Qt 库的依赖，应用层只需链接 core 库即可     |
| **代码复用**     | 核心功能可被多个应用模块共享使用                     |

**总体架构**：`hmi_qt 应用` → 链接 → `core 库` → 链接 → `Qt5 库`