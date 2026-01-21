cmake_minimum_required(VERSION 3.16)    # 指定 CMake 最低版本为 3.16
# 定义项目名称为 hmi_measure_mysql，使用 C++ 语言
project(hmi_measure_mysql LANGUAGES CXX)

# 设置 C++ 标准版本为 C++17
set(CMAKE_CXX_STANDARD 17)
# 强制使用指定的 C++ 标准，不允许降级
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加子目录 libs/core（该目录必须包含 CMakeLists.txt）
add_subdirectory(libs/core)
# 添加子目录 apps/hmi_qt（该目录必须包含 CMakeLists.txt）
add_subdirectory(apps/hmi_qt)
