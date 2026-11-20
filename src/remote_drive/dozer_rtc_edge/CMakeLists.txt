cmake_minimum_required(VERSION 3.18)
project(dozer_rtc_edge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
find_package(ament_cmake REQUIRED)
find_package(Threads REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(dozer_audio REQUIRED)
find_package(dozer_msgs REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc)
find_package(OpenSSL REQUIRED)

find_package(dozer_media_vendor REQUIRED)
find_package(dozer_rtc_sdk REQUIRED)
find_package(dozer_rtc_common REQUIRED)

set(app "${CMAKE_CURRENT_SOURCE_DIR}/src/app")
add_library(edge_dependencies INTERFACE)
target_include_directories(edge_dependencies INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/Simple-WebSocket-Server" ${OpenCV_INCLUDE_DIRS})
target_compile_definitions(edge_dependencies INTERFACE ASIO_STANDALONE USE_STANDALONE_ASIO)
target_link_libraries(edge_dependencies INTERFACE
  dozer_rtc_sdk::vtsrtc dozer_rtc_common::dozer_rtc_common
  dozer_media_vendor::headers dozer_audio::dozer_audio
  Threads::Threads OpenSSL::SSL OpenSSL::Crypto ${OpenCV_LIBS})

add_library(edge_support STATIC
  src/ros/ros_logging.cpp
  src/runtime/rtc_session.cpp
  src/vehicle/vehicle_control_interface.cpp
  src/vehicle/vehicle_control_module.cpp
  src/dog/dog_command_forwarder.cpp)
target_link_libraries(edge_support edge_dependencies)
ament_target_dependencies(edge_support rclcpp)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
  set(CMAKE_CUDA_ARCHITECTURES 87 CACHE STRING "CUDA architectures (Jetson Orin by default)")
endif()
enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)
add_library(edge_direct_camera STATIC
  src/camera/v4l2_camera.cpp
  src/camera/single/async_image_source.cpp
  src/camera/single/frame_converter.cpp
  src/camera/single/uyvy_to_i420_cuda.cu
  src/ros/direct_image_source.cpp)
target_link_libraries(edge_direct_camera PUBLIC edge_support CUDA::cudart dozer_media_vendor::yuv)
target_compile_options(edge_direct_camera PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)

add_executable(rtc_edge
  ${app}/main.cpp ${app}/edge_options.cpp ${app}/edge_application.cpp ${app}/video_send_planner.cpp
  src/ros/ros_context.cpp
  src/ros/ros_audio_source.cpp
  src/ros/ros_image_source.cpp
  src/ros/ros_camera_streaming.cpp)
target_link_libraries(rtc_edge edge_direct_camera)
ament_target_dependencies(rtc_edge rclcpp sensor_msgs dozer_msgs)
include(../../../cmake/rtc_runtime.cmake)
dozer_install_rtc_app(rtc_edge)
set(rtc_config_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../system/dozer_bringup/config/rtc_edge")
foreach(config_name rtc.cfg edge.cfg)
  configure_file("${rtc_config_dir}/${config_name}" "${CMAKE_CURRENT_BINARY_DIR}/${config_name}" COPYONLY)
endforeach()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rtc.cfg" "${CMAKE_CURRENT_BINARY_DIR}/edge.cfg"
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  add_executable(rtc_camera_tests test/camera/single/rtc_camera_tests.cpp)
  target_link_libraries(rtc_camera_tests PRIVATE edge_direct_camera)
  add_test(NAME rtc_camera_tests COMMAND rtc_camera_tests)
  set_tests_properties(rtc_camera_tests PROPERTIES TIMEOUT 30 LABELS hardware)
  add_executable(direct_image_source_tests test/ros/direct_image_source_tests.cpp)
  target_link_libraries(direct_image_source_tests PRIVATE edge_direct_camera)
  add_test(NAME direct_image_source_tests COMMAND direct_image_source_tests)
  add_executable(shared_camera_frame_tests test/ros/shared_camera_frame_tests.cpp)
  target_include_directories(shared_camera_frame_tests PRIVATE include)
  add_test(NAME shared_camera_frame_tests COMMAND shared_camera_frame_tests)
  set_tests_properties(shared_camera_frame_tests PROPERTIES TIMEOUT 10)
  function(edge_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE "${app}")
    target_link_libraries(${name} PRIVATE edge_support)
    add_test(NAME ${name} COMMAND ${name})
  endfunction()
  enable_testing()
  find_package(rcl_interfaces REQUIRED)
  edge_test(ros_logging_tests test/ros/ros_logging_tests.cpp)
  target_link_libraries(ros_logging_tests PRIVATE ${rcl_interfaces_TARGETS})
  add_test(NAME ros_logging_preinitialized_tests COMMAND ros_logging_tests --preinitialized)
  set_tests_properties(ros_logging_tests ros_logging_preinitialized_tests PROPERTIES
    TIMEOUT 15
    FAIL_REGULAR_EXPRESSION "logging was initialized more than once")
  edge_test(rtc_edge_options_tests ${app}/edge_options.cpp test/app/edge_options_tests.cpp)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/no_config")
  add_test(NAME rtc_edge_default_config_tests COMMAND rtc_edge_options_tests --check-default-config)
  set_tests_properties(rtc_edge_default_config_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/no_config")
  edge_test(rtc_edge_streaming_tests ${app}/video_send_planner.cpp test/app/video_send_planner_tests.cpp)
  edge_test(rtc_vehicle_control_protocol_tests test/protocols/vehicle/vehicle_control_protocol_tests.cpp)
  edge_test(dog_command_forwarder_tests test/dog/dog_command_forwarder_tests.cpp)
  set_tests_properties(dog_command_forwarder_tests PROPERTIES TIMEOUT 30)
  edge_test(rtc_vehicle_control_module_tests test/vehicle/vehicle_control_module_tests.cpp)
  edge_test(rtc_vision_view_control_protocol_tests test/protocols/vision/view_control_protocol_tests.cpp)
  add_executable(ros_image_source_tests test/ros/ros_image_source_tests.cpp
    src/ros/ros_image_source.cpp src/ros/ros_context.cpp src/ros/ros_camera_streaming.cpp)
  target_include_directories(ros_image_source_tests PRIVATE include ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(ros_image_source_tests edge_direct_camera ${OpenCV_LIBS} Threads::Threads)
  ament_target_dependencies(ros_image_source_tests rclcpp sensor_msgs)
  add_test(NAME ros_image_source_tests COMMAND ros_image_source_tests)
  add_test(NAME ros_image_source_standalone_tests COMMAND ros_image_source_tests --standalone-context)
  set_tests_properties(ros_image_source_tests ros_image_source_standalone_tests PROPERTIES
    TIMEOUT 30
    FAIL_REGULAR_EXPRESSION "logging was initialized more than once")
  add_executable(ros_audio_source_tests test/ros/ros_audio_source_tests.cpp
    src/ros/ros_audio_source.cpp src/ros/ros_context.cpp src/ros/ros_image_source.cpp)
  target_include_directories(ros_audio_source_tests PRIVATE include ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(ros_audio_source_tests ${OpenCV_LIBS} Threads::Threads dozer_audio::dozer_audio)
  ament_target_dependencies(ros_audio_source_tests rclcpp sensor_msgs dozer_msgs)
  add_test(NAME ros_audio_source_tests COMMAND ros_audio_source_tests)
  add_test(NAME ros_audio_source_standalone_tests COMMAND ros_audio_source_tests --standalone-context)
  set_tests_properties(ros_audio_source_tests ros_audio_source_standalone_tests PROPERTIES
    TIMEOUT 30
    FAIL_REGULAR_EXPRESSION "logging was initialized more than once")
endif()
ament_package()
