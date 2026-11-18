foreach(required CONSOLE_EXECUTABLE SDK_LIBRARY READELF_TOOL LDD_TOOL)
  if(NOT EXISTS "${${required}}")
    message(FATAL_ERROR "Missing ${required}: ${${required}}")
  endif()
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C
  "${READELF_TOOL}" -d "${CONSOLE_EXECUTABLE}"
  RESULT_VARIABLE inspect_result OUTPUT_VARIABLE dynamic_entries ERROR_VARIABLE inspect_error)
if(NOT inspect_result EQUAL 0)
  message(FATAL_ERROR "Cannot inspect console linkage: ${inspect_error}")
endif()

get_filename_component(sdk_filename "${SDK_LIBRARY}" NAME)
string(REGEX MATCH "Shared library: \\[([^]]*vtsrtc[^]]*)\\]" sdk_entry "${dynamic_entries}")
if(NOT "${CMAKE_MATCH_1}" STREQUAL "${sdk_filename}")
  message(FATAL_ERROR "Console must link the SDK by filename, not an absolute path: ${sdk_entry}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C
  "${LDD_TOOL}" "${CONSOLE_EXECUTABLE}"
  RESULT_VARIABLE resolve_result OUTPUT_VARIABLE resolved_libraries ERROR_VARIABLE resolve_error)
if(NOT resolve_result EQUAL 0 OR resolved_libraries MATCHES "not found")
  message(FATAL_ERROR "Console has unresolved libraries: ${resolved_libraries}${resolve_error}")
endif()

get_filename_component(sdk_realpath "${SDK_LIBRARY}" REALPATH)
string(FIND "${resolved_libraries}" "${sdk_filename} => ${sdk_realpath} (" sdk_position)
if(sdk_position EQUAL -1)
  message(FATAL_ERROR "Console must load the selected local SDK: ${resolved_libraries}")
endif()
message(STATUS "Console uses the local SDK without an absolute NEEDED entry")