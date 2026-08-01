if(NOT SVRT_BUILD_DRIVER OR NOT WIN32)
  return()
endif()

add_library(driver_svrt SHARED src/driver.cpp src/direct_mode.cpp src/direct_mode.h)
target_compile_features(driver_svrt PRIVATE cxx_std_17)
target_include_directories(driver_svrt PRIVATE ${CMAKE_SOURCE_DIR}/third_party/openvr/headers)
target_link_libraries(driver_svrt PRIVATE d3d11 dxgi)
target_compile_definitions(driver_svrt PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(driver_svrt PROPERTIES
  PREFIX ""
  OUTPUT_NAME "driver_svrt"
  RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/svrt/bin/win64>"
  LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/svrt/bin/win64>")
add_custom_command(TARGET driver_svrt POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/svrt ${CMAKE_BINARY_DIR}/svrt)
