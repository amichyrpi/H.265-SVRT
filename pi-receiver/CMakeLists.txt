if(NOT SVRT_BUILD_RECEIVER OR NOT UNIX)
  return()
endif()
include(GNUInstallDirs)
add_executable(svrt-receiver src/main.c src/status_server.c src/status_server.h)
target_compile_features(svrt-receiver PRIVATE c_std_11)
find_package(Threads REQUIRED)
target_link_libraries(svrt-receiver PRIVATE svrt::svrt Threads::Threads)
install(TARGETS svrt-receiver RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/svrt-receiver.service.in
  ${CMAKE_CURRENT_BINARY_DIR}/svrt-receiver.service
  @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/svrt-receiver.service
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
