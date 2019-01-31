Name: slate-client
Version: %{version}
Release: 1%{?dist}
Summary: Command line client for SLATE Cyber-Infrastructure
License: MIT
URL: https://github.com/slateci/slate-client-server

Source0: slate-client-server-%{version}.tar.gz

BuildRequires: gcc-c++ boost-devel zlib-devel openssl-devel libcurl-devel yaml-cpp-devel cmake3 aws-sdk-cpp-dynamodb-devel
Requires: boost zlib openssl libcurl yaml-cpp aws-sdk-cpp-dynamodb-libs

%description
SLATE CLI Client

%prep
%setup -c -n slate-client-server-%{version}.tar.gz 

%build
cd slate-client-server
mkdir build
cd build
cmake3 -DCMAKE_INSTALL_PREFIX="$RPM_BUILD_ROOT/usr/" -DBUILD_CLIENT=True -DBUILD_SERVER=False ..
make -j3

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
%{_bindir}/slate

%defattr(-,root,root,-)

