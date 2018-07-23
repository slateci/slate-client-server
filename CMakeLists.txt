cmake_minimum_required (VERSION 3.0.0 FATAL_ERROR)
cmake_policy(VERSION 3.0.0)

SET(CMAKE_CXX_STANDARD 11)

# -----------------------------------------------------------------------------
# Set up installation
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
# Main executable
LIST(APPEND SERVICE_SOURCES
  ${CMAKE_SOURCE_DIR}/src/slate_service.cpp
  ${CMAKE_SOURCE_DIR}/src/Entities.cpp
  ${CMAKE_SOURCE_DIR}/src/KubeInterface.cpp
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
    boost_date_time
    yaml-cpp
)
target_compile_options(slate-service PRIVATE -DRAPIDJSON_HAS_STDSTRING)
install(TARGETS slate-service RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# TODO: uninstall

# -----------------------------------------------------------------------------
# Testing
configure_file(test/CTestCustom.cmake CTestCustom.cmake)
enable_testing()

add_executable(slate-test-database-server
  test/DBServer.cpp
  test/Process.cpp
)
target_include_directories (slate-test-database-server
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(slate-test-database-server
  PUBLIC
    boost_system
    pthread
    ssl
    crypto
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

list(APPEND BASE_TEST_COMPONENTS
  test/test_main.cpp
  test/HTTPRequests.cpp
  test/Process.cpp
)

macro(slate_add_test TEST_NAME TEST_CODE)
  add_executable(${TEST_NAME}
    ${BASE_TEST_COMPONENTS}
    ${TEST_CODE}
  )
  target_compile_options(${TEST_NAME} PRIVATE -g -DRAPIDJSON_HAS_STDSTRING)
  target_include_directories (${TEST_NAME}
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
  )
  target_link_libraries(${TEST_NAME}
    PUBLIC
      curl
      boost_system
      pthread
      ssl
      crypto
  )
  list(APPEND ALL_TESTS
    tests/${TEST_NAME}
  )
endmacro(slate_add_test)

slate_add_test(test-user-listing
    test/TestUserListing.cpp)

slate_add_test(test-user-creation
    test/TestUserCreation.cpp)

slate_add_test(test-user-info
    test/TestUserInfo.cpp)

slate_add_test(test-user-update
    test/TestUserUpdate.cpp)

slate_add_test(test-user-deletion
    test/TestUserDeletion.cpp)

slate_add_test(test-user-vo-addition
    test/TestUserVOAddition.cpp)

slate_add_test(test-user-vo-removal
    test/TestUserVORemoval.cpp)

slate_add_test(test-user-vo-listing
    test/TestUserVOListing.cpp)

slate_add_test(test-vo-listing
    test/TestVOListing.cpp)

slate_add_test(test-vo-creation
    test/TestVOCreation.cpp)

slate_add_test(test-vo-deletion
    test/TestVODeletion.cpp)

foreach(TEST ${ALL_TESTS})
  get_filename_component(TEST_NAME ${TEST} NAME_WE)
  add_test(${TEST_NAME} ${TEST})
endforeach()

add_custom_target(check 
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS ${ALL_TESTS} slate-test-database-server slate-service)
