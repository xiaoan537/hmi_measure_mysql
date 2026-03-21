project(core LANGUAGES CXX) # 项目定义：定义了一个名为 core 的子项目，使用 C++ 语言

# 依赖查找：查找 Qt5 库，并明确要求 Core、Sql 和 SerialBus 组件。如果找不到这些组件，CMake 会报错
find_package(Qt5 REQUIRED COMPONENTS Core Sql SerialBus)

# 将当前目录下的 src 目录中的所有 .cpp 文件，以及 include/core 下的头文件都加入目标。
# 这样 AUTOMOC 才能扫描到位于 core 库中的 Q_OBJECT 头文件（如 plc_runtime_v2.hpp 等）。
file(GLOB CORE_SRC CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB CORE_HDR CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/include/core/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/core/*.hpp"
)

# 旧版已弃用：如果文件存在，避免被误编译进来
list(FILTER CORE_SRC EXCLUDE REGEX ".*/raw_writer\.cpp$")

# 静态库构建
add_library(core STATIC ${CORE_SRC} ${CORE_HDR})

# 头文件目录：告知 core 目标及其所有使用者都需要在 include 目录下查找头文件。
target_include_directories(core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 链接库：确保 core 目标及其所有使用者都能正确链接到 Qt 的核心功能、数据库功能和 Modbus 所需的 SerialBus
# 并自动获得所有必要的编译设置
target_link_libraries(core PUBLIC
  Qt5::Core
  Qt5::Sql
  Qt5::SerialBus
)