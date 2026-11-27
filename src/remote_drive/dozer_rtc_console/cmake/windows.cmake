cmake_minimum_required(VERSION 3.24)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
   CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|arm|aarch")
  message(FATAL_ERROR "The Windows console requires MSVC and an x64 generator (-A x64).")
endif()

include(CTest)
include(FetchContent)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" config_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin/${config}")
endforeach()
add_compile_options(/utf-8 /bigobj)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)

get_filename_component(workspace "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(DOZER_WEBRTC_ROOT "${workspace}/vendor/windows/webrtc-win" CACHE PATH
  "Windows x64 WebRTC package with include/ and lib/webrtc.lib")
foreach(required include/api/peer_connection_interface.h lib/webrtc.lib)
  if(NOT EXISTS "${DOZER_WEBRTC_ROOT}/${required}")
    message(FATAL_ERROR "Missing ${DOZER_WEBRTC_ROOT}/${required}. Extract the matching Windows WebRTC package or set DOZER_WEBRTC_ROOT; see README.md.")
  endif()
endforeach()

set(DOZER_NVCODEC_ROOT "${workspace}/vendor/windows/Video_Codec_SDK_11.0.10" CACHE PATH
  "NVIDIA Video Codec SDK 11.0.10 (Interface, Lib/x64 and Samples)")
foreach(required Interface/nvcuvid.h Lib/x64/nvcuvid.lib Samples/NvCodec/NvDecoder/NvDecoder.cpp)
  if(NOT EXISTS "${DOZER_NVCODEC_ROOT}/${required}")
    message(FATAL_ERROR "Missing ${DOZER_NVCODEC_ROOT}/${required}. Set DOZER_NVCODEC_ROOT to the extracted NVIDIA Video Codec SDK; see README.md.")
  endif()
endforeach()
find_package(CUDAToolkit REQUIRED)

# 固定第三方版本；离线构建可用 FETCHCONTENT_SOURCE_DIR_<NAME> 指向已解压源码。
FetchContent_Declare(sdl3
  URL https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-3.4.12
  URL_HASH SHA256=b68381f06a7580e63400b3b6eb547ec57d8c3ebde70f9f40e0aba530ba05da27)
FetchContent_Declare(imgui
  URL https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.92.6
  URL_HASH SHA256=5b17c01f69545bde732b14936d89ce0f508adb83e8b56fa82448371845172bc3)
FetchContent_Declare(nlohmann_json
  URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
  URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187)
FetchContent_Declare(nanopb
  URL https://codeload.github.com/nanopb/nanopb/tar.gz/refs/tags/0.4.9
  URL_HASH SHA256=524882ce9b6b8abeab3d458b9f15449c3f11d60c099eb388d8732d1bf2944eb3
  SOURCE_SUBDIR dozer-unused)
FetchContent_Declare(asio
  URL https://codeload.github.com/chriskohlhoff/asio/tar.gz/refs/tags/asio-1-32-0
  URL_HASH SHA256=f1b94b80eeb00bb63a3c8cef5047d4e409df4d8a3fe502305976965827d95672
  SOURCE_SUBDIR dozer-unused)
FetchContent_Declare(spdlog
  URL https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.14.1
  URL_HASH SHA256=1586508029a7d0670dfcb2d97575dcdc242d3868a259742b69f100801ab4e16b)
FetchContent_Declare(protobuf
  URL https://codeload.github.com/protocolbuffers/protobuf/tar.gz/refs/tags/v21.12
  URL_HASH SHA256=22fdaf641b31655d4b2297f9981fa5203b2866f8332d3c6333f6b0107bb320de)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sdl3 imgui nlohmann_json nanopb asio spdlog protobuf)

add_library(console_nanopb STATIC "${nanopb_SOURCE_DIR}/pb_common.c"
  "${nanopb_SOURCE_DIR}/pb_decode.c" "${nanopb_SOURCE_DIR}/pb_encode.c")
target_include_directories(console_nanopb PUBLIC "${nanopb_SOURCE_DIR}")

add_library(console_imgui STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp" "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp" "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
target_include_directories(console_imgui PUBLIC "${imgui_SOURCE_DIR}")
target_link_libraries(console_imgui PUBLIC SDL3::SDL3 OpenGL::GL)
add_library(imgui::imgui ALIAS console_imgui)

set(audio "${workspace}/src/drivers/dozer_audio")
add_library(dozer_audio STATIC "${audio}/src/audio_device.cpp")
target_include_directories(dozer_audio PUBLIC "${audio}/include")
target_link_libraries(dozer_audio PUBLIC SDL3::SDL3 Threads::Threads)
add_library(dozer_audio::dozer_audio ALIAS dozer_audio)

set(common "${workspace}/src/remote_drive/dozer_rtc_common")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated}")
add_custom_command(
  OUTPUT "${generated}/fused_detection.pb.cc" "${generated}/fused_detection.pb.h"
  COMMAND protobuf::protoc --cpp_out "${generated}" -I "${common}/protocols"
    "${common}/protocols/fused_detection.proto"
  DEPENDS protobuf::protoc "${common}/protocols/fused_detection.proto"
  VERBATIM)
add_library(dozer_rtc_common STATIC
  "${common}/src/process_runtime.cpp"
  "${common}/src/vehicle_control_protocol.cpp" "${common}/src/view_control_protocol.cpp"
  "${common}/protocols/vehicle/generated/rtc_vehicle.pb.c"
  "${generated}/fused_detection.pb.cc" "${generated}/fused_detection.pb.h")
target_include_directories(dozer_rtc_common PUBLIC "${common}/include"
  "${common}/protocols/vehicle/generated" "${generated}")
target_link_libraries(dozer_rtc_common PUBLIC nlohmann_json::nlohmann_json
  console_nanopb protobuf::libprotobuf)
add_library(dozer_rtc_common::dozer_rtc_common ALIAS dozer_rtc_common)

set(sdk_root "${workspace}/src/remote_drive/dozer_rtc_sdk")
set(sdk "${sdk_root}/src")
add_library(console_webrtc INTERFACE)
target_include_directories(console_webrtc SYSTEM INTERFACE
  "${DOZER_WEBRTC_ROOT}/include" "${DOZER_WEBRTC_ROOT}/include/third_party/abseil-cpp"
  "${DOZER_WEBRTC_ROOT}/include/third_party/jsoncpp/source/include"
  "${DOZER_WEBRTC_ROOT}/include/third_party/jsoncpp/generated"
  "${DOZER_WEBRTC_ROOT}/include/third_party/libyuv/include"
  "${DOZER_WEBRTC_ROOT}/include/third_party/boringssl/src/include")
target_compile_definitions(console_webrtc INTERFACE WEBRTC_WIN NDEBUG _ITERATOR_DEBUG_LEVEL=0)
target_link_libraries(console_webrtc INTERFACE "${DOZER_WEBRTC_ROOT}/lib/webrtc.lib"
  ws2_32 winmm secur32 msdmo dmoguids wmcodecdspuuid strmiids advapi32)

add_library(console_nvdec STATIC "${sdk}/video/decode/nvdec_factory.cpp"
  "${sdk}/log/log_manager.cpp" "${DOZER_NVCODEC_ROOT}/Samples/NvCodec/NvDecoder/NvDecoder.cpp")
target_include_directories(console_nvdec PRIVATE "${sdk}" "${DOZER_NVCODEC_ROOT}/Interface"
  "${DOZER_NVCODEC_ROOT}/Samples/NvCodec" "${DOZER_NVCODEC_ROOT}/Samples/Utils")
target_link_libraries(console_nvdec PUBLIC console_webrtc PRIVATE spdlog::spdlog_header_only
  CUDA::cuda_driver "${DOZER_NVCODEC_ROOT}/Lib/x64/nvcuvid.lib" delayimp)
target_link_options(console_nvdec INTERFACE /DELAYLOAD:nvcuda.dll /DELAYLOAD:nvcuvid.dll)

add_library(vtsrtc SHARED
  "${sdk}/log/log_webrtc_hook.cpp"
  "${sdk}/rtc_device_manager.cpp" "${sdk}/rtc_connection.cpp"
  "${sdk}/rtc_connection_manager.cpp" "${sdk}/rtc.cpp" "${sdk}/c_rtc.cpp"
  "${sdk}/statistics/rtc_statistics.cpp" "${sdk}/packet/pack.cpp"
  "${sdk}/packet/packetizer.cpp" "${sdk}/packet/depacketizer.cpp")
target_include_directories(vtsrtc PUBLIC "${sdk_root}/include/vtsrtc"
  PRIVATE "${sdk}" "${sdk_root}/signaling" "${asio_SOURCE_DIR}/asio/include"
  "${workspace}/vendor/Simple-Web-Server" "${workspace}/vendor/Simple-WebSocket-Server")
set_target_properties(vtsrtc console_nvdec PROPERTIES CXX_STANDARD 14 MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_compile_definitions(vtsrtc PRIVATE RTC_DLL_EXPORTS
  USE_STANDALONE_ASIO ASIO_STANDALONE ENABLE_ENCODE_PERF_STATS=0)
target_link_libraries(vtsrtc PRIVATE console_nvdec
  nlohmann_json::nlohmann_json spdlog::spdlog_header_only Threads::Threads)
add_library(dozer_rtc_sdk::vtsrtc ALIAS vtsrtc)

foreach(dependency sdl3 imgui nlohmann_json nanopb asio spdlog protobuf)
  if(dependency STREQUAL "nlohmann_json")
    set(license LICENSE.MIT)
  elseif(dependency STREQUAL "asio")
    set(license asio/LICENSE_1_0.txt)
  elseif(dependency STREQUAL "spdlog" OR dependency STREQUAL "protobuf")
    set(license LICENSE)
  else()
    set(license LICENSE.txt)
  endif()
  install(FILES "${${dependency}_SOURCE_DIR}/${license}"
    DESTINATION "share/licenses/${dependency}" COMPONENT Runtime)
endforeach()
install(FILES "${DOZER_WEBRTC_ROOT}/LICENSE.md"
  DESTINATION share/licenses/webrtc COMPONENT Runtime OPTIONAL)
foreach(dependency Simple-Web-Server Simple-WebSocket-Server)
  install(FILES "${workspace}/vendor/${dependency}/LICENSE"
    DESTINATION "share/licenses/${dependency}" COMPONENT Runtime)
endforeach()

if(BUILD_TESTING)
  add_executable(rtc_console_nvdec_tests "${sdk_root}/test/nvdec_h264_tests.cpp")
  target_include_directories(rtc_console_nvdec_tests PRIVATE "${sdk}")
  target_link_libraries(rtc_console_nvdec_tests PRIVATE console_nvdec)
  set_target_properties(rtc_console_nvdec_tests PROPERTIES CXX_STANDARD 14 MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  add_test(NAME rtc_console_nvdec_tests COMMAND rtc_console_nvdec_tests)
  set_tests_properties(rtc_console_nvdec_tests PROPERTIES TIMEOUT 20 LABELS hardware)
  add_executable(rtc_console_audio_device_tests "${audio}/test/audio_device_tests.cpp")
  target_link_libraries(rtc_console_audio_device_tests PRIVATE dozer_audio)
  add_test(NAME rtc_console_audio_device_tests COMMAND rtc_console_audio_device_tests)
  set_tests_properties(rtc_console_audio_device_tests PROPERTIES TIMEOUT 15
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")
  add_executable(rtc_console_sdk_abi_tests "${sdk_root}/test/rtc_video_delay_abi_tests.cpp")
  target_link_libraries(rtc_console_sdk_abi_tests PRIVATE vtsrtc)
  add_test(NAME rtc_console_sdk_abi_tests COMMAND rtc_console_sdk_abi_tests)
  set_tests_properties(rtc_console_sdk_abi_tests PROPERTIES TIMEOUT 10)
endif()
