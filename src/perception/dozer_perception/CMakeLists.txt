cmake_minimum_required(VERSION 3.8)
project(dozer_perception)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclpy REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(dozer_msgs REQUIRED)
install(PROGRAMS scripts/yolo_node.py scripts/detection_viewer.py DESTINATION lib/${PROJECT_NAME})
install(FILES scripts/camera_shm.py DESTINATION lib/${PROJECT_NAME})

# TODO(负责人)：在此添加节点目标，参考 README.md 中的“包规范”。
# 示例：
# add_executable(xxx_node src/xxx_node.cpp)
# ament_target_dependencies(xxx_node rclcpp dozer_msgs)
# install(TARGETS xxx_node DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  enable_testing()
  add_test(NAME camera_shm_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test/camera_shm_tests.py)
  set_tests_properties(camera_shm_tests PROPERTIES TIMEOUT 15
    ENVIRONMENT "ROS_DOMAIN_ID=182;ROS_LOCALHOST_ONLY=1")
  # find_package(ament_cmake_gtest REQUIRED)
  # ament_add_gtest(${PROJECT_NAME}_tests test/xxx_tests.cpp)
endif()

ament_package()
