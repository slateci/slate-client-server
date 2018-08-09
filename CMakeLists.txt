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
  ${CMAKE_SOURCE_DIR}/src/SecretCommands.cpp
  ${CMAKE_SOURCE_DIR}/src/UserCommands.cpp
  ${CMAKE_SOURCE_DIR}/src/VOCommands.cpp

  ${CMAKE_SOURCE_DIR}/src/scrypt/util/entropy.c
  ${CMAKE_SOURCE_DIR}/src/scrypt/util/insecure_memzero.c
  ${CMAKE_SOURCE_DIR}/src/scrypt/alg/sha256.c
  ${CMAKE_SOURCE_DIR}/src/scrypt/crypto/crypto_aes.c
  ${CMAKE_SOURCE_DIR}/src/scrypt/crypto/crypto_aesctr.c
  ${CMAKE_SOURCE_DIR}/src/scrypt/crypto/crypto_entropy.c
  ${CMAKE_SOURCE_DIR}/src/scrypt/crypto/crypto_scrypt.c
  ${CMAKE_SOURCE_DIR}/src/scrypt/crypto/crypto_scrypt_smix.c
  ${CMAKE_SOURCE_DIR}/src/scrypt/scryptenc/scryptenc.c
)

SET(SLATE_SERVICE_COMPILE_OPTIONS -DRAPIDJSON_HAS_STDSTRING -O2)

IF(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86_64)$")
  LIST(APPEND SERVICE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/scrypt/cpusupport/cpusupport_x86_sse2.c
    ${CMAKE_SOURCE_DIR}/src/scrypt/crypto/crypto_scrypt_smix_sse2.c
  )
  SET(SLATE_SERVICE_COMPILE_OPTIONS ${SLATE_SERVICE_COMPILE_OPTIONS} 
      -msse2 -DCPUSUPPORT_X86_CPUID -DCPUSUPPORT_X86_SSE2
  )
ENDIF()

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
target_compile_options(slate-service PRIVATE ${SLATE_SERVICE_COMPILE_OPTIONS} )
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

macro(list_contains var value)
  set(${var})
  foreach (value2 ${ARGN})
    if (${value} STREQUAL ${value2})
      set(${var} TRUE)
    endif (${value} STREQUAL ${value2})
  endforeach (value2)
endmacro(list_contains)

MACRO(PARSE_ARGUMENTS prefix arg_names option_names)
  SET(DEFAULT_ARGS)
  FOREACH(arg_name ${arg_names})
    SET(${prefix}_${arg_name})
  ENDFOREACH(arg_name)
  FOREACH(option ${option_names})
    SET(${prefix}_${option} FALSE)
  ENDFOREACH(option)

  SET(current_arg_name DEFAULT_ARGS)
  SET(current_arg_list)
  FOREACH(arg ${ARGN})
    LIST_CONTAINS(is_arg_name ${arg} ${arg_names})
    IF (is_arg_name)
      SET(${prefix}_${current_arg_name} ${current_arg_list})
      SET(current_arg_name ${arg})
      SET(current_arg_list)
    ELSE (is_arg_name)
      LIST_CONTAINS(is_option ${arg} ${option_names})
      IF (is_option)
      SET(${prefix}_${arg} TRUE)
      ELSE (is_option)
      SET(current_arg_list ${current_arg_list} ${arg})
      ENDIF (is_option)
    ENDIF (is_arg_name)
  ENDFOREACH(arg)
  SET(${prefix}_${current_arg_name} ${current_arg_list})
ENDMACRO(PARSE_ARGUMENTS)

macro(slate_add_test TEST_NAME)
  PARSE_ARGUMENTS(${TEST_NAME}_ARGS "SOURCE_FILES;COMPILE_FLAGS;LINK_LIBRARIES" "" ${ARGN})
  add_executable(${TEST_NAME}
    ${BASE_TEST_COMPONENTS}
    ${${TEST_NAME}_ARGS_SOURCE_FILES}
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
      ${${TEST_NAME}_ARGS_LINK_LIBRARIES}
  )
  list(APPEND ALL_TESTS
    tests/${TEST_NAME}
  )
endmacro(slate_add_test)

slate_add_test(test-user-listing
    SOURCE_FILES test/TestUserListing.cpp)

slate_add_test(test-user-creation
    SOURCE_FILES test/TestUserCreation.cpp)

slate_add_test(test-user-info
    SOURCE_FILES test/TestUserInfo.cpp)

slate_add_test(test-user-update
    SOURCE_FILES test/TestUserUpdate.cpp)

slate_add_test(test-user-deletion
    SOURCE_FILES test/TestUserDeletion.cpp)

slate_add_test(test-user-vo-addition
    SOURCE_FILES test/TestUserVOAddition.cpp)

slate_add_test(test-user-vo-removal
    SOURCE_FILES test/TestUserVORemoval.cpp)

slate_add_test(test-user-vo-listing
    SOURCE_FILES test/TestUserVOListing.cpp)

slate_add_test(test-find-user
    SOURCE_FILES test/TestFindUser.cpp)

slate_add_test(test-vo-listing
    SOURCE_FILES test/TestVOListing.cpp)

slate_add_test(test-vo-creation
    SOURCE_FILES test/TestVOCreation.cpp)

slate_add_test(test-vo-deletion
    SOURCE_FILES test/TestVODeletion.cpp)

slate_add_test(test-cluster-creation
    SOURCE_FILES test/TestClusterCreation.cpp)

slate_add_test(test-cluster-listing
    SOURCE_FILES test/TestClusterListing.cpp)

slate_add_test(test-cluster-deletion
    SOURCE_FILES test/TestClusterDeletion.cpp)

slate_add_test(test-cluster-update
    SOURCE_FILES test/TestClusterUpdate.cpp)

slate_add_test(test-cluster-allowed-vo-listing
    SOURCE_FILES test/TestClusterAllowedVOListing.cpp)

slate_add_test(test-cluster-allowed-vo-addition
    SOURCE_FILES test/TestClusterAllowedVOAddition.cpp)

slate_add_test(test-cluster-allowed-vo-deletion
    SOURCE_FILES test/TestClusterAllowedVODeletion.cpp)

slate_add_test(test-app-listing
    SOURCE_FILES test/TestApplicationListing.cpp)

slate_add_test(test-fetch-app-config
    SOURCE_FILES test/TestFetchApplicationConfig.cpp)

slate_add_test(test-app-install
    SOURCE_FILES test/TestApplicationInstall.cpp src/Utilities.cpp
    LINK_LIBRARIES boost_date_time)

slate_add_test(test-instance-listing
    SOURCE_FILES test/TestInstanceListing.cpp)

slate_add_test(test-instance-info-fetching
    SOURCE_FILES test/TestInstanceInfoFetching.cpp)

slate_add_test(test-instance-deletion
    SOURCE_FILES test/TestInstanceDeletion.cpp)
  
foreach(TEST ${ALL_TESTS})
  get_filename_component(TEST_NAME ${TEST} NAME_WE)
  add_test(${TEST_NAME} ${TEST})
endforeach()

add_custom_target(check 
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS ${ALL_TESTS} slate-test-database-server slate-service)
