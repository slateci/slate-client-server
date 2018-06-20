cmake_minimum_required (VERSION 3.0.0 FATAL_ERROR)
cmake_policy(VERSION 3.0.0)

SET(CMAKE_CXX_STANDARD 11)

# -----------------------------------------------------------------------------
# Set up installation
# -----------------------------------------------------------------------------
# Install to correct subdirectories on all platforms
include(GNUInstallDirs)
if(APPLE)
  # See: https://cmake.org/Wiki/CMake_RPATH_handling
  set(CMAKE_MACOSX_RPATH TRUE)
  set(CMAKE_SKIP_BUILD_RPATH FALSE)
  set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
  set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
endif(APPLE)

# Main executable
LIST(APPEND SERVICE_SOURCES
  ${CMAKE_SOURCE_DIR}/src/slate_service.cpp
  ${CMAKE_SOURCE_DIR}/src/Entities.cpp
  ${CMAKE_SOURCE_DIR}/src/PersistentStore.cpp
  ${CMAKE_SOURCE_DIR}/src/Utilities.cpp
  ${CMAKE_SOURCE_DIR}/src/ApplicationCommands.cpp
  ${CMAKE_SOURCE_DIR}/src/ApplicationInstanceCommands.cpp
  ${CMAKE_SOURCE_DIR}/src/ClusterCommands.cpp
  ${CMAKE_SOURCE_DIR}/src/UserCommands.cpp
  ${CMAKE_SOURCE_DIR}/src/VOCommands.cpp
)
add_executable(slate-service ${SERVICE_SOURCES})
target_include_directories (slate-service
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(slate-service
  PUBLIC
    boost_system
    aws-cpp-sdk-dynamodb
    aws-cpp-sdk-core
    curl
    pthread
    ssl
    crypto
)
install(TARGETS slate-service RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# TODO: uninstall