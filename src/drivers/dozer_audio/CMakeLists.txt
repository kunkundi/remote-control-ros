cmake_minimum_required(VERSION 3.18)
project(dozer_audio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(Threads REQUIRED)
find_package(rclcpp REQUIRED)
find_package(dozer_msgs REQUIRED)
find_package(dozer_media_vendor REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

add_library(dozer_audio STATIC src/audio_device.cpp)
target_include_directories(dozer_audio PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(dozer_audio PUBLIC SDL3::SDL3 Threads::Threads)

add_executable(audio_capture_node src/main.cpp src/audio_capture_node.cpp)
target_link_libraries(audio_capture_node dozer_audio)
ament_target_dependencies(audio_capture_node rclcpp dozer_msgs)
install(TARGETS audio_capture_node DESTINATION lib/${PROJECT_NAME})

install(TARGETS dozer_audio EXPORT export_dozer_audio
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
ament_export_targets(export_dozer_audio HAS_LIBRARY_TARGET)
ament_export_dependencies(dozer_media_vendor SDL3 Threads)

if(BUILD_TESTING)
  enable_testing()
  add_executable(audio_device_tests test/audio_device_tests.cpp)
  target_link_libraries(audio_device_tests PRIVATE dozer_audio)
  add_test(NAME audio_device_tests COMMAND audio_device_tests)
  set_tests_properties(audio_device_tests PROPERTIES
    TIMEOUT 15
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")
  add_executable(audio_capture_node_tests
    test/audio_capture_node_tests.cpp src/audio_capture_node.cpp)
  target_include_directories(audio_capture_node_tests PRIVATE src)
  target_link_libraries(audio_capture_node_tests dozer_audio)
  ament_target_dependencies(audio_capture_node_tests rclcpp dozer_msgs)
  add_test(NAME audio_capture_node_tests COMMAND audio_capture_node_tests)
  set_tests_properties(audio_capture_node_tests PROPERTIES
    TIMEOUT 15
    ENVIRONMENT "SDL_AUDIODRIVER=dummy")
endif()

ament_package()
