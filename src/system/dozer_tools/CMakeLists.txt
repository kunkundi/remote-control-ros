cmake_minimum_required(VERSION 3.8)
project(dozer_tools)

find_package(ament_cmake REQUIRED)

install(PROGRAMS
  scripts/mock_radar_publisher.py
  scripts/record_bag.sh
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
