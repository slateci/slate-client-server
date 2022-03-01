Name: aws-sdk-cpp
Version: 1.7.345
Release: 1%{?dist}
Summary: AWS C++ SDK
License: MIT
URL: https://github.com/aws/aws-sdk-cpp

# curl -L https://github.com/aws/aws-sdk-cpp/archive/1.5.25.tar.gz -o aws-sdk-cpp-1.5.25.tar.gz
Source: %{name}-%{version}.tar.gz

BuildRequires: gcc-c++ cmake3 openssl-devel zlib zlib-devel openssl curl curl-devel git

%description
Amazon AWS SDK for C++

#TODO further split this out into core- and other AWS tools

%package core-devel
Summary: headers for AWS C++ SDK Core
Group: Development/Libraries
%description core-devel
%{summary}.

%package core-libs
Summary: AWS C++ SDK runtime libraries Core
Group: System Environment/Libraries
%description core-libs
%{summary}.

%package dynamodb-devel
Summary: headers for AWS C++ SDK for DynamoDB
Group: Development/Libraries
Requires: aws-sdk-cpp-core-devel
%description dynamodb-devel
%{summary}.

%package dynamodb-libs
Summary: AWS C++ SDK runtime libraries for DynamoDB
Group: System Environment/Libraries
Requires: aws-sdk-cpp-core-libs
%description dynamodb-libs
%{summary}.

%package route53-devel
Summary: headers for AWS C++ SDK for Route53
Group: Development/Libraries
Requires: aws-sdk-cpp-core-devel
%description route53-devel
%{summary}.

%package route53-libs
Summary: AWS C++ SDK runtime libraries for Route53
Group: System Environment/Libraries
Requires: aws-sdk-cpp-core-libs
%description route53-libs
%{summary}.

%prep
%setup -c -n %{name}-%{version}.tar.gz 

%build
cd %{name}-%{version}
mkdir build
cd build
cmake3 .. -DBUILD_ONLY="dynamodb;route53" -DBUILD_SHARED_LIBS=Off -Wno-error
make

%install
cd %{name}-%{version}
cd build
rm -rf $RPM_BUILD_ROOT
echo $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/usr/{include,lib64}
mv $RPM_BUILD_ROOT/usr/local/include/aws $RPM_BUILD_ROOT/usr/include/aws
mv $RPM_BUILD_ROOT/usr/local/lib64/cmake $RPM_BUILD_ROOT/usr/lib64/cmake
mv $RPM_BUILD_ROOT/usr/local/lib64/*.a $RPM_BUILD_ROOT/usr/lib64/
mv $RPM_BUILD_ROOT/usr/local/lib64/aws-* $RPM_BUILD_ROOT/usr/lib64/
cd ..

%clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%files core-devel
%defattr(-,root,root,-)
%{_includedir}/aws/core/AmazonSerializableWebServiceRequest.h
%{_includedir}/aws/core/AmazonStreamingWebServiceRequest.h
%{_includedir}/aws/core/AmazonWebServiceRequest.h
%{_includedir}/aws/core/AmazonWebServiceResult.h
%{_includedir}/aws/core/Aws.h
%{_includedir}/aws/core/Core_EXPORTS.h
%{_includedir}/aws/core/Globals.h
%{_includedir}/aws/core/NoResult.h
%{_includedir}/aws/core/Region.h
%{_includedir}/aws/core/SDKConfig.h
%{_includedir}/aws/core/Version.h
%{_includedir}/aws/core/VersionConfig.h
%{_includedir}/aws/core/auth/AWSAuthSigner.h
%{_includedir}/aws/core/auth/AWSAuthSignerProvider.h
%{_includedir}/aws/core/auth/AWSCredentialsProvider.h
%{_includedir}/aws/core/auth/AWSCredentialsProviderChain.h
%{_includedir}/aws/core/client/AWSClient.h
%{_includedir}/aws/core/client/AWSError.h
%{_includedir}/aws/core/client/AWSErrorMarshaller.h
%{_includedir}/aws/core/client/AsyncCallerContext.h
%{_includedir}/aws/core/client/ClientConfiguration.h
%{_includedir}/aws/core/client/CoreErrors.h
%{_includedir}/aws/core/client/DefaultRetryStrategy.h
%{_includedir}/aws/core/client/RetryStrategy.h
%{_includedir}/aws/core/config/AWSProfileConfigLoader.h
%{_includedir}/aws/core/external/cjson/cJSON.h
%{_includedir}/aws/core/external/tinyxml2/tinyxml2.h
%{_includedir}/aws/core/http/HttpClient.h
%{_includedir}/aws/core/http/HttpClientFactory.h
%{_includedir}/aws/core/http/HttpRequest.h
%{_includedir}/aws/core/http/HttpResponse.h
%{_includedir}/aws/core/http/HttpTypes.h
%{_includedir}/aws/core/http/Scheme.h
%{_includedir}/aws/core/http/URI.h
%{_includedir}/aws/core/http/curl/CurlHandleContainer.h
%{_includedir}/aws/core/http/curl/CurlHttpClient.h
%{_includedir}/aws/core/http/standard/StandardHttpRequest.h
%{_includedir}/aws/core/http/standard/StandardHttpResponse.h
%{_includedir}/aws/core/internal/AWSHttpResourceClient.h
%{_includedir}/aws/core/platform/Android.h
%{_includedir}/aws/core/platform/Environment.h
%{_includedir}/aws/core/platform/FileSystem.h
%{_includedir}/aws/core/platform/OSVersionInfo.h
%{_includedir}/aws/core/platform/Platform.h
%{_includedir}/aws/core/platform/Security.h
%{_includedir}/aws/core/platform/Time.h
%{_includedir}/aws/core/utils/Array.h
%{_includedir}/aws/core/utils/DNS.h
%{_includedir}/aws/core/utils/DateTime.h
%{_includedir}/aws/core/utils/EnumParseOverflowContainer.h
%{_includedir}/aws/core/utils/FileSystemUtils.h
%{_includedir}/aws/core/utils/GetTheLights.h
%{_includedir}/aws/core/utils/HashingUtils.h
%{_includedir}/aws/core/utils/Outcome.h
%{_includedir}/aws/core/utils/ResourceManager.h
%{_includedir}/aws/core/utils/StringUtils.h
%{_includedir}/aws/core/utils/UUID.h
%{_includedir}/aws/core/utils/UnreferencedParam.h
%{_includedir}/aws/core/utils/base64/Base64.h
%{_includedir}/aws/core/utils/crypto/Cipher.h
%{_includedir}/aws/core/utils/crypto/ContentCryptoMaterial.h
%{_includedir}/aws/core/utils/crypto/ContentCryptoScheme.h
%{_includedir}/aws/core/utils/crypto/CryptoBuf.h
%{_includedir}/aws/core/utils/crypto/CryptoStream.h
%{_includedir}/aws/core/utils/crypto/EncryptionMaterials.h
%{_includedir}/aws/core/utils/crypto/Factories.h
%{_includedir}/aws/core/utils/crypto/HMAC.h
%{_includedir}/aws/core/utils/crypto/Hash.h
%{_includedir}/aws/core/utils/crypto/HashResult.h
%{_includedir}/aws/core/utils/crypto/KeyWrapAlgorithm.h
%{_includedir}/aws/core/utils/crypto/MD5.h
%{_includedir}/aws/core/utils/crypto/SecureRandom.h
%{_includedir}/aws/core/utils/crypto/Sha256.h
%{_includedir}/aws/core/utils/crypto/Sha256HMAC.h
%{_includedir}/aws/core/utils/crypto/openssl/CryptoImpl.h
%{_includedir}/aws/core/utils/json/JsonSerializer.h
%{_includedir}/aws/core/utils/logging/AWSLogging.h
%{_includedir}/aws/core/utils/logging/ConsoleLogSystem.h
%{_includedir}/aws/core/utils/logging/DefaultLogSystem.h
%{_includedir}/aws/core/utils/logging/FormattedLogSystem.h
%{_includedir}/aws/core/utils/logging/LogLevel.h
%{_includedir}/aws/core/utils/logging/LogMacros.h
%{_includedir}/aws/core/utils/logging/LogSystemInterface.h
%{_includedir}/aws/core/utils/logging/NullLogSystem.h
%{_includedir}/aws/core/utils/memory/AWSMemory.h
%{_includedir}/aws/core/utils/memory/MemorySystemInterface.h
%{_includedir}/aws/core/utils/memory/stl/AWSAllocator.h
%{_includedir}/aws/core/utils/memory/stl/AWSDeque.h
%{_includedir}/aws/core/utils/memory/stl/AWSList.h
%{_includedir}/aws/core/utils/memory/stl/AWSMap.h
%{_includedir}/aws/core/utils/memory/stl/AWSMultiMap.h
%{_includedir}/aws/core/utils/memory/stl/AWSQueue.h
%{_includedir}/aws/core/utils/memory/stl/AWSSet.h
%{_includedir}/aws/core/utils/memory/stl/AWSStack.h
%{_includedir}/aws/core/utils/memory/stl/AWSStreamFwd.h
%{_includedir}/aws/core/utils/memory/stl/AWSString.h
%{_includedir}/aws/core/utils/memory/stl/AWSStringStream.h
%{_includedir}/aws/core/utils/memory/stl/AWSVector.h
%{_includedir}/aws/core/utils/memory/stl/SimpleStringStream.h
%{_includedir}/aws/core/utils/ratelimiter/DefaultRateLimiter.h
%{_includedir}/aws/core/utils/ratelimiter/RateLimiterInterface.h
%{_includedir}/aws/core/utils/stream/PreallocatedStreamBuf.h
%{_includedir}/aws/core/utils/stream/ResponseStream.h
%{_includedir}/aws/core/utils/stream/SimpleStreamBuf.h
%{_includedir}/aws/core/utils/threading/Executor.h
%{_includedir}/aws/core/utils/threading/ReaderWriterLock.h
%{_includedir}/aws/core/utils/threading/Semaphore.h
%{_includedir}/aws/core/utils/threading/ThreadTask.h
%{_includedir}/aws/core/utils/xml/XmlSerializer.h
%{_includedir}/aws/core/monitoring/CoreMetrics.h
%{_includedir}/aws/core/monitoring/DefaultMonitoring.h
%{_includedir}/aws/core/monitoring/HttpClientMetrics.h
%{_includedir}/aws/core/monitoring/MonitoringFactory.h
%{_includedir}/aws/core/monitoring/MonitoringInterface.h
%{_includedir}/aws/core/monitoring/MonitoringManager.h
%{_includedir}/aws/core/net/Net.h
%{_includedir}/aws/core/net/SimpleUDP.h
%{_includedir}/aws/core/utils/Cache.h
%{_includedir}/aws/core/utils/ConcurrentCache.h
%{_includedir}/aws/core/utils/event/EventHeader.h
%{_includedir}/aws/core/utils/event/EventMessage.h
%{_includedir}/aws/core/utils/event/EventStream.h
%{_includedir}/aws/core/utils/event/EventStreamBuf.h
%{_includedir}/aws/core/utils/event/EventStreamDecoder.h
%{_includedir}/aws/core/utils/event/EventStreamErrors.h
%{_includedir}/aws/core/utils/event/EventStreamHandler.h
%{_includedir}/aws/external/gtest.h
%{_includedir}/aws/testing/MemoryTesting.h
%{_includedir}/aws/testing/ProxyConfig.h
%{_includedir}/aws/testing/TestingEnvironment.h
%{_includedir}/aws/testing/Testing_EXPORTS.h
%{_includedir}/aws/testing/mocks/aws/auth/MockAWSHttpResourceClient.h
%{_includedir}/aws/testing/mocks/aws/client/MockAWSClient.h
%{_includedir}/aws/testing/mocks/event/MockEventStreamDecoder.h
%{_includedir}/aws/testing/mocks/event/MockEventStreamHandler.h
%{_includedir}/aws/testing/mocks/http/MockHttpClient.h
%{_includedir}/aws/testing/platform/PlatformTesting.h
%{_includedir}/aws/checksums/crc.h
%{_includedir}/aws/checksums/crc_jni.h
%{_includedir}/aws/checksums/exports.h
%{_includedir}/aws/common/allocator.h
%{_includedir}/aws/common/array_list.h
%{_includedir}/aws/common/array_list.inl
%{_includedir}/aws/common/assert.h
%{_includedir}/aws/common/atomics.h
%{_includedir}/aws/common/atomics.inl
%{_includedir}/aws/common/atomics_fallback.inl
%{_includedir}/aws/common/atomics_gnu.inl
%{_includedir}/aws/common/atomics_gnu_old.inl
%{_includedir}/aws/common/atomics_msvc.inl
%{_includedir}/aws/common/bigint.h
%{_includedir}/aws/common/byte_buf.h
%{_includedir}/aws/common/byte_order.h
%{_includedir}/aws/common/byte_order.inl
%{_includedir}/aws/common/cache.h
%{_includedir}/aws/common/clock.h
%{_includedir}/aws/common/clock.inl
%{_includedir}/aws/common/command_line_parser.h
%{_includedir}/aws/common/common.h
%{_includedir}/aws/common/condition_variable.h
%{_includedir}/aws/common/config.h
%{_includedir}/aws/common/date_time.h
%{_includedir}/aws/common/device_random.h
%{_includedir}/aws/common/encoding.h
%{_includedir}/aws/common/encoding.inl
%{_includedir}/aws/common/environment.h
%{_includedir}/aws/common/error.h
%{_includedir}/aws/common/error.inl
%{_includedir}/aws/common/exports.h
%{_includedir}/aws/common/fifo_cache.h
%{_includedir}/aws/common/hash_table.h
%{_includedir}/aws/common/lifo_cache.h
%{_includedir}/aws/common/linked_hash_table.h
%{_includedir}/aws/common/linked_list.h
%{_includedir}/aws/common/linked_list.inl
%{_includedir}/aws/common/log_channel.h
%{_includedir}/aws/common/log_formatter.h
%{_includedir}/aws/common/log_writer.h
%{_includedir}/aws/common/logging.h
%{_includedir}/aws/common/lru_cache.h
%{_includedir}/aws/common/macros.h
%{_includedir}/aws/common/math.cbmc.inl
%{_includedir}/aws/common/math.fallback.inl
%{_includedir}/aws/common/math.gcc_overflow.inl
%{_includedir}/aws/common/math.gcc_x64_asm.inl
%{_includedir}/aws/common/math.h
%{_includedir}/aws/common/math.inl
%{_includedir}/aws/common/math.msvc.inl
%{_includedir}/aws/common/mutex.h
%{_includedir}/aws/common/package.h
%{_includedir}/aws/common/posix/common.inl
%{_includedir}/aws/common/predicates.h
%{_includedir}/aws/common/priority_queue.h
%{_includedir}/aws/common/process.h
%{_includedir}/aws/common/resource_name.h
%{_includedir}/aws/common/ring_buffer.h
%{_includedir}/aws/common/ring_buffer.inl
%{_includedir}/aws/common/rw_lock.h
%{_includedir}/aws/common/statistics.h
%{_includedir}/aws/common/stdbool.h
%{_includedir}/aws/common/stdint.h
%{_includedir}/aws/common/string.h
%{_includedir}/aws/common/string.inl
%{_includedir}/aws/common/system_info.h
%{_includedir}/aws/common/task_scheduler.h
%{_includedir}/aws/common/thread.h
%{_includedir}/aws/common/time.h
%{_includedir}/aws/common/uuid.h
%{_includedir}/aws/common/zero.h
%{_includedir}/aws/common/zero.inl
%{_includedir}/aws/core/auth/AWSCredentials.h
%{_includedir}/aws/core/auth/STSCredentialsProvider.h
%{_includedir}/aws/core/client/SpecifiedRetryableErrorsRetryStrategy.h
%{_includedir}/aws/core/utils/ARN.h
%{_includedir}/aws/core/utils/event/EventDecoderStream.h
%{_includedir}/aws/core/utils/event/EventEncoderStream.h
%{_includedir}/aws/core/utils/event/EventStreamEncoder.h
%{_includedir}/aws/core/utils/stream/ConcurrentStreamBuf.h
%{_includedir}/aws/event-stream/event_stream.h
%{_includedir}/aws/event-stream/event_stream_exports.h
%{_includedir}/aws/testing/aws_test_allocators.h
%{_includedir}/aws/testing/aws_test_harness.h


%files core-libs
%{_libdir}/cmake/AWSSDK/AWSSDKConfig.cmake
%{_libdir}/cmake/AWSSDK/AWSSDKConfigVersion.cmake
%{_libdir}/cmake/AWSSDK/build_external.cmake
%{_libdir}/cmake/AWSSDK/compiler_settings.cmake
%{_libdir}/cmake/AWSSDK/dependencies.cmake
%{_libdir}/cmake/AWSSDK/external_dependencies.cmake
%{_libdir}/cmake/AWSSDK/initialize_project_version.cmake
%{_libdir}/cmake/AWSSDK/make_uninstall.cmake
%{_libdir}/cmake/AWSSDK/platform/android.cmake
%{_libdir}/cmake/AWSSDK/platform/apple.cmake
%{_libdir}/cmake/AWSSDK/platform/custom.cmake
%{_libdir}/cmake/AWSSDK/platform/linux.cmake
%{_libdir}/cmake/AWSSDK/platform/unix.cmake
%{_libdir}/cmake/AWSSDK/platform/windows.cmake
%{_libdir}/cmake/AWSSDK/platformDeps.cmake
%{_libdir}/cmake/AWSSDK/resolve_platform.cmake
%{_libdir}/cmake/AWSSDK/sdks.cmake
%{_libdir}/cmake/AWSSDK/sdksCommon.cmake
%{_libdir}/cmake/AWSSDK/setup_cmake_find_module.cmake
%{_libdir}/cmake/AWSSDK/utilities.cmake
%{_libdir}/cmake/aws-cpp-sdk-core/aws-cpp-sdk-core-config-version.cmake
%{_libdir}/cmake/aws-cpp-sdk-core/aws-cpp-sdk-core-config.cmake
%{_libdir}/cmake/aws-cpp-sdk-core/aws-cpp-sdk-core-targets.cmake
%{_libdir}/cmake/testing-resources/testing-resources-config-version.cmake
%{_libdir}/cmake/testing-resources/testing-resources-config.cmake
%{_libdir}/cmake/testing-resources/testing-resources-targets.cmake
%{_libdir}/cmake/AwsCFlags.cmake
%{_libdir}/cmake/AwsCheckHeaders.cmake
%{_libdir}/cmake/AwsFindPackage.cmake
%{_libdir}/cmake/AwsLibFuzzer.cmake
%{_libdir}/cmake/AwsSIMD.cmake
%{_libdir}/cmake/AwsSanitizers.cmake
%{_libdir}/cmake/AwsSharedLibSetup.cmake
%{_libdir}/cmake/AwsTestHarness.cmake
%{_libdir}/cmake/aws-cpp-sdk-core/aws-cpp-sdk-core-targets-release.cmake
%{_libdir}/cmake/testing-resources/testing-resources-targets-release.cmake
%{_libdir}/libaws-c-common.a
%{_libdir}/libaws-c-event-stream.a
%{_libdir}/libaws-checksums.a
%{_libdir}/libaws-cpp-sdk-core.a
%{_libdir}/libtesting-resources.a
%{_libdir}/aws-c-common/cmake/aws-c-common-config.cmake
%{_libdir}/aws-c-common/cmake/static/aws-c-common-targets-release.cmake
%{_libdir}/aws-c-common/cmake/static/aws-c-common-targets.cmake
%{_libdir}/aws-c-event-stream/cmake/aws-c-event-stream-config.cmake
%{_libdir}/aws-c-event-stream/cmake/static/aws-c-event-stream-targets-release.cmake
%{_libdir}/aws-c-event-stream/cmake/static/aws-c-event-stream-targets.cmake
%{_libdir}/aws-checksums/cmake/aws-checksums-config.cmake
%{_libdir}/aws-checksums/cmake/static/aws-checksums-targets-release.cmake
%{_libdir}/aws-checksums/cmake/static/aws-checksums-targets.cmake



%files dynamodb-devel
%defattr(-,root,root,-)
%{_includedir}/aws/dynamodb/DynamoDBClient.h
%{_includedir}/aws/dynamodb/DynamoDBEndpoint.h
%{_includedir}/aws/dynamodb/DynamoDBErrorMarshaller.h
%{_includedir}/aws/dynamodb/DynamoDBErrors.h
%{_includedir}/aws/dynamodb/DynamoDBRequest.h
%{_includedir}/aws/dynamodb/DynamoDB_EXPORTS.h
%{_includedir}/aws/dynamodb/model/AttributeAction.h
%{_includedir}/aws/dynamodb/model/AttributeDefinition.h
%{_includedir}/aws/dynamodb/model/AttributeValue.h
%{_includedir}/aws/dynamodb/model/AttributeValueUpdate.h
%{_includedir}/aws/dynamodb/model/AttributeValueValue.h
%{_includedir}/aws/dynamodb/model/AutoScalingPolicyDescription.h
%{_includedir}/aws/dynamodb/model/AutoScalingPolicyUpdate.h
%{_includedir}/aws/dynamodb/model/AutoScalingSettingsDescription.h
%{_includedir}/aws/dynamodb/model/AutoScalingSettingsUpdate.h
%{_includedir}/aws/dynamodb/model/AutoScalingTargetTrackingScalingPolicyConfigurationDescription.h
%{_includedir}/aws/dynamodb/model/AutoScalingTargetTrackingScalingPolicyConfigurationUpdate.h
%{_includedir}/aws/dynamodb/model/BackupDescription.h
%{_includedir}/aws/dynamodb/model/BackupDetails.h
%{_includedir}/aws/dynamodb/model/BackupStatus.h
%{_includedir}/aws/dynamodb/model/BackupSummary.h
%{_includedir}/aws/dynamodb/model/BackupType.h
%{_includedir}/aws/dynamodb/model/BackupTypeFilter.h
%{_includedir}/aws/dynamodb/model/BatchGetItemRequest.h
%{_includedir}/aws/dynamodb/model/BatchGetItemResult.h
%{_includedir}/aws/dynamodb/model/BatchWriteItemRequest.h
%{_includedir}/aws/dynamodb/model/BatchWriteItemResult.h
%{_includedir}/aws/dynamodb/model/Capacity.h
%{_includedir}/aws/dynamodb/model/ComparisonOperator.h
%{_includedir}/aws/dynamodb/model/Condition.h
%{_includedir}/aws/dynamodb/model/ConditionalOperator.h
%{_includedir}/aws/dynamodb/model/ConsumedCapacity.h
%{_includedir}/aws/dynamodb/model/ContinuousBackupsDescription.h
%{_includedir}/aws/dynamodb/model/ContinuousBackupsStatus.h
%{_includedir}/aws/dynamodb/model/CreateBackupRequest.h
%{_includedir}/aws/dynamodb/model/CreateBackupResult.h
%{_includedir}/aws/dynamodb/model/CreateGlobalSecondaryIndexAction.h
%{_includedir}/aws/dynamodb/model/CreateGlobalTableRequest.h
%{_includedir}/aws/dynamodb/model/CreateGlobalTableResult.h
%{_includedir}/aws/dynamodb/model/CreateReplicaAction.h
%{_includedir}/aws/dynamodb/model/CreateTableRequest.h
%{_includedir}/aws/dynamodb/model/CreateTableResult.h
%{_includedir}/aws/dynamodb/model/DeleteBackupRequest.h
%{_includedir}/aws/dynamodb/model/DeleteBackupResult.h
%{_includedir}/aws/dynamodb/model/DeleteGlobalSecondaryIndexAction.h
%{_includedir}/aws/dynamodb/model/DeleteItemRequest.h
%{_includedir}/aws/dynamodb/model/DeleteItemResult.h
%{_includedir}/aws/dynamodb/model/DeleteReplicaAction.h
%{_includedir}/aws/dynamodb/model/DeleteRequest.h
%{_includedir}/aws/dynamodb/model/DeleteTableRequest.h
%{_includedir}/aws/dynamodb/model/DeleteTableResult.h
%{_includedir}/aws/dynamodb/model/DescribeBackupRequest.h
%{_includedir}/aws/dynamodb/model/DescribeBackupResult.h
%{_includedir}/aws/dynamodb/model/DescribeContinuousBackupsRequest.h
%{_includedir}/aws/dynamodb/model/DescribeContinuousBackupsResult.h
%{_includedir}/aws/dynamodb/model/DescribeGlobalTableRequest.h
%{_includedir}/aws/dynamodb/model/DescribeGlobalTableResult.h
%{_includedir}/aws/dynamodb/model/DescribeGlobalTableSettingsRequest.h
%{_includedir}/aws/dynamodb/model/DescribeGlobalTableSettingsResult.h
%{_includedir}/aws/dynamodb/model/DescribeLimitsRequest.h
%{_includedir}/aws/dynamodb/model/DescribeLimitsResult.h
%{_includedir}/aws/dynamodb/model/DescribeTableRequest.h
%{_includedir}/aws/dynamodb/model/DescribeTableResult.h
%{_includedir}/aws/dynamodb/model/DescribeTimeToLiveRequest.h
%{_includedir}/aws/dynamodb/model/DescribeTimeToLiveResult.h
%{_includedir}/aws/dynamodb/model/ExpectedAttributeValue.h
%{_includedir}/aws/dynamodb/model/GetItemRequest.h
%{_includedir}/aws/dynamodb/model/GetItemResult.h
%{_includedir}/aws/dynamodb/model/GlobalSecondaryIndex.h
%{_includedir}/aws/dynamodb/model/GlobalSecondaryIndexDescription.h
%{_includedir}/aws/dynamodb/model/GlobalSecondaryIndexInfo.h
%{_includedir}/aws/dynamodb/model/GlobalSecondaryIndexUpdate.h
%{_includedir}/aws/dynamodb/model/GlobalTable.h
%{_includedir}/aws/dynamodb/model/GlobalTableDescription.h
%{_includedir}/aws/dynamodb/model/GlobalTableGlobalSecondaryIndexSettingsUpdate.h
%{_includedir}/aws/dynamodb/model/GlobalTableStatus.h
%{_includedir}/aws/dynamodb/model/IndexStatus.h
%{_includedir}/aws/dynamodb/model/ItemCollectionMetrics.h
%{_includedir}/aws/dynamodb/model/KeySchemaElement.h
%{_includedir}/aws/dynamodb/model/KeyType.h
%{_includedir}/aws/dynamodb/model/KeysAndAttributes.h
%{_includedir}/aws/dynamodb/model/ListBackupsRequest.h
%{_includedir}/aws/dynamodb/model/ListBackupsResult.h
%{_includedir}/aws/dynamodb/model/ListGlobalTablesRequest.h
%{_includedir}/aws/dynamodb/model/ListGlobalTablesResult.h
%{_includedir}/aws/dynamodb/model/ListTablesRequest.h
%{_includedir}/aws/dynamodb/model/ListTablesResult.h
%{_includedir}/aws/dynamodb/model/ListTagsOfResourceRequest.h
%{_includedir}/aws/dynamodb/model/ListTagsOfResourceResult.h
%{_includedir}/aws/dynamodb/model/LocalSecondaryIndex.h
%{_includedir}/aws/dynamodb/model/LocalSecondaryIndexDescription.h
%{_includedir}/aws/dynamodb/model/LocalSecondaryIndexInfo.h
%{_includedir}/aws/dynamodb/model/PointInTimeRecoveryDescription.h
%{_includedir}/aws/dynamodb/model/PointInTimeRecoverySpecification.h
%{_includedir}/aws/dynamodb/model/PointInTimeRecoveryStatus.h
%{_includedir}/aws/dynamodb/model/Projection.h
%{_includedir}/aws/dynamodb/model/ProjectionType.h
%{_includedir}/aws/dynamodb/model/ProvisionedThroughput.h
%{_includedir}/aws/dynamodb/model/ProvisionedThroughputDescription.h
%{_includedir}/aws/dynamodb/model/PutItemRequest.h
%{_includedir}/aws/dynamodb/model/PutItemResult.h
%{_includedir}/aws/dynamodb/model/PutRequest.h
%{_includedir}/aws/dynamodb/model/QueryRequest.h
%{_includedir}/aws/dynamodb/model/QueryResult.h
%{_includedir}/aws/dynamodb/model/Replica.h
%{_includedir}/aws/dynamodb/model/ReplicaDescription.h
%{_includedir}/aws/dynamodb/model/ReplicaGlobalSecondaryIndexSettingsDescription.h
%{_includedir}/aws/dynamodb/model/ReplicaGlobalSecondaryIndexSettingsUpdate.h
%{_includedir}/aws/dynamodb/model/ReplicaSettingsDescription.h
%{_includedir}/aws/dynamodb/model/ReplicaSettingsUpdate.h
%{_includedir}/aws/dynamodb/model/ReplicaStatus.h
%{_includedir}/aws/dynamodb/model/ReplicaUpdate.h
%{_includedir}/aws/dynamodb/model/RestoreSummary.h
%{_includedir}/aws/dynamodb/model/RestoreTableFromBackupRequest.h
%{_includedir}/aws/dynamodb/model/RestoreTableFromBackupResult.h
%{_includedir}/aws/dynamodb/model/RestoreTableToPointInTimeRequest.h
%{_includedir}/aws/dynamodb/model/RestoreTableToPointInTimeResult.h
%{_includedir}/aws/dynamodb/model/ReturnConsumedCapacity.h
%{_includedir}/aws/dynamodb/model/ReturnItemCollectionMetrics.h
%{_includedir}/aws/dynamodb/model/ReturnValue.h
%{_includedir}/aws/dynamodb/model/SSEDescription.h
%{_includedir}/aws/dynamodb/model/SSESpecification.h
%{_includedir}/aws/dynamodb/model/SSEStatus.h
%{_includedir}/aws/dynamodb/model/SSEType.h
%{_includedir}/aws/dynamodb/model/ScalarAttributeType.h
%{_includedir}/aws/dynamodb/model/ScanRequest.h
%{_includedir}/aws/dynamodb/model/ScanResult.h
%{_includedir}/aws/dynamodb/model/Select.h
%{_includedir}/aws/dynamodb/model/SourceTableDetails.h
%{_includedir}/aws/dynamodb/model/SourceTableFeatureDetails.h
%{_includedir}/aws/dynamodb/model/StreamSpecification.h
%{_includedir}/aws/dynamodb/model/StreamViewType.h
%{_includedir}/aws/dynamodb/model/TableDescription.h
%{_includedir}/aws/dynamodb/model/TableStatus.h
%{_includedir}/aws/dynamodb/model/Tag.h
%{_includedir}/aws/dynamodb/model/TagResourceRequest.h
%{_includedir}/aws/dynamodb/model/TimeToLiveDescription.h
%{_includedir}/aws/dynamodb/model/TimeToLiveSpecification.h
%{_includedir}/aws/dynamodb/model/TimeToLiveStatus.h
%{_includedir}/aws/dynamodb/model/UntagResourceRequest.h
%{_includedir}/aws/dynamodb/model/UpdateContinuousBackupsRequest.h
%{_includedir}/aws/dynamodb/model/UpdateContinuousBackupsResult.h
%{_includedir}/aws/dynamodb/model/UpdateGlobalSecondaryIndexAction.h
%{_includedir}/aws/dynamodb/model/UpdateGlobalTableRequest.h
%{_includedir}/aws/dynamodb/model/UpdateGlobalTableResult.h
%{_includedir}/aws/dynamodb/model/UpdateGlobalTableSettingsRequest.h
%{_includedir}/aws/dynamodb/model/UpdateGlobalTableSettingsResult.h
%{_includedir}/aws/dynamodb/model/UpdateItemRequest.h
%{_includedir}/aws/dynamodb/model/UpdateItemResult.h
%{_includedir}/aws/dynamodb/model/UpdateTableRequest.h
%{_includedir}/aws/dynamodb/model/UpdateTableResult.h
%{_includedir}/aws/dynamodb/model/UpdateTimeToLiveRequest.h
%{_includedir}/aws/dynamodb/model/UpdateTimeToLiveResult.h
%{_includedir}/aws/dynamodb/model/WriteRequest.h
%{_includedir}/aws/dynamodb/model/BillingMode.h
%{_includedir}/aws/dynamodb/model/BillingModeSummary.h
%{_includedir}/aws/dynamodb/model/CancellationReason.h
%{_includedir}/aws/dynamodb/model/ConditionCheck.h
%{_includedir}/aws/dynamodb/model/Delete.h
%{_includedir}/aws/dynamodb/model/DescribeEndpointsRequest.h
%{_includedir}/aws/dynamodb/model/DescribeEndpointsResult.h
%{_includedir}/aws/dynamodb/model/Endpoint.h
%{_includedir}/aws/dynamodb/model/Get.h
%{_includedir}/aws/dynamodb/model/ItemResponse.h
%{_includedir}/aws/dynamodb/model/Put.h
%{_includedir}/aws/dynamodb/model/ReturnValuesOnConditionCheckFailure.h
%{_includedir}/aws/dynamodb/model/TransactGetItem.h
%{_includedir}/aws/dynamodb/model/TransactGetItemsRequest.h
%{_includedir}/aws/dynamodb/model/TransactGetItemsResult.h
%{_includedir}/aws/dynamodb/model/TransactWriteItem.h
%{_includedir}/aws/dynamodb/model/TransactWriteItemsRequest.h
%{_includedir}/aws/dynamodb/model/TransactWriteItemsResult.h
%{_includedir}/aws/dynamodb/model/Update.h
%{_includedir}/aws/dynamodb/model/ArchivalSummary.h
%{_includedir}/aws/dynamodb/model/ContributorInsightsAction.h
%{_includedir}/aws/dynamodb/model/ContributorInsightsStatus.h
%{_includedir}/aws/dynamodb/model/ContributorInsightsSummary.h
%{_includedir}/aws/dynamodb/model/CreateReplicationGroupMemberAction.h
%{_includedir}/aws/dynamodb/model/DeleteReplicationGroupMemberAction.h
%{_includedir}/aws/dynamodb/model/DescribeContributorInsightsRequest.h
%{_includedir}/aws/dynamodb/model/DescribeContributorInsightsResult.h
%{_includedir}/aws/dynamodb/model/DescribeTableReplicaAutoScalingRequest.h
%{_includedir}/aws/dynamodb/model/DescribeTableReplicaAutoScalingResult.h
%{_includedir}/aws/dynamodb/model/FailureException.h
%{_includedir}/aws/dynamodb/model/GlobalSecondaryIndexAutoScalingUpdate.h
%{_includedir}/aws/dynamodb/model/ListContributorInsightsRequest.h
%{_includedir}/aws/dynamodb/model/ListContributorInsightsResult.h
%{_includedir}/aws/dynamodb/model/ProvisionedThroughputOverride.h
%{_includedir}/aws/dynamodb/model/ReplicaAutoScalingDescription.h
%{_includedir}/aws/dynamodb/model/ReplicaAutoScalingUpdate.h
%{_includedir}/aws/dynamodb/model/ReplicaGlobalSecondaryIndex.h
%{_includedir}/aws/dynamodb/model/ReplicaGlobalSecondaryIndexAutoScalingDescription.h
%{_includedir}/aws/dynamodb/model/ReplicaGlobalSecondaryIndexAutoScalingUpdate.h
%{_includedir}/aws/dynamodb/model/ReplicaGlobalSecondaryIndexDescription.h
%{_includedir}/aws/dynamodb/model/ReplicationGroupUpdate.h
%{_includedir}/aws/dynamodb/model/TableAutoScalingDescription.h
%{_includedir}/aws/dynamodb/model/UpdateContributorInsightsRequest.h
%{_includedir}/aws/dynamodb/model/UpdateContributorInsightsResult.h
%{_includedir}/aws/dynamodb/model/UpdateReplicationGroupMemberAction.h
%{_includedir}/aws/dynamodb/model/UpdateTableReplicaAutoScalingRequest.h
%{_includedir}/aws/dynamodb/model/UpdateTableReplicaAutoScalingResult.h

%files dynamodb-libs
%{_libdir}/cmake/aws-cpp-sdk-dynamodb/aws-cpp-sdk-dynamodb-config-version.cmake
%{_libdir}/cmake/aws-cpp-sdk-dynamodb/aws-cpp-sdk-dynamodb-config.cmake
%{_libdir}/cmake/aws-cpp-sdk-dynamodb/aws-cpp-sdk-dynamodb-targets.cmake
%{_libdir}/cmake/aws-cpp-sdk-dynamodb/aws-cpp-sdk-dynamodb-targets-release.cmake
%{_libdir}/libaws-cpp-sdk-dynamodb.a

%files route53-devel
%{_includedir}/aws/route53/Route53Client.h
%{_includedir}/aws/route53/Route53Endpoint.h
%{_includedir}/aws/route53/Route53ErrorMarshaller.h
%{_includedir}/aws/route53/Route53Errors.h
%{_includedir}/aws/route53/Route53Request.h
%{_includedir}/aws/route53/Route53_EXPORTS.h
%{_includedir}/aws/route53/model/AccountLimit.h
%{_includedir}/aws/route53/model/AccountLimitType.h
%{_includedir}/aws/route53/model/AlarmIdentifier.h
%{_includedir}/aws/route53/model/AliasTarget.h
%{_includedir}/aws/route53/model/AssociateVPCWithHostedZoneRequest.h
%{_includedir}/aws/route53/model/AssociateVPCWithHostedZoneResult.h
%{_includedir}/aws/route53/model/Change.h
%{_includedir}/aws/route53/model/ChangeAction.h
%{_includedir}/aws/route53/model/ChangeBatch.h
%{_includedir}/aws/route53/model/ChangeInfo.h
%{_includedir}/aws/route53/model/ChangeResourceRecordSetsRequest.h
%{_includedir}/aws/route53/model/ChangeResourceRecordSetsResult.h
%{_includedir}/aws/route53/model/ChangeStatus.h
%{_includedir}/aws/route53/model/ChangeTagsForResourceRequest.h
%{_includedir}/aws/route53/model/ChangeTagsForResourceResult.h
%{_includedir}/aws/route53/model/CloudWatchAlarmConfiguration.h
%{_includedir}/aws/route53/model/CloudWatchRegion.h
%{_includedir}/aws/route53/model/ComparisonOperator.h
%{_includedir}/aws/route53/model/CreateHealthCheckRequest.h
%{_includedir}/aws/route53/model/CreateHealthCheckResult.h
%{_includedir}/aws/route53/model/CreateHostedZoneRequest.h
%{_includedir}/aws/route53/model/CreateHostedZoneResult.h
%{_includedir}/aws/route53/model/CreateQueryLoggingConfigRequest.h
%{_includedir}/aws/route53/model/CreateQueryLoggingConfigResult.h
%{_includedir}/aws/route53/model/CreateReusableDelegationSetRequest.h
%{_includedir}/aws/route53/model/CreateReusableDelegationSetResult.h
%{_includedir}/aws/route53/model/CreateTrafficPolicyInstanceRequest.h
%{_includedir}/aws/route53/model/CreateTrafficPolicyInstanceResult.h
%{_includedir}/aws/route53/model/CreateTrafficPolicyRequest.h
%{_includedir}/aws/route53/model/CreateTrafficPolicyResult.h
%{_includedir}/aws/route53/model/CreateTrafficPolicyVersionRequest.h
%{_includedir}/aws/route53/model/CreateTrafficPolicyVersionResult.h
%{_includedir}/aws/route53/model/CreateVPCAssociationAuthorizationRequest.h
%{_includedir}/aws/route53/model/CreateVPCAssociationAuthorizationResult.h
%{_includedir}/aws/route53/model/DelegationSet.h
%{_includedir}/aws/route53/model/DeleteHealthCheckRequest.h
%{_includedir}/aws/route53/model/DeleteHealthCheckResult.h
%{_includedir}/aws/route53/model/DeleteHostedZoneRequest.h
%{_includedir}/aws/route53/model/DeleteHostedZoneResult.h
%{_includedir}/aws/route53/model/DeleteQueryLoggingConfigRequest.h
%{_includedir}/aws/route53/model/DeleteQueryLoggingConfigResult.h
%{_includedir}/aws/route53/model/DeleteReusableDelegationSetRequest.h
%{_includedir}/aws/route53/model/DeleteReusableDelegationSetResult.h
%{_includedir}/aws/route53/model/DeleteTrafficPolicyInstanceRequest.h
%{_includedir}/aws/route53/model/DeleteTrafficPolicyInstanceResult.h
%{_includedir}/aws/route53/model/DeleteTrafficPolicyRequest.h
%{_includedir}/aws/route53/model/DeleteTrafficPolicyResult.h
%{_includedir}/aws/route53/model/DeleteVPCAssociationAuthorizationRequest.h
%{_includedir}/aws/route53/model/DeleteVPCAssociationAuthorizationResult.h
%{_includedir}/aws/route53/model/Dimension.h
%{_includedir}/aws/route53/model/DisassociateVPCFromHostedZoneRequest.h
%{_includedir}/aws/route53/model/DisassociateVPCFromHostedZoneResult.h
%{_includedir}/aws/route53/model/GeoLocation.h
%{_includedir}/aws/route53/model/GeoLocationDetails.h
%{_includedir}/aws/route53/model/GetAccountLimitRequest.h
%{_includedir}/aws/route53/model/GetAccountLimitResult.h
%{_includedir}/aws/route53/model/GetChangeRequest.h
%{_includedir}/aws/route53/model/GetChangeResult.h
%{_includedir}/aws/route53/model/GetCheckerIpRangesRequest.h
%{_includedir}/aws/route53/model/GetCheckerIpRangesResult.h
%{_includedir}/aws/route53/model/GetGeoLocationRequest.h
%{_includedir}/aws/route53/model/GetGeoLocationResult.h
%{_includedir}/aws/route53/model/GetHealthCheckCountRequest.h
%{_includedir}/aws/route53/model/GetHealthCheckCountResult.h
%{_includedir}/aws/route53/model/GetHealthCheckLastFailureReasonRequest.h
%{_includedir}/aws/route53/model/GetHealthCheckLastFailureReasonResult.h
%{_includedir}/aws/route53/model/GetHealthCheckRequest.h
%{_includedir}/aws/route53/model/GetHealthCheckResult.h
%{_includedir}/aws/route53/model/GetHealthCheckStatusRequest.h
%{_includedir}/aws/route53/model/GetHealthCheckStatusResult.h
%{_includedir}/aws/route53/model/GetHostedZoneCountRequest.h
%{_includedir}/aws/route53/model/GetHostedZoneCountResult.h
%{_includedir}/aws/route53/model/GetHostedZoneLimitRequest.h
%{_includedir}/aws/route53/model/GetHostedZoneLimitResult.h
%{_includedir}/aws/route53/model/GetHostedZoneRequest.h
%{_includedir}/aws/route53/model/GetHostedZoneResult.h
%{_includedir}/aws/route53/model/GetQueryLoggingConfigRequest.h
%{_includedir}/aws/route53/model/GetQueryLoggingConfigResult.h
%{_includedir}/aws/route53/model/GetReusableDelegationSetLimitRequest.h
%{_includedir}/aws/route53/model/GetReusableDelegationSetLimitResult.h
%{_includedir}/aws/route53/model/GetReusableDelegationSetRequest.h
%{_includedir}/aws/route53/model/GetReusableDelegationSetResult.h
%{_includedir}/aws/route53/model/GetTrafficPolicyInstanceCountRequest.h
%{_includedir}/aws/route53/model/GetTrafficPolicyInstanceCountResult.h
%{_includedir}/aws/route53/model/GetTrafficPolicyInstanceRequest.h
%{_includedir}/aws/route53/model/GetTrafficPolicyInstanceResult.h
%{_includedir}/aws/route53/model/GetTrafficPolicyRequest.h
%{_includedir}/aws/route53/model/GetTrafficPolicyResult.h
%{_includedir}/aws/route53/model/HealthCheck.h
%{_includedir}/aws/route53/model/HealthCheckConfig.h
%{_includedir}/aws/route53/model/HealthCheckObservation.h
%{_includedir}/aws/route53/model/HealthCheckRegion.h
%{_includedir}/aws/route53/model/HealthCheckType.h
%{_includedir}/aws/route53/model/HostedZone.h
%{_includedir}/aws/route53/model/HostedZoneConfig.h
%{_includedir}/aws/route53/model/HostedZoneLimit.h
%{_includedir}/aws/route53/model/HostedZoneLimitType.h
%{_includedir}/aws/route53/model/InsufficientDataHealthStatus.h
%{_includedir}/aws/route53/model/LinkedService.h
%{_includedir}/aws/route53/model/ListGeoLocationsRequest.h
%{_includedir}/aws/route53/model/ListGeoLocationsResult.h
%{_includedir}/aws/route53/model/ListHealthChecksRequest.h
%{_includedir}/aws/route53/model/ListHealthChecksResult.h
%{_includedir}/aws/route53/model/ListHostedZonesByNameRequest.h
%{_includedir}/aws/route53/model/ListHostedZonesByNameResult.h
%{_includedir}/aws/route53/model/ListHostedZonesRequest.h
%{_includedir}/aws/route53/model/ListHostedZonesResult.h
%{_includedir}/aws/route53/model/ListQueryLoggingConfigsRequest.h
%{_includedir}/aws/route53/model/ListQueryLoggingConfigsResult.h
%{_includedir}/aws/route53/model/ListResourceRecordSetsRequest.h
%{_includedir}/aws/route53/model/ListResourceRecordSetsResult.h
%{_includedir}/aws/route53/model/ListReusableDelegationSetsRequest.h
%{_includedir}/aws/route53/model/ListReusableDelegationSetsResult.h
%{_includedir}/aws/route53/model/ListTagsForResourceRequest.h
%{_includedir}/aws/route53/model/ListTagsForResourceResult.h
%{_includedir}/aws/route53/model/ListTagsForResourcesRequest.h
%{_includedir}/aws/route53/model/ListTagsForResourcesResult.h
%{_includedir}/aws/route53/model/ListTrafficPoliciesRequest.h
%{_includedir}/aws/route53/model/ListTrafficPoliciesResult.h
%{_includedir}/aws/route53/model/ListTrafficPolicyInstancesByHostedZoneRequest.h
%{_includedir}/aws/route53/model/ListTrafficPolicyInstancesByHostedZoneResult.h
%{_includedir}/aws/route53/model/ListTrafficPolicyInstancesByPolicyRequest.h
%{_includedir}/aws/route53/model/ListTrafficPolicyInstancesByPolicyResult.h
%{_includedir}/aws/route53/model/ListTrafficPolicyInstancesRequest.h
%{_includedir}/aws/route53/model/ListTrafficPolicyInstancesResult.h
%{_includedir}/aws/route53/model/ListTrafficPolicyVersionsRequest.h
%{_includedir}/aws/route53/model/ListTrafficPolicyVersionsResult.h
%{_includedir}/aws/route53/model/ListVPCAssociationAuthorizationsRequest.h
%{_includedir}/aws/route53/model/ListVPCAssociationAuthorizationsResult.h
%{_includedir}/aws/route53/model/QueryLoggingConfig.h
%{_includedir}/aws/route53/model/RRType.h
%{_includedir}/aws/route53/model/ResettableElementName.h
%{_includedir}/aws/route53/model/ResourceRecord.h
%{_includedir}/aws/route53/model/ResourceRecordSet.h
%{_includedir}/aws/route53/model/ResourceRecordSetFailover.h
%{_includedir}/aws/route53/model/ResourceRecordSetRegion.h
%{_includedir}/aws/route53/model/ResourceTagSet.h
%{_includedir}/aws/route53/model/ReusableDelegationSetLimit.h
%{_includedir}/aws/route53/model/ReusableDelegationSetLimitType.h
%{_includedir}/aws/route53/model/Statistic.h
%{_includedir}/aws/route53/model/StatusReport.h
%{_includedir}/aws/route53/model/Tag.h
%{_includedir}/aws/route53/model/TagResourceType.h
%{_includedir}/aws/route53/model/TestDNSAnswerRequest.h
%{_includedir}/aws/route53/model/TestDNSAnswerResult.h
%{_includedir}/aws/route53/model/TrafficPolicy.h
%{_includedir}/aws/route53/model/TrafficPolicyInstance.h
%{_includedir}/aws/route53/model/TrafficPolicySummary.h
%{_includedir}/aws/route53/model/UpdateHealthCheckRequest.h
%{_includedir}/aws/route53/model/UpdateHealthCheckResult.h
%{_includedir}/aws/route53/model/UpdateHostedZoneCommentRequest.h
%{_includedir}/aws/route53/model/UpdateHostedZoneCommentResult.h
%{_includedir}/aws/route53/model/UpdateTrafficPolicyCommentRequest.h
%{_includedir}/aws/route53/model/UpdateTrafficPolicyCommentResult.h
%{_includedir}/aws/route53/model/UpdateTrafficPolicyInstanceRequest.h
%{_includedir}/aws/route53/model/UpdateTrafficPolicyInstanceResult.h
%{_includedir}/aws/route53/model/VPC.h
%{_includedir}/aws/route53/model/VPCRegion.h

%files route53-libs
%{_libdir}/cmake/aws-cpp-sdk-route53/aws-cpp-sdk-route53-targets-release.cmake
%{_libdir}/cmake/aws-cpp-sdk-route53/aws-cpp-sdk-route53-config-version.cmake
%{_libdir}/cmake/aws-cpp-sdk-route53/aws-cpp-sdk-route53-config.cmake
%{_libdir}/cmake/aws-cpp-sdk-route53/aws-cpp-sdk-route53-targets.cmake
%{_libdir}/libaws-cpp-sdk-route53.a

%changelog
* Mon Aug 13 2018 Lincoln Bryant <lincolnb@uchicago.edu> - 1.4.70-1
- Initial package

* Mon Mar 1 2022 Suchandra Thapa <sthapa@uchicago.edu> - 1.7.345-1
- Update to use aws-cpp-sdk 1.7.345