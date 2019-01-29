Name: slate-api-server
Version: 0.1.0
Release: 1%{?dist}
Summary: API server for SLATE Cyber-Infrastructure
License: MIT
URL: https://github.com/slateci/slate-api-server

Source0: %{name}-%{version}.tar.gz

BuildRequires: gcc-c++ boost-devel zlib-devel openssl-devel libcurl-devel yaml-cpp-devel cmake3 aws-sdk-cpp-dynamodb-devel
Requires: boost zlib openssl libcurl yaml-cpp aws-sdk-cpp-dynamodb-libs

%description
SLATE API Server

%prep
%setup -c -n %{name}-%{version}.tar.gz 

%build
cd %{name}
mkdir build
cd build
cmake3 -DCMAKE_INSTALL_PREFIX="$RPM_BUILD_ROOT/usr/" -DBUILD_CLIENT=False -DBUILD_SERVER_TESTS=False ..
make -j3

%install
cd %{name}
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

