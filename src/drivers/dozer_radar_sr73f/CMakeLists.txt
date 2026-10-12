cmake_minimum_required(VERSION 3.8)
project(dozer_radar_sr73f)

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
find_package(dozer_msgs REQUIRED)

# 协议解析、周期拼装、CAN 数据源：与 ROS 无关，供节点、配置工具和单元测试共用
add_library(sr73f_core STATIC
  src/sr73f_protocol.cpp
  src/object_list_assembler.cpp
  src/can_frame_source.cpp
)
target_include_directories(sr73f_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
set_target_properties(sr73f_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 驱动节点：既是 component，也生成独立可执行文件 radar_sr73f_node
add_library(radar_sr73f_component SHARED src/radar_sr73f_node.cpp)
target_link_libraries(radar_sr73f_component sr73f_core)
ament_target_dependencies(radar_sr73f_component
  rclcpp rclcpp_components sensor_msgs diagnostic_msgs dozer_msgs)
rclcpp_components_register_node(radar_sr73f_component
  PLUGIN "dozer_radar_sr73f::RadarSr73fNode"
  EXECUTABLE radar_sr73f_node)

# 雷达配置与排查工具（不依赖 ROS）
add_executable(sr73f_config src/sr73f_config_main.cpp)
target_link_libraries(sr73f_config sr73f_core)

install(TARGETS radar_sr73f_component
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(TARGETS sr73f_config DESTINATION lib/${PROJECT_NAME})
install(PROGRAMS scripts/gen_sr73f_candump.py DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(${PROJECT_NAME}_tests
    test/test_sr73f_protocol.cpp
    test/test_object_list_assembler.cpp
    test/test_candump.cpp)
  target_link_libraries(${PROJECT_NAME}_tests sr73f_core)
endif()

ament_package()
