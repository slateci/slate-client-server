Name: slate-api-server
Version: 942
Release: 1%{?dist}
Summary: API server for SLATE Cyber-Infrastructure
License: MIT
URL: https://github.com/slateci/slate-api-server

Source0: slate-client-server-%{version}.tar.gz

BuildRequires: gcc-c++ boost-devel zlib-devel openssl-devel libcurl-devel yaml-cpp-devel cmake3 aws-sdk-cpp-dynamodb-devel aws-sdk-cpp-route53-devel openssl-static
Requires: boost zlib openssl libcurl yaml-cpp aws-sdk-cpp-dynamodb-libs aws-sdk-cpp-route53-libs cryptopp

%description
SLATE API Server

%prep
%setup -c -n slate-client-server-%{version}.tar.gz 

%build
cd slate-client-server
mkdir -p build
cd build
cmake3 -DCMAKE_INSTALL_PREFIX="$RPM_BUILD_ROOT/usr/" -DBUILD_CLIENT=False -DBUILD_SERVER_TESTS=False –DAWS_CUSTOM_MEMORY_MANAGEMENT=0 ..
make

%install
cd slate-client-server
cd build
rm -rf "$RPM_BUILD_ROOT"
echo "$RPM_BUILD_ROOT"
make install
cd ..

%clean
rm -rf "$RPM_BUILD_ROOT"

%post
/sbin/ldconfig

%postun
/sbin/ldconfig

%files
%{_bindir}/slate-service

%defattr(-,root,root,-)

