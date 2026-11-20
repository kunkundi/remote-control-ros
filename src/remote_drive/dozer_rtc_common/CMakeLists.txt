cmake_minimum_required(VERSION 3.18)
project(dozer_rtc_common LANGUAGES C CXX)

find_package(ament_cmake REQUIRED)
find_package(dozer_media_vendor REQUIRED)
find_package(Protobuf REQUIRED)

protobuf_generate_cpp(fused_sources fused_headers protocols/fused_detection.proto)
set(vehicle_generated "${CMAKE_CURRENT_SOURCE_DIR}/protocols/vehicle/generated")
add_library(${PROJECT_NAME} STATIC
  src/process_runtime.cpp
  src/vehicle_control_protocol.cpp
  src/view_control_protocol.cpp
  "${vehicle_generated}/rtc_vehicle.pb.c"
  ${fused_sources} ${fused_headers})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>"
  "$<BUILD_INTERFACE:${vehicle_generated}>"
  "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>")
target_link_libraries(${PROJECT_NAME} PUBLIC
  dozer_media_vendor::headers dozer_media_vendor::nanopb protobuf::libprotobuf)

install(TARGETS ${PROJECT_NAME} EXPORT export_common ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME})
install(FILES ${fused_headers} "${vehicle_generated}/rtc_vehicle.pb.h" DESTINATION include/${PROJECT_NAME})
ament_export_targets(export_common HAS_LIBRARY_TARGET)
ament_export_dependencies(dozer_media_vendor Protobuf)
ament_package()
