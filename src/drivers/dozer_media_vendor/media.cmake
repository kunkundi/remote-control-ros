get_filename_component(dozer_media_prefix "${CMAKE_CURRENT_LIST_DIR}/../jetson" ABSOLUTE)
set(SDL3_DIR "${dozer_media_prefix}/lib/cmake/SDL3")
set(imgui_DIR "${dozer_media_prefix}/lib/cmake/imgui")

if(NOT TARGET dozer_media_vendor::headers)
  add_library(dozer_media_vendor::headers INTERFACE IMPORTED)
  set_target_properties(dozer_media_vendor::headers PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${dozer_media_prefix}/include")
  add_library(dozer_media_vendor::yuv STATIC IMPORTED)
  set_target_properties(dozer_media_vendor::yuv PROPERTIES
    IMPORTED_LOCATION "${dozer_media_prefix}/lib/libyuv.a"
    INTERFACE_INCLUDE_DIRECTORIES "${dozer_media_prefix}/include")
  add_library(dozer_media_vendor::nanopb STATIC IMPORTED)
  set_target_properties(dozer_media_vendor::nanopb PROPERTIES
    IMPORTED_LOCATION "${dozer_media_prefix}/lib/libprotobuf-nanopb.a"
    INTERFACE_INCLUDE_DIRECTORIES "${dozer_media_prefix}/include/nanopb")
endif()
