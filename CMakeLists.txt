cmake_minimum_required(VERSION 3.16)

project(IoTEdgeGateway VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(IOTGW_MONGOOSE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mongoose"
  CACHE PATH "Path to a local mongoose source directory")
option(IOTGW_ENABLE_GSTREAMER "Enable RK3568 gst-launch camera/HLS runtime support" OFF)

find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
find_library(SQLITE3_LIBRARY sqlite3)
if(NOT SQLITE3_INCLUDE_DIR OR NOT SQLITE3_LIBRARY)
  message(FATAL_ERROR
    "SQLite3 development files were not found in the toolchain/sysroot. "
    "Need sqlite3.h and libsqlite3.so to build database support.")
endif()

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp
  @ONLY
)

if(IOTGW_MONGOOSE_DIR)
  if(NOT EXISTS "${IOTGW_MONGOOSE_DIR}/mongoose.c" OR
     NOT EXISTS "${IOTGW_MONGOOSE_DIR}/mongoose.h")
    message(FATAL_ERROR
      "IOTGW_MONGOOSE_DIR must contain mongoose.c and mongoose.h: "
      "${IOTGW_MONGOOSE_DIR}")
  endif()
  set(mongoose_SOURCE_DIR "${IOTGW_MONGOOSE_DIR}")
else()
  FetchContent_Declare(
    mongoose
    GIT_REPOSITORY https://github.com/cesanta/mongoose.git
    GIT_TAG 7.20
  )
  FetchContent_MakeAvailable(mongoose)
endif()

add_library(mongoose_lib STATIC ${mongoose_SOURCE_DIR}/mongoose.c)
target_include_directories(mongoose_lib PUBLIC ${mongoose_SOURCE_DIR})

set(IOTGW_COMMON_SOURCES
  src/core/common/config/config_validator.cpp
  src/core/common/logger/file_logger.cpp
  src/core/device/manager/device_registry.cpp
  src/services/system_services/camera/camera_manager.cpp
  src/services/storage/database_service.cpp
  src/core/protocol_adapters/mqtt_adapter/mqtt_client.cpp
  src/core/protocol_adapters/zigbee_adapter/zigbee_serial_adapter.cpp
  src/services/web_services/api/system_api.cpp
  src/services/web_services/api/device_api.cpp
  src/services/web_services/api/zigbee_api.cpp
  src/services/web_services/api/camera_api.cpp
  src/services/web_services/api/database_api.cpp
  src/services/web_services/api/rule_api.cpp
)

add_library(iotgw_common STATIC ${IOTGW_COMMON_SOURCES})
target_include_directories(iotgw_common
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${SQLITE3_INCLUDE_DIR}
)
target_link_libraries(iotgw_common PUBLIC mongoose_lib ${SQLITE3_LIBRARY})

if(IOTGW_ENABLE_GSTREAMER)
  target_compile_definitions(iotgw_common PUBLIC IOTGW_ENABLE_GSTREAMER=1)
endif()

add_executable(iotgw_gateway
  src/gateway/main.cpp
  src/gateway/gateway_core.cpp
)
target_link_libraries(iotgw_gateway PRIVATE iotgw_common)

if(MSVC)
  target_compile_options(iotgw_common PRIVATE /W4)
  target_compile_options(iotgw_gateway PRIVATE /W4)
else()
  target_compile_options(iotgw_common PRIVATE -Wall -Wextra -Wpedantic)
  target_compile_options(iotgw_gateway PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS iotgw_gateway RUNTIME DESTINATION bin)
install(DIRECTORY config www DESTINATION .)
