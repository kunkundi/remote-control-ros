cmake_minimum_required(VERSION 3.18)
project(dozer_rtc_console LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(ament_cmake REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(dozer_audio REQUIRED)

set(RTC_DEPENDENCY_CACHE "$ENV{HOME}/.xmake/packages" CACHE PATH "Optional preinstalled dependency cache; xmake is not invoked")
foreach(dependency nanopb nlohmann_json libyuv imgui onnxruntime)
  string(SUBSTRING "${dependency}" 0 1 initial)
  file(GLOB dependency_prefixes "${RTC_DEPENDENCY_CACHE}/${initial}/${dependency}/*/*")
  list(APPEND CMAKE_PREFIX_PATH ${dependency_prefixes})
endforeach()
find_package(nlohmann_json CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
  find_path(JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
  add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
  set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${JSON_INCLUDE_DIR}")
endif()

find_package(imgui CONFIG REQUIRED)
find_path(NANOPB_INCLUDE_DIR pb.h PATH_SUFFIXES nanopb REQUIRED)
find_library(NANOPB_LIBRARY NAMES protobuf-nanopb nanopb REQUIRED)
find_path(LIBYUV_INCLUDE_DIR libyuv.h REQUIRED)
find_library(LIBYUV_LIBRARY NAMES yuv REQUIRED)
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h REQUIRED)
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime REQUIRED)
set(VTSRTC_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vtsrtc" CACHE PATH "Standalone vtsrtc SDK directory")
find_path(CONSOLE_VTSRTC_INCLUDE_DIR c_rtc.h PATHS "${VTSRTC_SDK_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
find_library(CONSOLE_VTSRTC_LIBRARY NAMES vtsrtc.aarch64 vtsrtc PATHS "${VTSRTC_SDK_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
set(VTSRTC_INCLUDE_DIR "${CONSOLE_VTSRTC_INCLUDE_DIR}")
set(VTSRTC_LIBRARY "${CONSOLE_VTSRTC_LIBRARY}")
add_library(vtsrtc_sdk SHARED IMPORTED)
set_target_properties(vtsrtc_sdk PROPERTIES IMPORTED_LOCATION "${VTSRTC_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${VTSRTC_INCLUDE_DIR}"
  IMPORTED_NO_SONAME TRUE)

set(app "${CMAKE_CURRENT_SOURCE_DIR}/src/app")
set(protocols "${CMAKE_CURRENT_SOURCE_DIR}/protocols")
add_library(console_options STATIC "${app}/rtc_console_options.cpp")
target_include_directories(console_options PUBLIC
  "${app}" "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(console_options PUBLIC nlohmann_json::nlohmann_json)

add_library(console_protocols STATIC
  "${protocols}/vehicle/generated/rtc_vehicle.pb.c"
  "${protocols}/vehicle/src/vehicle_control_protocol.cpp"
  "${protocols}/vision/generated/vision_detection.pb.c"
  "${protocols}/vision/src/vision_detection_codec.cpp"
  "${protocols}/vision/src/view_control_protocol.cpp")
target_include_directories(console_protocols PUBLIC
  "${protocols}/vehicle/include" "${protocols}/vehicle/generated"
  "${protocols}/vision/include" "${protocols}/vision/generated"
  "${NANOPB_INCLUDE_DIR}")
target_link_libraries(console_protocols PUBLIC "${NANOPB_LIBRARY}")

add_library(console_support STATIC
  src/runtime/process_runtime.cpp
  src/vision/yolo_onnx_detector.cpp
  "${app}/local_yolo.cpp"
  "${app}/video_composite_layout.cpp"
  "${app}/video_detection_overlay.cpp"
  "${app}/video_view_state.cpp"
  "${app}/vehicle_control_sender.cpp"
  "${app}/vehicle_state_log_limiter.cpp")
target_include_directories(console_support PUBLIC
  "${LIBYUV_INCLUDE_DIR}" "${ONNXRUNTIME_INCLUDE_DIR}")
target_link_libraries(console_support PUBLIC console_options console_protocols
  vtsrtc_sdk Threads::Threads "${LIBYUV_LIBRARY}" "${ONNXRUNTIME_LIBRARY}")

add_executable(rtc_console "${app}/main.cpp")
target_link_libraries(rtc_console PRIVATE console_support dozer_audio::dozer_audio
  imgui::imgui OpenGL::GL ${CMAKE_DL_LIBS})
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES "${VTSRTC_INCLUDE_DIR}")
set(CMAKE_REQUIRED_LIBRARIES "${VTSRTC_LIBRARY}")
check_cxx_source_compiles("#include <c_rtc.h>
int main() {
  RtcVideoDelayStatsV1 stats{};
  RtcSetVideoDelayStatsHandlerV1(nullptr);
  return static_cast<int>(stats.struct_size);
}" RTC_HAS_VIDEO_DELAY_STATS)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)
if(RTC_HAS_VIDEO_DELAY_STATS)
  target_compile_definitions(rtc_console PRIVATE DOZER_RTC_HAS_VIDEO_DELAY_STATS=1)
endif()
get_filename_component(onnxruntime_library_dir "${ONNXRUNTIME_LIBRARY}" DIRECTORY)
set_target_properties(rtc_console PROPERTIES
  INSTALL_RPATH "$ORIGIN;${onnxruntime_library_dir}")
install(TARGETS rtc_console DESTINATION lib/${PROJECT_NAME})
install(FILES "${VTSRTC_LIBRARY}" DESTINATION lib/${PROJECT_NAME})

set(rtc_config_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../system/dozer_bringup/config/rtc_console")
foreach(config_name rtc.cfg console.cfg)
  configure_file("${rtc_config_dir}/${config_name}" "${CMAKE_CURRENT_BINARY_DIR}/${config_name}" COPYONLY)
endforeach()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rtc.cfg" "${CMAKE_CURRENT_BINARY_DIR}/console.cfg"
  DESTINATION lib/${PROJECT_NAME})
set(CONSOLE_YOLO_MODEL "${CMAKE_CURRENT_SOURCE_DIR}/models/yolo26n.onnx" CACHE FILEPATH "Optional console YOLO model")
if(EXISTS "${CONSOLE_YOLO_MODEL}")
  install(FILES "${CONSOLE_YOLO_MODEL}" DESTINATION lib/${PROJECT_NAME}/models RENAME yolo26n.onnx)
endif()

if(BUILD_TESTING)
  enable_testing()
  function(console_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE console_support)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
  endfunction()
  console_test(rtc_console_options_tests test/rtc_console_options_tests.cpp)
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
    "-DSDK_LIBRARY=${VTSRTC_LIBRARY}"
    "-DREADELF_TOOL=${CONSOLE_READELF}"
    "-DLDD_TOOL=${CONSOLE_LDD}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test/sdk_linkage_test.cmake")
  set_tests_properties(rtc_console_sdk_linkage PROPERTIES TIMEOUT 10)
  if(EXISTS "${CONSOLE_YOLO_MODEL}")
    console_test(rtc_console_local_yolo_tests test/local_yolo_tests.cpp)
    set_tests_properties(rtc_console_local_yolo_tests PROPERTIES
      ENVIRONMENT "VTSRTC_YOLO_MODEL=${CONSOLE_YOLO_MODEL}" TIMEOUT 60)
  endif()
endif()

ament_package()