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
LIST(APPEND CLIENT_SOURCES
  ${CMAKE_SOURCE_DIR}/src/slate_client.cpp
  ${CMAKE_SOURCE_DIR}/src/Client.cpp
  ${CMAKE_SOURCE_DIR}/src/HTTPRequests.cpp
  ${CMAKE_SOURCE_DIR}/src/SecretLoading.cpp
)
add_executable(slate-client ${CLIENT_SOURCES})
target_include_directories (slate-client
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(slate-client
  PUBLIC
    curl
    ssl
    crypto
)
target_compile_options(slate-client PRIVATE -DRAPIDJSON_HAS_STDSTRING)
install(TARGETS slate-client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# TODO: uninstall