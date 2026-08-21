add_executable(svrt-utility WIN32
  main.cpp
  ui.cpp ui.h
  ui_util.cpp ui_util.h
  pairing.cpp pairing.h
  steamvr.cpp steamvr.h
  network.cpp network.h
  config.cpp config.h
  ../common/mdns_discovery.cpp ../common/mdns_discovery.h)
target_compile_features(svrt-utility PRIVATE cxx_std_17)
target_compile_definitions(svrt-utility PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(svrt-utility PRIVATE ../common)
target_link_libraries(svrt-utility PRIVATE ws2_32 dnsapi iphlpapi shell32)
if(SVRT_BUILD_TESTS)
  add_executable(stearlight-mdns-probe ../common/mdns_probe.cpp ../common/mdns_discovery.cpp)
  target_include_directories(stearlight-mdns-probe PRIVATE ../common)
  target_compile_features(stearlight-mdns-probe PRIVATE cxx_std_17)
  target_link_libraries(stearlight-mdns-probe PRIVATE dnsapi ws2_32 iphlpapi)
endif()
if(SVRT_BUILD_TESTS)
  add_executable(svrt-config-test config.cpp ../tests/config_json_test.cpp)
  target_compile_features(svrt-config-test PRIVATE cxx_std_17)
  target_compile_definitions(svrt-config-test PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX SVRT_CONFIG_TESTING)
  target_link_libraries(svrt-config-test PRIVATE ws2_32 shell32)
  add_test(NAME svrt-config-json COMMAND svrt-config-test)
endif()
set_target_properties(svrt-utility PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/svrt-utility>")
add_custom_command(TARGET svrt-utility POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:svrt-utility>/assets")
