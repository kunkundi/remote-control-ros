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

set(RTC_FRAMEWORK_ROOT "$ENV{HOME}/workspace/rtc-framework" CACHE PATH "External RTC SDK root")
set(RTC_FRAMEWORK_INSTALL "${RTC_FRAMEWORK_ROOT}/install" CACHE PATH "External vtsrtc installation")
set(RTC_DEPENDENCY_CACHE "$ENV{HOME}/.xmake/packages" CACHE PATH "Optional preinstalled dependency cache; xmake is not invoked")
foreach(dependency nanopb nlohmann_json asio libyuv)
  string(SUBSTRING "${dependency}" 0 1 initial)
  file(GLOB dependency_prefixes "${RTC_DEPENDENCY_CACHE}/${initial}/${dependency}/*/*")
  list(APPEND CMAKE_PREFIX_PATH ${dependency_prefixes})
endforeach()
find_package(nlohmann_json CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
  find_path(JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
  add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
  set_target_properties(nlohmann_json::nlohmann_json PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${JSON_INCLUDE_DIR}")
endif()
find_path(NANOPB_INCLUDE_DIR pb.h PATH_SUFFIXES nanopb REQUIRED)
find_library(NANOPB_LIBRARY NAMES protobuf-nanopb nanopb REQUIRED)
find_path(LIBYUV_INCLUDE_DIR libyuv.h REQUIRED)
find_library(LIBYUV_LIBRARY yuv REQUIRED)
find_path(ASIO_INCLUDE_DIR asio.hpp REQUIRED)
find_path(SIMPLE_WS_INCLUDE_DIR client_ws.hpp HINTS "${RTC_FRAMEWORK_ROOT}/third_party/Simple-WebSocket-Server" REQUIRED)
find_path(VTSRTC_INCLUDE_DIR c_rtc.h HINTS "${RTC_FRAMEWORK_INSTALL}/vtsrtc/include" REQUIRED)
find_library(VTSRTC_LIBRARY NAMES vtsrtc.aarch64 vtsrtc HINTS "${RTC_FRAMEWORK_INSTALL}/lib" REQUIRED)
add_library(vtsrtc_sdk SHARED IMPORTED)
set_target_properties(vtsrtc_sdk PROPERTIES IMPORTED_LOCATION "${VTSRTC_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${VTSRTC_INCLUDE_DIR}")

set(app "${CMAKE_CURRENT_SOURCE_DIR}/src/app")
add_library(edge_dependencies INTERFACE)
target_include_directories(edge_dependencies INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
foreach(protocol vehicle vision)
  target_include_directories(edge_dependencies INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/protocols/${protocol}/generated")
endforeach()
target_include_directories(edge_dependencies INTERFACE "${NANOPB_INCLUDE_DIR}" "${ASIO_INCLUDE_DIR}" "${SIMPLE_WS_INCLUDE_DIR}" ${OpenCV_INCLUDE_DIRS})
target_compile_definitions(edge_dependencies INTERFACE ASIO_STANDALONE USE_STANDALONE_ASIO)
target_include_directories(edge_dependencies INTERFACE "${LIBYUV_INCLUDE_DIR}")
target_link_libraries(edge_dependencies INTERFACE vtsrtc_sdk Threads::Threads nlohmann_json::nlohmann_json dozer_audio::dozer_audio OpenSSL::SSL OpenSSL::Crypto "${NANOPB_LIBRARY}" ${OpenCV_LIBS})

add_library(edge_support STATIC
  src/ros/ros_logging.cpp
  src/runtime/process_runtime.cpp
  src/runtime/rtc_session.cpp
  src/vehicle/vehicle_control_interface.cpp
  src/vehicle/vehicle_control_module.cpp
  src/dog/dog_command_forwarder.cpp
  protocols/vehicle/generated/rtc_vehicle.pb.c
  src/protocols/vehicle/vehicle_control_protocol.cpp
  protocols/vision/generated/vision_detection.pb.c
  src/protocols/vision/vision_detection_codec.cpp
  src/protocols/vision/view_control_protocol.cpp)
target_link_libraries(edge_support edge_dependencies)
ament_target_dependencies(edge_support rclcpp)

add_executable(rtc_edge
  ${app}/main.cpp ${app}/edge_options.cpp ${app}/edge_application.cpp ${app}/video_send_planner.cpp
  src/ros/ros_context.cpp
  src/ros/ros_audio_source.cpp
  src/ros/ros_image_source.cpp
  src/ros/ros_camera_streaming.cpp)
target_link_libraries(rtc_edge edge_support "${LIBYUV_LIBRARY}")
ament_target_dependencies(rtc_edge rclcpp sensor_msgs dozer_msgs)
get_filename_component(vtsrtc_library_dir "${VTSRTC_LIBRARY}" DIRECTORY)
set_target_properties(rtc_edge PROPERTIES INSTALL_RPATH "${vtsrtc_library_dir}")
install(TARGETS rtc_edge DESTINATION lib/${PROJECT_NAME})
set(rtc_config_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../system/dozer_bringup/config/rtc_edge")
foreach(config_name rtc.cfg edge.cfg)
  configure_file("${rtc_config_dir}/${config_name}" "${CMAKE_CURRENT_BINARY_DIR}/${config_name}" COPYONLY)
endforeach()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rtc.cfg" "${CMAKE_CURRENT_BINARY_DIR}/edge.cfg"
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
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
  edge_test(rtc_vehicle_control_module_tests test/vehicle/vehicle_control_module_tests.cpp)
  edge_test(rtc_vision_view_control_protocol_tests test/protocols/vision/view_control_protocol_tests.cpp)
  add_executable(ros_image_source_tests test/ros/ros_image_source_tests.cpp
    src/ros/ros_image_source.cpp src/ros/ros_context.cpp)
  target_include_directories(ros_image_source_tests PRIVATE include ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(ros_image_source_tests ${OpenCV_LIBS} Threads::Threads)
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