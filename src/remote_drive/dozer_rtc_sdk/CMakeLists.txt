cmake_minimum_required(VERSION 3.18)
project(dozer_rtc_sdk LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32)
  cmake_minimum_required(VERSION 3.24)
  add_compile_options(/utf-8 /bigobj)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  include(cmake/windows.cmake)
  return()
endif()
find_package(ament_cmake REQUIRED)
find_package(dozer_media_vendor REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER IMPORTED_TARGET gstreamer-app-1.0 gstreamer-video-1.0)
include(../../../cmake/jetson_archive.cmake)
dozer_extract_jetson_archive(webrtc-jetson.tar.zst webrtc)
set(vendor "${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor")
set(sdk "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(server "${CMAKE_CURRENT_SOURCE_DIR}/signaling")
set(encoder "${sdk}/video/encode/nvidia-jetson")
set(jetson_api "/usr/src/jetson_multimedia_api" CACHE PATH "Jetson Multimedia API")
set(classes "${jetson_api}/samples/common/classes")
find_library(nvbufsurface nvbufsurface PATH_SUFFIXES nvidia tegra REQUIRED)
find_library(nvbufsurftransform nvbufsurftransform PATH_SUFFIXES nvidia tegra REQUIRED)
find_library(v4l2 v4l2 REQUIRED)

add_library(webrtc_dependencies INTERFACE)
target_include_directories(webrtc_dependencies SYSTEM INTERFACE
  "${webrtc}/include" "${webrtc}/include/third_party/abseil-cpp"
  "${webrtc}/include/third_party/jsoncpp/source/include" "${webrtc}/include/third_party/jsoncpp/generated"
  "${webrtc}/include/third_party/libyuv/include" "${webrtc}/include/third_party/boringssl/src/include")
# Match the release WebRTC archive's ABI even in a debug build of the application.
target_compile_definitions(webrtc_dependencies INTERFACE WEBRTC_LINUX WEBRTC_POSIX NDEBUG)
target_link_libraries(webrtc_dependencies INTERFACE "${webrtc}/lib/libwebrtc.a" Threads::Threads X11::X11 rt ${CMAKE_DL_LIBS})

add_library(simple_web INTERFACE)
target_compile_definitions(simple_web INTERFACE USE_STANDALONE_ASIO ASIO_STANDALONE)
target_include_directories(simple_web INTERFACE "${vendor}/Simple-Web-Server"
  "${vendor}/Simple-WebSocket-Server")
target_link_libraries(simple_web INTERFACE dozer_media_vendor::headers Threads::Threads)

add_library(jetson_encoder STATIC
  "${encoder}/jetson_encoder.cpp" "${encoder}/jetsonh264_encoder_impl.cpp" "${encoder}/NvVideoEncoder.cpp"
  "${classes}/NvV4l2Element.cpp" "${classes}/NvV4l2ElementPlane.cpp" "${classes}/NvElementProfiler.cpp"
  "${classes}/NvBuffer.cpp" "${classes}/NvElement.cpp" "${classes}/NvLogging.cpp")
set_target_properties(jetson_encoder PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(jetson_encoder PRIVATE "${sdk}")
target_include_directories(jetson_encoder PUBLIC "${jetson_api}/include")
target_link_libraries(jetson_encoder PUBLIC webrtc_dependencies dozer_media_vendor::headers
  "${v4l2}" "${nvbufsurface}" "${nvbufsurftransform}")
include(CheckCXXSourceCompiles)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_REQUIRED_INCLUDES "${jetson_api}/include")
function(check_encoder_signature name definition)
  check_cxx_source_compiles("#include <NvVideoEncoder.h>\n${definition}" ${name})
  if(${name})
    target_compile_definitions(jetson_encoder PRIVATE ${name})
  endif()
endfunction()
check_encoder_signature(VTSRTC_JETSON_NVVIDEOENCODER_HAS_DEVNODE_CTOR
  "NvVideoEncoder::NvVideoEncoder(const char* n, const char* d, int f) : NvV4l2Element(n, d, f, valid_fields) {}")
check_encoder_signature(VTSRTC_JETSON_NVVIDEOENCODER_HAS_PLAIN_CTOR
  "NvVideoEncoder::NvVideoEncoder(const char* n, int f) : NvV4l2Element(n, \"/dev/null\", f, valid_fields) {}")
check_encoder_signature(VTSRTC_JETSON_NVVIDEOENCODER_HAS_COLORSPACE_OUTPUT_FORMAT
  "int NvVideoEncoder::setOutputPlaneFormat(uint32_t, uint32_t, uint32_t, enum v4l2_colorspace) {return 0;}")
check_encoder_signature(VTSRTC_JETSON_NVVIDEOENCODER_HAS_PLAIN_OUTPUT_FORMAT
  "int NvVideoEncoder::setOutputPlaneFormat(uint32_t, uint32_t, uint32_t) {return 0;}")

add_library(vtsrtc SHARED
  "${sdk}/log/log_manager.cpp" "${sdk}/log/log_webrtc_hook.cpp"
  "${sdk}/rtc_device_manager.cpp" "${sdk}/rtc_connection.cpp" "${sdk}/rtc_connection_manager.cpp"
  "${sdk}/rtc.cpp" "${sdk}/c_rtc.cpp" "${sdk}/video/encode/rtc_encoder_factory.cpp"
  "${sdk}/statistics/rtc_statistics.cpp" "${sdk}/packet/pack.cpp" "${sdk}/packet/packetizer.cpp"
  "${sdk}/packet/depacketizer.cpp")
target_include_directories(vtsrtc
  PRIVATE "${sdk}" "${server}"
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/vtsrtc>" "$<INSTALL_INTERFACE:include/vtsrtc>")
target_link_libraries(vtsrtc PRIVATE simple_web jetson_encoder)
target_compile_definitions(vtsrtc PRIVATE ENABLE_ENCODE_PERF_STATS=0)
if(GSTREAMER_FOUND)
  target_sources(vtsrtc PRIVATE "${sdk}/video/encode/rasp/rasp_h264_encoder_impl.cpp")
  target_compile_definitions(vtsrtc PRIVATE VTSRTC_HAS_GSTREAMER)
  target_link_libraries(vtsrtc PRIVATE PkgConfig::GSTREAMER)
endif()
set_target_properties(vtsrtc PROPERTIES OUTPUT_NAME vtsrtc.aarch64
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
  LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/vtsrtc.map")
target_link_options(vtsrtc PRIVATE "LINKER:--exclude-libs,ALL" "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/vtsrtc.map" "LINKER:-z,defs")
install(TARGETS vtsrtc EXPORT export_vtsrtc LIBRARY DESTINATION lib)
install(FILES include/vtsrtc/c_rtc.h DESTINATION include/vtsrtc)
install(FILES "${webrtc}/LICENSE.md" DESTINATION share/${PROJECT_NAME}/licenses)
foreach(web_library IN ITEMS Simple-Web-Server Simple-WebSocket-Server)
  install(FILES "${vendor}/${web_library}/LICENSE" DESTINATION share/${PROJECT_NAME}/licenses/${web_library})
endforeach()
ament_export_targets(export_vtsrtc HAS_LIBRARY_TARGET)

add_executable(signaling-server "${server}/main.cpp" "${server}/http_controller.cpp" "${server}/ws_controller.cpp"
  "${server}/log_manager.cpp" "${server}/openssl_sha1_compat.cpp")
target_compile_features(signaling-server PRIVATE cxx_std_17)
target_link_libraries(signaling-server PRIVATE simple_web OpenSSL::SSL OpenSSL::Crypto)
install(TARGETS signaling-server DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY "${server}/web" DESTINATION share/${PROJECT_NAME} COMPONENT Runtime)
install(FILES "${server}/signaling-server.cfg" DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  enable_testing()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_executable(rtc_loopback_peer test/loopback_peer.cpp)
  target_link_libraries(rtc_loopback_peer PRIVATE vtsrtc Threads::Threads)
  add_test(NAME rtc_loopback COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/loopback_test.py"
    "$<TARGET_FILE:signaling-server>" "$<TARGET_FILE:rtc_loopback_peer>" "${server}/web")
  set_tests_properties(rtc_loopback PROPERTIES TIMEOUT 60 LABELS hardware)
  add_executable(rtc_video_delay_tests test/rtc_video_delay_tests.cpp)
  target_include_directories(rtc_video_delay_tests PRIVATE "${sdk}")
  target_compile_options(rtc_video_delay_tests PRIVATE -UNDEBUG)
  add_test(NAME rtc_video_delay_tests COMMAND rtc_video_delay_tests)
  add_executable(rtc_video_delay_abi_tests test/rtc_video_delay_abi_tests.cpp)
  target_link_libraries(rtc_video_delay_abi_tests PRIVATE vtsrtc)
  add_test(NAME rtc_video_delay_abi_tests COMMAND rtc_video_delay_abi_tests)
  foreach(name IN ITEMS data_channel_observer_tests rtc_external_audio_device_tests)
    add_executable(${name} test/${name}.cpp)
    target_include_directories(${name} PRIVATE "${sdk}")
    target_link_libraries(${name} PRIVATE webrtc_dependencies)
    add_test(NAME ${name} COMMAND ${name})
  endforeach()
  add_executable(rtc_jetson_encoder_switch_benchmark EXCLUDE_FROM_ALL
    test/rtc_jetson_encoder_switch_benchmark.cpp "${sdk}/log/log_manager.cpp")
  target_include_directories(rtc_jetson_encoder_switch_benchmark PRIVATE "${sdk}")
  target_link_libraries(rtc_jetson_encoder_switch_benchmark PRIVATE jetson_encoder)
  add_executable(rtc_jetson_h264_encoder_integration_tests test/rtc_jetson_h264_encoder_integration_tests.cpp
    "${sdk}/log/log_manager.cpp")
  target_include_directories(rtc_jetson_h264_encoder_integration_tests PRIVATE "${sdk}")
  target_link_libraries(rtc_jetson_h264_encoder_integration_tests PRIVATE jetson_encoder)
  add_test(NAME rtc_jetson_h264_encoder_integration_tests COMMAND rtc_jetson_h264_encoder_integration_tests)
  set_tests_properties(rtc_jetson_h264_encoder_integration_tests PROPERTIES TIMEOUT 120 LABELS hardware)
endif()
ament_package()
