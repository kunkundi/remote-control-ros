cmake_minimum_required(VERSION 3.8)
project(dozer_camera_gmsl)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_components REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)

# 第一阶段：C++ 库（由 rtc-framework 的 client/modules/camera 迁移），供 dozer_rtc_bridge 直接链接。
# TODO(负责人)：
# add_library(${PROJECT_NAME} SHARED src/v4l2_camera.cpp src/async_image_source.cpp ...)
# target_include_directories(${PROJECT_NAME} PUBLIC
#   $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
# install(DIRECTORY include/ DESTINATION include)
# install(TARGETS ${PROJECT_NAME} EXPORT export_${PROJECT_NAME}
#   LIBRARY DESTINATION lib ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)
# ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
#
# 第二阶段：component（rclcpp_components_register_node），帧类型通过 rclcpp::TypeAdapter
# 适配为 sensor_msgs/Image，同进程订阅者零拷贝，跨进程订阅者才转换。

if(BUILD_TESTING)
  # find_package(ament_cmake_gtest REQUIRED)
  # ament_add_gtest(${PROJECT_NAME}_tests test/xxx_tests.cpp)
endif()

ament_package()
