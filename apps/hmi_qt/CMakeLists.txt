project(hmi_qt LANGUAGES CXX)

# Widgets:Qt图形界面库，创建窗口和控件
# Sql:Qt数据库访问库，处理数据库连接和查询
# Core:Qt核心库，提供基础功能如事件循环、字符串处理等
find_package(Qt5 REQUIRED COMPONENTS 
  Widgets 
  Sql 
  Core 
  Network
)

file(GLOB APP_SRC CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.ui"
)

add_executable(hmi_qt ${APP_SRC})

target_include_directories(hmi_qt PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(hmi_qt PRIVATE 
  Qt5::Widgets 
  Qt5::Sql 
  Qt5::Core
  Qt5::Network 
  core
)
