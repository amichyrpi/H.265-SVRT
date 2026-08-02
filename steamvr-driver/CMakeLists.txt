if(NOT SVRT_BUILD_DRIVER OR NOT WIN32)
  return()
endif()

set(SVRT_OPENVR_INCLUDE "${CMAKE_SOURCE_DIR}/third_party/openvr/headers")
if(NOT EXISTS "${SVRT_OPENVR_INCLUDE}/openvr_driver.h")
  include(FetchContent)
  FetchContent_Declare(openvr_sdk
    GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
    GIT_TAG v2.15.6
    GIT_SHALLOW TRUE)
  FetchContent_GetProperties(openvr_sdk)
  if(NOT openvr_sdk_POPULATED)
    FetchContent_Populate(openvr_sdk)
  endif()
  set(SVRT_OPENVR_INCLUDE "${openvr_sdk_SOURCE_DIR}/headers")
endif()

add_library(driver_svrt SHARED
  src/driver.cpp
  src/audio_transport.cpp src/audio_transport.h
  src/direct_mode.cpp src/direct_mode.h
  src/receiver_link.cpp src/receiver_link.h)
target_compile_features(driver_svrt PRIVATE cxx_std_17)
target_include_directories(driver_svrt PRIVATE "${SVRT_OPENVR_INCLUDE}")
target_link_libraries(driver_svrt PRIVATE d3d11 dxgi ws2_32 ole32 uuid)
target_compile_definitions(driver_svrt PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(driver_svrt PROPERTIES
  PREFIX ""
  OUTPUT_NAME "driver_svrt"
  RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/svrt/bin/win64>"
  LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/svrt/bin/win64>")
add_custom_command(TARGET driver_svrt POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/svrt ${CMAKE_BINARY_DIR}/svrt)
