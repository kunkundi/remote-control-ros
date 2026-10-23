cmake_minimum_required(VERSION 3.8)
project(dozer_camera_gmsl)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(camera_gmsl_node src/camera_node.cpp)
ament_target_dependencies(camera_gmsl_node rclcpp sensor_msgs)
install(TARGETS camera_gmsl_node DESTINATION lib/${PROJECT_NAME})

ament_package()
