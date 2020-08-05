cmake_minimum_required (VERSION 3.0.0 FATAL_ERROR)
cmake_policy(VERSION 3.0.0)

SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_C_STANDARD 11)

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
# Figure out what to build
# By default, build everything
if(NOT DEFINED BUILD_CLIENT)
  set(BUILD_CLIENT True)
endif()
if(NOT DEFINED BUILD_SERVER)
  set(BUILD_SERVER True)
endif()
if(${BUILD_SERVER})
  if(NOT DEFINED BUILD_SERVER_TESTS)
    set(BUILD_SERVER_TESTS True)
  endif()
else()
  if(BUILD_SERVER_TESTS)
    message(FATAL_ERROR "Building the server tests requires building the server")
  endif()
  set(BUILD_SERVER_TESTS False)
endif()

if(${BUILD_SERVER_TESTS} AND NOT ${BUILD_SERVER})
	message(FATAL_ERROR "Building the server tests requires building the server")
endif()
if(BUILD_CLIENT)
  message("Will build client")
endif()
if(BUILD_SERVER)
  message("Will build server")
  if(BUILD_SERVER_TESTS)
    message("Will build server tests")
  endif()
endif()
if(NOT ${BUILD_CLIENT} AND NOT ${BUILD_SERVER})
  message(WARNING "nothing will be built. Is this what you want?")
endif()

# Cache settings
set(BUILD_CLIENT ${BUILD_CLIENT} CACHE BOOL "Build the client")
set(BUILD_SERVER ${BUILD_SERVER} CACHE BOOL "Build the server")
set(BUILD_SERVER_TESTS ${BUILD_SERVER_TESTS} CACHE BOOL "Build the server tests")

set(AWS_SDK_VERSION "1.7.25" CACHE STRING "The AWS SDK version to downlaod and build")

# -----------------------------------------------------------------------------
# Look for dependencies
SET(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Packages)
# These are needed by all components
FIND_PACKAGE(ZLIB)
FIND_PACKAGE(libcrypto)
FIND_PACKAGE(ssl)
FIND_PACKAGE(CURL)
# These are needed only by the server
if(BUILD_SERVER)
  FIND_PACKAGE(Boost COMPONENTS date_time system)
  FIND_PACKAGE(yaml-cpp)
endif()

# -----------------------------------------------------------------------------
# Version embedding
if(BUILD_SERVER)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/server_version.h ${CMAKE_BINARY_DIR}/server_version.h_
    COMMAND ${CMAKE_SOURCE_DIR}/cmake/embed_version.sh ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} server
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
  LIST(APPEND EMBED_VERSION_DEPS ${CMAKE_BINARY_DIR}/server_version.h_)
endif()
if(BUILD_CLIENT)
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/client_version.h ${CMAKE_BINARY_DIR}/client_version.h_
    COMMAND ${CMAKE_SOURCE_DIR}/cmake/embed_version.sh ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} client
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
  LIST(APPEND EMBED_VERSION_DEPS ${CMAKE_BINARY_DIR}/client_version.h_)
endif()
add_custom_target(embed_version
  DEPENDS ${EMBED_VERSION_DEPS} ${CMAKE_SOURCE_DIR}/cmake/embed_version.sh
)

# -----------------------------------------------------------------------------
# Client executable
if(BUILD_CLIENT)
  LIST(APPEND CLIENT_SOURCES
    ${CMAKE_SOURCE_DIR}/src/client/slate_client.cpp
    ${CMAKE_SOURCE_DIR}/src/client/Client.cpp
    ${CMAKE_SOURCE_DIR}/src/client/ClusterRegistration.cpp
    ${CMAKE_SOURCE_DIR}/src/client/Completion.cpp
    ${CMAKE_SOURCE_DIR}/src/client/SecretLoading.cpp
    ${CMAKE_SOURCE_DIR}/src/client/cluster_components/FederationRBAC.cpp
    ${CMAKE_SOURCE_DIR}/src/client/cluster_components/IngressController.cpp
    ${CMAKE_SOURCE_DIR}/src/client/cluster_components/PrometheusMonitoring.cpp
    ${CMAKE_SOURCE_DIR}/src/Archive.cpp
    ${CMAKE_SOURCE_DIR}/src/FileHandle.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/HTTPRequests.cpp
    ${CMAKE_SOURCE_DIR}/src/KubeInterface.cpp
    ${CMAKE_SOURCE_DIR}/src/Process.cpp
    ${CMAKE_SOURCE_DIR}/src/Utilities.cpp
  )
  SET(SLATE_CLIENT_COMPILE_OPTIONS
      -DSLATE_CLIENT
  )

  add_executable(slate ${CLIENT_SOURCES})
  target_compile_options(slate PRIVATE ${SLATE_CLIENT_COMPILE_OPTIONS} )
  target_include_directories (slate
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/resources>
      $<INSTALL_INTERFACE:include>
      ${CURL_INCLUDE_DIRS}
      ${LIBCRYPTO_CFLAGS}
      ${SSL_CFLAGS}
      $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  )
  add_dependencies(slate embed_version)
  
  if(STATIC_CLIENT)
    if(APPLE)
      message("Attempting partially static link")
      set(CLIENT_LIBRARIES
        ${CURL_LIBRARIES}
        z
        pthread
      )
    else(APPLE) # assume any thing which is not Apple is Linux
      # static building may only work on alpine with a statically linked copy of nghttp2 installed
      message("Attempting fully static link")
      set(CLIENT_LIBRARIES
        -no-pie
        -static
        -static-libstdc++
        -static-libgcc
        ${CURL_LIBRARIES}
        nghttp2
        ${SSL_LDFLAGS}
        ${LIBCRYPTO_LDFLAGS}
        z
        pthread
      )
    endif(APPLE)
  else(STATIC_CLIENT)
    set(CLIENT_LIBRARIES
      ${CURL_LIBRARIES}
      ${SSL_LDFLAGS}
      ${LIBCRYPTO_LDFLAGS}
      z
      pthread
    )
  endif(STATIC_CLIENT)
  
  target_link_libraries(slate
    PUBLIC
      ${CLIENT_LIBRARIES}
  )
  target_compile_options(slate PRIVATE -DRAPIDJSON_HAS_STDSTRING -Os -std=c++11)
  install(TARGETS slate RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  
  LIST(APPEND RPM_SOURCES ${CLIENT_SOURCES})

  # TODO: uninstall
endif(BUILD_CLIENT)

LIST(APPEND STRESS_TEST_SOURCES
  ${CMAKE_SOURCE_DIR}/src/StressTest.cpp
  ${CMAKE_SOURCE_DIR}/src/Archive.cpp
  ${CMAKE_SOURCE_DIR}/src/FileHandle.cpp
  ${CMAKE_SOURCE_DIR}/src/FileSystem.cpp
  ${CMAKE_SOURCE_DIR}/src/HTTPRequests.cpp
  ${CMAKE_SOURCE_DIR}/src/KubeInterface.cpp
  ${CMAKE_SOURCE_DIR}/src/Process.cpp
  ${CMAKE_SOURCE_DIR}/src/Utilities.cpp
)
SET(STRESS_TEST_COMPILE_OPTIONS
    -DSLATE_CLIENT
)
add_executable(slate-stress-test ${STRESS_TEST_SOURCES})
target_compile_options(slate-stress-test PRIVATE ${SLATE_CLIENT_COMPILE_OPTIONS} )
target_include_directories (slate-stress-test
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/resources>
    $<INSTALL_INTERFACE:include>
    ${CURL_INCLUDE_DIRS}
    ${LIBCRYPTO_CFLAGS}
    ${SSL_CFLAGS}
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
)
target_link_libraries(slate-stress-test
  PUBLIC
    ${CURL_LIBRARIES}
    ${SSL_LDFLAGS}
    ${LIBCRYPTO_LDFLAGS}
    z
    pthread
)
target_compile_options(slate-stress-test PRIVATE -DRAPIDJSON_HAS_STDSTRING -Os -std=c++11)


# -----------------------------------------------------------------------------
# Main executable
if(BUILD_SERVER)
  LIST(APPEND SERVER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/slate_service.cpp
    ${CMAKE_SOURCE_DIR}/src/DNSManipulator.cpp
    ${CMAKE_SOURCE_DIR}/src/Entities.cpp
    ${CMAKE_SOURCE_DIR}/src/Geocoder.cpp
    ${CMAKE_SOURCE_DIR}/src/HTTPRequests.cpp
    ${CMAKE_SOURCE_DIR}/src/KubeInterface.cpp
    ${CMAKE_SOURCE_DIR}/src/PersistentStore.cpp
    ${CMAKE_SOURCE_DIR}/src/ServerUtilities.cpp
    ${CMAKE_SOURCE_DIR}/src/Utilities.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationInstanceCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/ClusterCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/GroupCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/MonitoringCredentialCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/SecretCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/UserCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/VersionCommands.cpp
  
    ${CMAKE_SOURCE_DIR}/src/Archive.cpp
    ${CMAKE_SOURCE_DIR}/src/FileHandle.cpp
    ${CMAKE_SOURCE_DIR}/src/FileSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/Logging.cpp
    ${CMAKE_SOURCE_DIR}/src/Process.cpp
  
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
  
  SET(SLATE_SERVER_COMPILE_OPTIONS 
    -DSLATE_SERVER
    ${CURL_CFLAGS}
    ${LIBCRYPTO_CFLAGS}
    ${SSL_CFLAGS}
    ${YAMLCPP_CFLAGS}
    -DRAPIDJSON_HAS_STDSTRING
    -O2
  )
  
  IF(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86_64)$")
    LIST(APPEND SERVER_SOURCES
      ${CMAKE_SOURCE_DIR}/src/scrypt/cpusupport/cpusupport_x86_sse2.c
      ${CMAKE_SOURCE_DIR}/src/scrypt/crypto/crypto_scrypt_smix_sse2.c
    )
    SET(SLATE_SERVER_COMPILE_OPTIONS ${SLATE_SERVER_COMPILE_OPTIONS} 
        -msse2 -DCPUSUPPORT_X86_CPUID -DCPUSUPPORT_X86_SSE2
    )
  ENDIF()
  
  add_library(slate-server STATIC ${SERVER_SOURCES})
  target_include_directories(slate-server
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${Boost_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
  )
  target_link_libraries(slate-server
    PUBLIC
    pthread
    aws-cpp-sdk-dynamodb
    aws-cpp-sdk-route53
    aws-cpp-sdk-core
    ${CURL_LIBRARIES}
    ${SSL_LDFLAGS}
    ${LIBCRYPTO_LDFLAGS}
    ${Boost_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${YAMLCPP_LDFLAGS}
    )
  target_compile_options(slate-server PRIVATE ${SLATE_SERVER_COMPILE_OPTIONS} )
  add_dependencies(slate-server embed_version)
  
  add_executable(slate-service ${CMAKE_SOURCE_DIR}/src/slate_service.cpp)
  target_compile_options(slate-service PRIVATE -DRAPIDJSON_HAS_STDSTRING)
  target_link_libraries(slate-service slate-server)
  install(TARGETS slate-service RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  # TODO: uninstall

  # -----------------------------------------------------------------------------
  # Testing
  if(BUILD_SERVER_TESTS)
    configure_file(test/CTestCustom.cmake CTestCustom.cmake)
    enable_testing()
    
    add_executable(slate-test-database-server
      test/DBServer.cpp
      src/Archive.cpp
      src/FileHandle.cpp
      src/FileSystem.cpp
      src/Process.cpp
    )
    target_include_directories (slate-test-database-server
      PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${CURL_INCLUDE_DIRS}
        ${LIBCRYPTO_CFLAGS}
        ${SSL_CFLAGS}
        ${Boost_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
        ${LIBCRYPTO_CFLAGS}
        ${SSL_CFLAGS}
    )
    target_link_libraries(slate-test-database-server
      PUBLIC
        ${CURL_LIBRARIES}
        ${SSL_LDFLAGS}
        ${LIBCRYPTO_LDFLAGS}
        ${Boost_LIBRARIES}
        ${ZLIB_LIBRARIES}
        pthread
    )
    
     add_executable(slate-test-helm-server
      test/HelmServer.cpp
    )
    target_include_directories (slate-test-helm-server
      PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${CURL_INCLUDE_DIRS}
        ${LIBCRYPTO_CFLAGS}
        ${SSL_CFLAGS}
        ${Boost_INCLUDE_DIRS}
        ${CURL_INCLUDE_DIRS}
        ${LIBCRYPTO_CFLAGS}
        ${SSL_CFLAGS}
    )
    target_compile_options(slate-test-helm-server PRIVATE ${YAMLCPP_CFLAGS} )
    target_link_libraries(slate-test-helm-server
      PUBLIC
        ${YAMLCPP_LDFLAGS}
        ${CURL_LIBRARIES}
        ${SSL_LDFLAGS}
        ${LIBCRYPTO_LDFLAGS}
        ${Boost_LIBRARIES}
        ${ZLIB_LIBRARIES}
        pthread
    )
    
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    set (CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    
    list(APPEND BASE_TEST_COMPONENTS
      test/test_main.cpp
    )
    
    add_library(slate-testing STATIC
      ${BASE_TEST_COMPONENTS}
    )
    target_include_directories(slate-testing
      PRIVATE
      ${CURL_INCLUDE_DIRS}
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
    )
    target_compile_options(slate-testing PRIVATE -g -DRAPIDJSON_HAS_STDSTRING ${LIBCRYPTO_CFLAGS} ${SSL_CFLAGS})
    target_link_libraries(slate-testing
      PUBLIC
      ${CURL_LIBRARIES}
      boost_system
      pthread
      ssl
      crypto
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
        ${${TEST_NAME}_ARGS_SOURCE_FILES}
        )
      target_compile_options(${TEST_NAME} PRIVATE -g -DRAPIDJSON_HAS_STDSTRING)
      target_link_libraries(${TEST_NAME}
        PUBLIC
        ${${TEST_NAME}_ARGS_LINK_LIBRARIES}
        slate-testing
        slate-server
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
    
    slate_add_test(test-user-group-addition
        SOURCE_FILES test/TestUserGroupAddition.cpp)
    
    slate_add_test(test-user-group-removal
        SOURCE_FILES test/TestUserGroupRemoval.cpp)
    
    slate_add_test(test-user-group-listing
        SOURCE_FILES test/TestUserGroupListing.cpp)
    
    slate_add_test(test-user-token-replacement
        SOURCE_FILES test/TestUserTokenReplacement.cpp)
    
    slate_add_test(test-find-user
        SOURCE_FILES test/TestFindUser.cpp)
    
    slate_add_test(test-group-listing
        SOURCE_FILES test/TestGroupListing.cpp)
    
    slate_add_test(test-group-info
        SOURCE_FILES test/TestGroupInfo.cpp)
    
    slate_add_test(test-group-update
        SOURCE_FILES test/TestGroupUpdate.cpp)
    
    slate_add_test(test-group-creation
        SOURCE_FILES test/TestGroupCreation.cpp)
    
    slate_add_test(test-group-deletion
        SOURCE_FILES test/TestGroupDeletion.cpp)
    
    slate_add_test(test-group-member-listing
        SOURCE_FILES test/TestGroupMemberListing.cpp)
    
    slate_add_test(test-group-cluster-listing
        SOURCE_FILES test/TestGroupClusterListing.cpp)
    
    slate_add_test(test-cluster-creation
        SOURCE_FILES test/TestClusterCreation.cpp)
    
    slate_add_test(test-cluster-listing
        SOURCE_FILES test/TestClusterListing.cpp)
    
    slate_add_test(test-cluster-info
        SOURCE_FILES test/TestClusterInfo.cpp)
    
    slate_add_test(test-cluster-deletion
        SOURCE_FILES test/TestClusterDeletion.cpp)
    
    slate_add_test(test-cluster-update
        SOURCE_FILES test/TestClusterUpdate.cpp)
    
    slate_add_test(test-cluster-allowed-group-listing
        SOURCE_FILES test/TestClusterAllowedGroupListing.cpp)
    
    slate_add_test(test-cluster-allowed-group-checking
        SOURCE_FILES test/TestClusterAllowedGroupChecking.cpp)
    
    slate_add_test(test-cluster-allowed-group-addition
        SOURCE_FILES test/TestClusterAllowedGroupAddition.cpp)
    
    slate_add_test(test-cluster-allowed-group-deletion
        SOURCE_FILES test/TestClusterAllowedGroupDeletion.cpp)
    
    slate_add_test(test-cluster-allowed-application-addition
        SOURCE_FILES test/TestClusterAllowedApplicationAddition.cpp)
    
    slate_add_test(test-cluster-allowed-application-deletion
        SOURCE_FILES test/TestClusterAllowedApplicationDeletion.cpp)
    
    slate_add_test(test-cluster-allowed-application-listing
        SOURCE_FILES test/TestClusterAllowedApplicationListing.cpp)
    
    slate_add_test(test-app-listing
        SOURCE_FILES test/TestApplicationListing.cpp)
    
    slate_add_test(test-fetch-app-config
        SOURCE_FILES test/TestFetchApplicationConfig.cpp)
    
    slate_add_test(test-app-install
        SOURCE_FILES test/TestApplicationInstall.cpp
        LINK_LIBRARIES boost_date_time)
    
    slate_add_test(test-ad-hoc-app-install
        SOURCE_FILES test/TestAdHocApplicationInstall.cpp
        LINK_LIBRARIES boost_date_time)
    
    slate_add_test(test-instance-listing
        SOURCE_FILES test/TestInstanceListing.cpp)
    
    slate_add_test(test-instance-info-fetching
        SOURCE_FILES test/TestInstanceInfoFetching.cpp)
    
    slate_add_test(test-instance-restarting
        SOURCE_FILES test/TestInstanceRestarting.cpp)
    
    slate_add_test(test-instance-deletion
        SOURCE_FILES test/TestInstanceDeletion.cpp)

    slate_add_test(test-instance-scaling
        SOURCE_FILES test/TestInstanceScale.cpp)

    slate_add_test(test-instance-duplicates
        SOURCE_FILES test/TestInstanceDuplicates.cpp)
    
    slate_add_test(test-secret-listing
        SOURCE_FILES test/TestSecretListing.cpp)
    
    slate_add_test(test-secret-creation
        SOURCE_FILES test/TestSecretCreation.cpp
        LINK_LIBRARIES boost_date_time)
    
    slate_add_test(test-secret-deletion
        SOURCE_FILES test/TestSecretDeletion.cpp)
    
    slate_add_test(test-secret-fetching
        SOURCE_FILES test/TestSecretFetching.cpp)
    
    slate_add_test(test-monitoring-credential-allocation
        SOURCE_FILES test/TestMonitoringCredentialAllocation.cpp)
      
    foreach(TEST ${ALL_TESTS})
      get_filename_component(TEST_NAME ${TEST} NAME_WE)
      add_test(${TEST_NAME} ${TEST})
    endforeach()
    
    set_tests_properties(test-ad-hoc-app-install PROPERTIES 
        ENVIRONMENT TEST_SRC=${CMAKE_CURRENT_SOURCE_DIR}/test
        )
    
    add_custom_target(check 
      COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
      DEPENDS ${ALL_TESTS} slate-test-database-server slate-service)
  endif(BUILD_SERVER_TESTS)
  
  LIST(APPEND RPM_SOURCES ${SERVER_SOURCES})

endif(BUILD_SERVER)

add_custom_target(rpm-sources 
  # FOO=BAR is a sacrificial dummy variable to absorb the extra 
  # quotes that cmake erroneously puts on the first variable
  COMMAND FOO=BAR 
    CMAKE_BINARY_DIR=${CMAKE_BINARY_DIR} 
    CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR} 
    ${CMAKE_SOURCE_DIR}/resources/build_rpm.sh
  DEPENDS ${RPM_SOURCES})

if(BUILD_CLIENT)
  add_custom_target(client-rpm
    COMMAND rpmbuild --define "_topdir ${CMAKE_BINARY_DIR}" --define "version `${CMAKE_SOURCE_DIR}/cmake/extract_version.sh" "${CMAKE_SOURCE_DIR}`" -ba "${CMAKE_SOURCE_DIR}/resources/rpm_specs/slate-client.spec"
    DEPENDS rpm-sources)
  LIST(APPEND RPMS client-rpm)
endif(BUILD_CLIENT)
if(BUILD_SERVER)
  add_custom_target(server-rpm
    COMMAND rpmbuild --define "_topdir ${CMAKE_BINARY_DIR}" --define "version `${CMAKE_SOURCE_DIR}/cmake/extract_version.sh" "${CMAKE_SOURCE_DIR}`" -ba "${CMAKE_SOURCE_DIR}/resources/rpm_specs/slate-api-server.spec"
    DEPENDS rpm-sources)
  LIST(APPEND RPMS server-rpm)
endif(BUILD_SERVER)

add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/SOURCES/aws-sdk-cpp-${AWS_SDK_VERSION}.tar.gz"
  COMMAND mkdir -p ${CMAKE_BINARY_DIR}/SOURCES
  COMMAND curl -L "https://github.com/aws/aws-sdk-cpp/archive/${AWS_SDK_VERSION}.tar.gz" -o "${CMAKE_BINARY_DIR}/SOURCES/aws-sdk-cpp-${AWS_SDK_VERSION}.tar.gz"
)
add_custom_target(aws-rpms
  COMMAND rpmbuild --define "_topdir ${CMAKE_BINARY_DIR}" --define "version ${AWS_SDK_VERSION}" -ba "${CMAKE_SOURCE_DIR}/resources/rpm_specs/aws-sdk-cpp.spec"
  DEPENDS "${CMAKE_BINARY_DIR}/SOURCES/aws-sdk-cpp-${AWS_SDK_VERSION}.tar.gz"
)

add_custom_target(rpm
  DEPENDS ${RPMS})
