if(NOT SVRT_BUILD_RECEIVER OR NOT UNIX)
  return()
endif()
include(GNUInstallDirs)
add_executable(svrt-receiver
  main.c
  status.c status.h
  audio.c audio.h)
target_sources(svrt-receiver PRIVATE
  ../gui/pairing.c ../gui/pairing.h
  ../gui/ui.c ../gui/ui.h
  ../gui/ui_anim.c ../gui/ui_anim.h
  ../gui/ui_boot_anim.c ../gui/ui_boot_anim.h)
target_include_directories(svrt-receiver PRIVATE ../gui .)
target_compile_definitions(svrt-receiver PRIVATE
  SVRT_GUI_FONT_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/gui/assets/font/system.ttf"
  SVRT_GUI_STEAMVR_LOGO_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/gui/assets/tex/steamvr-branding.png")
target_compile_features(svrt-receiver PRIVATE c_std_11)
find_package(Threads REQUIRED)
find_package(ALSA REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SVRT_SDL_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SVRT_SDL_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
target_link_libraries(svrt-receiver PRIVATE svrt::svrt Threads::Threads ALSA::ALSA PkgConfig::SVRT_SDL_TTF PkgConfig::SVRT_SDL_IMAGE m)
if(SVRT_BUILD_TESTS)
  pkg_check_modules(SVRT_SDL REQUIRED IMPORTED_TARGET sdl2)
  add_executable(svrt-gui-loadtest ../gui/tests/loadtest.c)
  target_include_directories(svrt-gui-loadtest PRIVATE
    ../gui ${SVRT_SDL_INCLUDE_DIRS} ${SVRT_SDL_TTF_INCLUDE_DIRS}
    ${SVRT_SDL_IMAGE_INCLUDE_DIRS})
  target_compile_features(svrt-gui-loadtest PRIVATE c_std_11)
  target_link_libraries(svrt-gui-loadtest PRIVATE
    PkgConfig::SVRT_SDL PkgConfig::SVRT_SDL_TTF PkgConfig::SVRT_SDL_IMAGE m)
  add_test(NAME svrt-gui-loadtest COMMAND svrt-gui-loadtest)
endif()
install(TARGETS svrt-receiver RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/svrt-receiver.service.in
  ${CMAKE_CURRENT_BINARY_DIR}/svrt-receiver.service
  @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/svrt-receiver.service
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/stearlight.service
  DESTINATION /etc/avahi/services)
install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/force-5ghz.sh
  DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/svrt)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../gui/assets/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/svrt/gui/assets)
