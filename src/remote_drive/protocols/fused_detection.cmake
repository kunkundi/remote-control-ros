find_package(Protobuf REQUIRED)
protobuf_generate_cpp(fused_sources fused_headers
  "${CMAKE_CURRENT_LIST_DIR}/fused_detection.proto")
add_library(fused_detection_protocol STATIC ${fused_sources} ${fused_headers})
target_link_libraries(fused_detection_protocol PUBLIC protobuf::libprotobuf)
target_include_directories(fused_detection_protocol PUBLIC
  "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_LIST_DIR}")