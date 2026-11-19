function(dozer_install_rtc_app executable)
  set(destination "lib/${PROJECT_NAME}")
  set_target_properties(${executable} PROPERTIES INSTALL_RPATH "$ORIGIN")

  # Older symlink installs point at a build executable or an external SDK.
  # CMake skips copying when a destination link resolves to the source file.
  string(CONFIGURE [[
    foreach(name IN ITEMS "@executable@" "$<TARGET_FILE_NAME:dozer_rtc_sdk::vtsrtc>")
      set(path "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/@destination@/${name}")
      if(IS_SYMLINK "${path}")
        file(REMOVE "${path}")
      endif()
    endforeach()
  ]] remove_old_links @ONLY)
  install(CODE "${remove_old_links}" COMPONENT Runtime)
  install(TARGETS ${executable} RUNTIME DESTINATION "${destination}" COMPONENT Runtime)
  install(FILES "$<TARGET_FILE:dozer_rtc_sdk::vtsrtc>" DESTINATION "${destination}" COMPONENT Runtime)
endfunction()
