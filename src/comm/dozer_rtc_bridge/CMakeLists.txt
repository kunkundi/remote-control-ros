cmake_minimum_required(VERSION 3.8)
project(dozer_rtc_bridge)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(dozer_msgs REQUIRED)
find_package(dozer_camera_gmsl REQUIRED)

# rtc-framework 用 xmake 构建并安装，这里只引用其安装产物，不复制源码。
set(RTC_FRAMEWORK_INSTALL "$ENV{HOME}/workspace/rtc-framework/install"
    CACHE PATH "rtc-framework 的 xmake install 目录")
find_path(VTSRTC_INCLUDE_DIR c_rtc.h PATHS "${RTC_FRAMEWORK_INSTALL}/vtsrtc/include" NO_DEFAULT_PATH)
find_library(VTSRTC_LIBRARY NAMES vtsrtc.aarch64 vtsrtc PATHS "${RTC_FRAMEWORK_INSTALL}/lib" NO_DEFAULT_PATH)
if(VTSRTC_INCLUDE_DIR AND VTSRTC_LIBRARY)
  message(STATUS "找到 vtsrtc: ${VTSRTC_LIBRARY}")
  add_library(vtsrtc::vtsrtc SHARED IMPORTED)
  set_target_properties(vtsrtc::vtsrtc PROPERTIES
    IMPORTED_LOCATION "${VTSRTC_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${VTSRTC_INCLUDE_DIR}")
else()
  message(WARNING "未找到 rtc-framework 安装产物（${RTC_FRAMEWORK_INSTALL}），rtc_bridge 节点将不会构建")
endif()

# TODO(负责人)：在此添加节点目标，参考 README.md 中的“包规范”。
# 示例：
# add_executable(xxx_node src/xxx_node.cpp)
# ament_target_dependencies(xxx_node rclcpp dozer_msgs)
# install(TARGETS xxx_node DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  # find_package(ament_cmake_gtest REQUIRED)
  # ament_add_gtest(${PROJECT_NAME}_tests test/xxx_tests.cpp)
endif()

ament_package()
