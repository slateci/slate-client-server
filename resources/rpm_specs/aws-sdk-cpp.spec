Name: aws-sdk-cpp
Version: 1.5.25
Release: 1%{?dist}
Summary: AWS C++ SDK
License: MIT
URL: https://github.com/aws/aws-sdk-cpp

# curl -L https://github.com/aws/aws-sdk-cpp/archive/1.5.25.tar.gz -o aws-sdk-cpp-1.5.25.tar.gz
Source: %{name}-%{version}.tar.gz

BuildRequires: gcc-c++ cmake3

%description
Amazon AWS SDK for C++

#TODO further split this out into core- and other AWS tools

%package dynamodb-devel
Summary: headers for AWS C++ SDK for DynamoDB
Group: Development/Libraries
%description dynamodb-devel
%{summary}.

%package dynamodb-libs
Summary: AWS C++ SDK runtime libraries for DynamoDB
Group: System Environment/Libraries
%description dynamodb-libs
%{summary}.

%prep
%setup -c -n %{name}-%{version}.tar.gz 

%build
cd %{name}-%{version}
mkdir build
cd build
cmake3 .. -DBUILD_ONLY="dynamodb" -DBUILD_SHARED_LIBS=Off
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
cd ..

%clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%files dynamodb-devel
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
%{_includedir}/aws/external/gtest.h
%{_includedir}/aws/testing/MemoryTesting.h
%{_includedir}/aws/testing/ProxyConfig.h
%{_includedir}/aws/testing/TestingEnvironment.h
%{_includedir}/aws/testing/Testing_EXPORTS.h
%{_includedir}/aws/testing/mocks/aws/auth/MockAWSHttpResourceClient.h
%{_includedir}/aws/testing/mocks/http/MockHttpClient.h
%{_includedir}/aws/testing/platform/PlatformTesting.h

%files dynamodb-libs
%{_libdir}/cmake/AWSSDK/AWSSDKConfig.cmake
%{_libdir}/cmake/AWSSDK/AWSSDKConfigVersion.cmake
%{_libdir}/cmake/AWSSDK/build_external.cmake
%{_libdir}/cmake/AWSSDK/compiler_settings.cmake
%{_libdir}/cmake/AWSSDK/dependencies.cmake
%{_libdir}/cmake/AWSSDK/external_dependencies.cmake
%{_libdir}/cmake/AWSSDK/initialize_project_version.cmake
%{_libdir}/cmake/AWSSDK/make_uninstall.cmake
%{_libdir}/cmake/AWSSDK/platform/android.cmake
%{_libdir}/cmake/AWSSDK/platform/android.toolchain.cmake
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
%{_libdir}/cmake/aws-cpp-sdk-core/aws-cpp-sdk-core-targets-noconfig.cmake
%{_libdir}/cmake/aws-cpp-sdk-core/aws-cpp-sdk-core-targets.cmake
%{_libdir}/cmake/aws-cpp-sdk-dynamodb/aws-cpp-sdk-dynamodb-config-version.cmake
%{_libdir}/cmake/aws-cpp-sdk-dynamodb/aws-cpp-sdk-dynamodb-config.cmake
%{_libdir}/cmake/aws-cpp-sdk-dynamodb/aws-cpp-sdk-dynamodb-targets-noconfig.cmake
%{_libdir}/cmake/aws-cpp-sdk-dynamodb/aws-cpp-sdk-dynamodb-targets.cmake
%{_libdir}/cmake/testing-resources/testing-resources-config-version.cmake
%{_libdir}/cmake/testing-resources/testing-resources-config.cmake
%{_libdir}/cmake/testing-resources/testing-resources-targets-noconfig.cmake
%{_libdir}/cmake/testing-resources/testing-resources-targets.cmake
%{_libdir}/libaws-cpp-sdk-core.a
%{_libdir}/libaws-cpp-sdk-dynamodb.a
%{_libdir}/libtesting-resources.a

%changelog
* Mon Aug 13 2018 Lincoln Bryant <lincolnb@uchicago.edu> - 1.4.70-1
- Initial package
