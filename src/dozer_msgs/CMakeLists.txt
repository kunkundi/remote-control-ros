cmake_minimum_required(VERSION 3.8)
project(dozer_msgs)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/RadarObject.msg"
  "msg/RadarObjectArray.msg"
  "msg/RadarStatus.msg"
  "msg/Detection2D.msg"
  "msg/Detection2DArray.msg"
  "msg/DriveCommand.msg"
  "msg/VehicleState.msg"
  "srv/SetGear.srv"
  DEPENDENCIES std_msgs
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
