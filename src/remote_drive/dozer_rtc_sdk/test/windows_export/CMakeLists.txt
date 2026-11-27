cmake_minimum_required(VERSION 3.24)
project(vtsrtc_export_test LANGUAGES CXX)

set(VTSRTC_PACKAGE "" CACHE PATH "Extracted VtsRtc module directory")
add_executable(sdk_consumer main.cpp)
target_include_directories(sdk_consumer PRIVATE "${VTSRTC_PACKAGE}/include")
target_compile_options(sdk_consumer PRIVATE /utf-8)
target_link_libraries(sdk_consumer PRIVATE "${VTSRTC_PACKAGE}/lib/Win64/vtsrtc.lib" delayimp)
target_link_options(sdk_consumer PRIVATE /DELAYLOAD:vtsrtc.dll)
add_custom_command(TARGET sdk_consumer POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
  "${VTSRTC_PACKAGE}/bin/Win64/vtsrtc.dll" "$<TARGET_FILE_DIR:sdk_consumer>" VERBATIM)

enable_testing()
add_test(NAME vtsrtc_exported_api COMMAND sdk_consumer)
