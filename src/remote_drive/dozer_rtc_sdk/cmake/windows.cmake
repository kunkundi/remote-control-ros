include(FetchContent)
find_package(Threads REQUIRED)
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|arm|aarch")
  message(FATAL_ERROR "The Windows RTC SDK requires MSVC and an x64 generator (-A x64).")
endif()

get_filename_component(sdk_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(workspace "${sdk_root}/../../.." ABSOLUTE)
set(DOZER_WEBRTC_ROOT "${workspace}/vendor/windows/webrtc-win" CACHE PATH
  "Windows x64 WebRTC package with include/ and lib/webrtc.lib")
foreach(required include/api/peer_connection_interface.h lib/webrtc.lib)
  if(NOT EXISTS "${DOZER_WEBRTC_ROOT}/${required}")
    message(FATAL_ERROR "Missing ${DOZER_WEBRTC_ROOT}/${required}. Set DOZER_WEBRTC_ROOT; see vendor/windows/README.md.")
  endif()
endforeach()
set(DOZER_NVCODEC_ROOT "${workspace}/vendor/windows/Video_Codec_SDK_11.0.10" CACHE PATH
  "NVIDIA Video Codec SDK 11.0.10 (Interface, Lib/x64 and Samples)")
foreach(required Interface/nvcuvid.h Lib/x64/nvcuvid.lib Samples/NvCodec/NvDecoder/NvDecoder.cpp)
  if(NOT EXISTS "${DOZER_NVCODEC_ROOT}/${required}")
    message(FATAL_ERROR "Missing ${DOZER_NVCODEC_ROOT}/${required}. Set DOZER_NVCODEC_ROOT; see vendor/windows/README.md.")
  endif()
endforeach()
find_package(CUDAToolkit REQUIRED)

FetchContent_Declare(nlohmann_json
  URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
  URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187)
FetchContent_Declare(asio
  URL https://codeload.github.com/chriskohlhoff/asio/tar.gz/refs/tags/asio-1-32-0
  URL_HASH SHA256=f1b94b80eeb00bb63a3c8cef5047d4e409df4d8a3fe502305976965827d95672
  SOURCE_SUBDIR dozer-unused)
FetchContent_Declare(spdlog
  URL https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.14.1
  URL_HASH SHA256=1586508029a7d0670dfcb2d97575dcdc242d3868a259742b69f100801ab4e16b)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json asio spdlog)

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

install(TARGETS vtsrtc RUNTIME DESTINATION bin/Win64 COMPONENT RtcSdk
  ARCHIVE DESTINATION lib/Win64 COMPONENT RtcSdk)
install(FILES "${sdk_root}/include/vtsrtc/c_rtc.h" DESTINATION include/vtsrtc COMPONENT RtcSdk)
install(FILES "${sdk_root}/ue4/VtsRtcApi.h" DESTINATION include COMPONENT RtcSdk)
install(FILES "${sdk_root}/ue4/VtsRtc.Build.cs" "${sdk_root}/ue4/README.md" DESTINATION . COMPONENT RtcSdk)
install(FILES "${sdk_root}/ue4/rtc.example.cfg" DESTINATION config COMPONENT RtcSdk)
foreach(component Runtime RtcSdk)
  install(FILES "${asio_SOURCE_DIR}/asio/LICENSE_1_0.txt" DESTINATION share/licenses/asio COMPONENT ${component})
  install(FILES "${nlohmann_json_SOURCE_DIR}/LICENSE.MIT" DESTINATION share/licenses/nlohmann_json COMPONENT ${component})
  install(FILES "${spdlog_SOURCE_DIR}/LICENSE" DESTINATION share/licenses/spdlog COMPONENT ${component})
  install(FILES "${DOZER_WEBRTC_ROOT}/LICENSE.md" DESTINATION share/licenses/webrtc COMPONENT ${component} OPTIONAL)
  foreach(dependency Simple-Web-Server Simple-WebSocket-Server)
    install(FILES "${workspace}/vendor/${dependency}/LICENSE" DESTINATION "share/licenses/${dependency}" COMPONENT ${component})
  endforeach()
endforeach()
