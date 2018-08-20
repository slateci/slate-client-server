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

# -----------------------------------------------------------------------------
# Look for dependencies
SET(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Packages)
FIND_PACKAGE(libcrypto)
FIND_PACKAGE(ssl)
FIND_PACKAGE(CURL)

# -----------------------------------------------------------------------------
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
    ${CURL_CFLAGS}
    ${LIBCRYPTO_CFLAGS}
    ${SSL_CFLAGS}
)

if(STATIC_CLIENT)
  # static building may only work on alpine with a manually built, statically linked copy of nghttp2 installed
  message("Attempting fully static link")
  set(CLIENT_LIBRARIES
    -static
    -static-libstdc++
    -static-libgcc
    ${CURL_LIBRARIES}
    nghttp2
    ${SSL_LDFLAGS}
    ${LIBCRYPTO_LDFLAGS}
    ssh2
    z
  )
else(STATIC_CLIENT)
  set(CLIENT_LIBRARIES
    ${CURL_LIBRARIES}
    ${SSL_LDFLAGS}
    ${LIBCRYPTO_LDFLAGS}
  )
endif(STATIC_CLIENT)

target_link_libraries(slate-client
  PUBLIC
    ${CLIENT_LIBRARIES}
)
target_compile_options(slate-client PRIVATE -DRAPIDJSON_HAS_STDSTRING -Os)
install(TARGETS slate-client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# TODO: uninstall