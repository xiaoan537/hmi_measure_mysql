project(core LANGUAGES CXX) # 项目定义：定义了一个名为 core 的子项目，使用 C++ 语言

# 依赖查找：查找 Qt5 库，并明确要求 Core 和 Sql 组件。如果找不到这些组件，CMake 会报错
find_package(Qt5 REQUIRED COMPONENTS Core Sql SerialBus)

# 将当前目录下的 src 目录中的所有 .cpp 文件添加到 CORE_SRC 变量中
file(GLOB CORE_SRC CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# 旧版已弃用：如果文件存在，避免被误编译进来
list(FILTER CORE_SRC EXCLUDE REGEX ".*/raw_writer\\.cpp$")

# 静态库构建：定义了一个名为 core 的静态库。 该库由 src 目录下的 config.cpp、db.cpp 和 model.cpp 文件组成
add_library(core STATIC ${CORE_SRC})

# 头文件目录：告知 CMake，core目标及其所有使用者都需要在 include目录下查找头文件。
target_include_directories(core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 链接库：确保 core目标及其所有使用者都能正确链接到 Qt 的核心功能和数据库功能，并自动获得所有必要的编译设置
target_link_libraries(core PUBLIC 
  Qt5::Core 
  Qt5::Sql
  Qt5::SerialBus
)
