cmake_minimum_required(VERSION 3.18)
project(dozer_rtc_console LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(ament_cmake REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(dozer_audio REQUIRED)

find_package(dozer_media_vendor REQUIRED)
find_package(dozer_rtc_sdk REQUIRED)
find_package(imgui CONFIG REQUIRED)

set(app "${CMAKE_CURRENT_SOURCE_DIR}/src/app")
set(protocols "${CMAKE_CURRENT_SOURCE_DIR}/protocols")
add_library(console_options STATIC "${app}/rtc_console_options.cpp")
target_include_directories(console_options PUBLIC
  "${app}" "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(console_options PUBLIC dozer_media_vendor::headers)

add_library(console_protocols STATIC
  "${protocols}/vehicle/generated/rtc_vehicle.pb.c"
  "${protocols}/vehicle/src/vehicle_control_protocol.cpp"
  "${protocols}/vision/src/view_control_protocol.cpp")
target_include_directories(console_protocols PUBLIC
  "${protocols}/vehicle/include" "${protocols}/vehicle/generated"
  "${protocols}/vision/include")
target_link_libraries(console_protocols PUBLIC dozer_media_vendor::nanopb)

add_library(console_support STATIC
  src/runtime/process_runtime.cpp
  "${app}/video_composite_layout.cpp"
  "${app}/video_detection_overlay.cpp"
  "${app}/video_view_state.cpp"
  "${app}/vehicle_control_sender.cpp"
  "${app}/vehicle_state_log_limiter.cpp")
target_link_libraries(console_support PUBLIC console_options console_protocols
  dozer_rtc_sdk::vtsrtc Threads::Threads dozer_media_vendor::yuv)

include(../protocols/fused_detection.cmake)
add_executable(rtc_console "${app}/main.cpp")
target_link_libraries(rtc_console PRIVATE console_support fused_detection_protocol dozer_audio::dozer_audio
  imgui::imgui OpenGL::GL ${CMAKE_DL_LIBS})
target_compile_definitions(rtc_console PRIVATE DOZER_RTC_HAS_VIDEO_DELAY_STATS=1)
include(../../../cmake/rtc_runtime.cmake)
dozer_install_rtc_app(rtc_console)

set(rtc_config_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../system/dozer_bringup/config/rtc_console")
foreach(config_name rtc.cfg console.cfg)
  configure_file("${rtc_config_dir}/${config_name}" "${CMAKE_CURRENT_BINARY_DIR}/${config_name}" COPYONLY)
endforeach()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rtc.cfg" "${CMAKE_CURRENT_BINARY_DIR}/console.cfg"
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  enable_testing()
  function(console_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE console_support)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
  endfunction()
  console_test(rtc_console_options_tests test/rtc_console_options_tests.cpp)
  console_test(rtc_console_fused_detection_tests test/fused_detection_tests.cpp)
  target_link_libraries(rtc_console_fused_detection_tests PRIVATE fused_detection_protocol)
  console_test(rtc_console_vehicle_control_sender_tests test/vehicle_control_sender_tests.cpp)
  console_test(rtc_console_vehicle_state_log_tests test/vehicle_state_log_limiter_tests.cpp)
  console_test(rtc_console_video_view_state_tests test/video_view_state_tests.cpp)
  console_test(rtc_console_video_composite_layout_tests test/video_composite_layout_tests.cpp)
  console_test(rtc_console_video_detection_overlay_tests test/video_detection_overlay_tests.cpp)
  console_test(rtc_console_video_fullscreen_state_tests test/video_fullscreen_state_tests.cpp)
  add_test(NAME rtc_console_config COMMAND rtc_console_options_tests
    "${CMAKE_CURRENT_BINARY_DIR}/rtc.cfg")
  set_tests_properties(rtc_console_config PROPERTIES TIMEOUT 30)
  add_test(NAME rtc_console_help COMMAND rtc_console --help)
  set_tests_properties(rtc_console_help PROPERTIES TIMEOUT 10)
  find_program(CONSOLE_READELF readelf REQUIRED)
  find_program(CONSOLE_LDD ldd REQUIRED)
  add_test(NAME rtc_console_sdk_linkage COMMAND "${CMAKE_COMMAND}"
    "-DCONSOLE_EXECUTABLE=$<TARGET_FILE:rtc_console>"
    "-DSDK_LIBRARY=$<TARGET_FILE:dozer_rtc_sdk::vtsrtc>"
    "-DREADELF_TOOL=${CONSOLE_READELF}"
    "-DLDD_TOOL=${CONSOLE_LDD}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test/sdk_linkage_test.cmake")
  set_tests_properties(rtc_console_sdk_linkage PROPERTIES TIMEOUT 10)
endif()

ament_package()
