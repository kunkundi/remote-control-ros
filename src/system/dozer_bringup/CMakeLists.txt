cmake_minimum_required(VERSION 3.8)
project(dozer_bringup)

find_package(ament_cmake REQUIRED)

install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
	find_package(Python3 REQUIRED COMPONENTS Interpreter)
	enable_testing()
	add_test(NAME perception_launch_tests
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/perception_launch_tests.py")
	set_tests_properties(perception_launch_tests PROPERTIES TIMEOUT 20)
endif()

ament_package()
